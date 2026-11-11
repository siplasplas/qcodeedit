#include <qce/encoding/TextFile.h>

#include <qce/encoding/EncodingGuard.h>

#include <qce/CodeEditArea.h>
#include <qce/LineDiff.h>
#include <qce/SimpleTextDocument.h>

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUndoStack>
#include <QVector>

namespace qce::encoding {
namespace {

// While reading or saving, the undo stack passes through states (an edit
// pushed, then cleared) that are not changes of the document's state; it is
// reported once at the end.
class BusyScope {
public:
    BusyScope(bool& busy, std::function<void()> done) : m_busy(busy), m_done(std::move(done)) { m_busy = true; }
    ~BusyScope() {
        m_busy = false;
        m_done();
    }
    BusyScope(const BusyScope&) = delete;
    BusyScope& operator=(const BusyScope&) = delete;

private:
    bool& m_busy;
    std::function<void()> m_done;
};

} // namespace

struct TextFile::Private {
    QPointer<CodeEditArea> m_area;
    SimpleTextDocument* m_document;
    EncodingGuard* m_guard;
    QString m_path;
    QString m_savedText;
    QString m_error;
    QStringList m_legacyLanguages;
    TrimWhitespace m_trim = TrimWhitespace::Off;
    Filter m_readFilter;
    Filter m_writeFilter;
    EncodingDetection m_uncertain;
    QDateTime m_diskModified;
    qint64 m_diskSize = -1;
    bool m_formatChanged = false;    ///< the encoding to save in changed
    bool m_reportedModified = false; ///< isModified() as last reported
    bool m_settingFormat = false;    ///< the format changes by reading or saving, not by the user
    /// Reading or saving: the undo stack's passing states are not reported.
    bool m_busy = false;
};

TextFile::TextFile(CodeEditArea* area, SimpleTextDocument* document, QObject* parent)
    :
    QObject(parent),
      d(std::make_unique<Private>()) {
    d->m_area = area;
    d->m_document = document;
    // Keeps typed and pasted text storable in the file's code page.
    d->m_guard = new EncodingGuard(area, this);
    connect(d->m_guard, &EncodingGuard::encodingChanged, this, [this] {
        // Switching to UTF-8 (by the user) is a change that needs saving;
        // reading and saving only set the format the file has.
        if (!d->m_settingFormat) d->m_formatChanged = true;
        updateModified();
        emit formatChanged();
    });
    connect(d->m_area->undoStack(), &QUndoStack::cleanChanged, this, &TextFile::updateModified);
}

// The area and its undo stack may go after this object: what they report
// while going must not reach it.
TextFile::~TextFile() {
    if (d->m_area) disconnect(d->m_area->undoStack(), nullptr, this, nullptr);
}

const FileFormat& TextFile::format() const {
    return d->m_guard->format();
}

void TextFile::setSaveEncoding(const QString& encoding) {
    d->m_guard->setEncoding(encoding);
}

bool TextFile::isModified() const {
    return d->m_formatChanged || !d->m_area->undoStack()->isClean();
}

void TextFile::updateModified() {
    if (d->m_busy) return;
    const bool modified = isModified();
    if (modified == d->m_reportedModified) return;
    d->m_reportedModified = modified;
    emit modificationChanged(modified);
}

void TextFile::setPath(const QString& path) {
    if (path == d->m_path) return;
    const QString previous = d->m_path;
    d->m_path = path;
    emit pathChanged(d->m_path, previous);
}

void TextFile::setFormat(const FileFormat& format) {
    const bool lineBreaksChanged = format.crlf != d->m_guard->format().crlf || format.cr != d->m_guard->format().cr
                                   || format.bom != d->m_guard->format().bom
                                   || format.mixedLineBreaks != d->m_guard->format().mixedLineBreaks;
    d->m_settingFormat = true;
    d->m_guard->setFormat(format);  // emits formatChanged() when the encoding changed
    d->m_settingFormat = false;
    if (lineBreaksChanged && format.encoding == d->m_guard->encoding()) emit formatChanged();
}

TextFile::Result TextFile::open(const QString& path, const QString& encoding) {
    d->m_error.clear();
    if (QFileInfo::exists(path)) return read(path, encoding, false);
    const BusyScope busy(d->m_busy, [this] { updateModified(); });
    // A name that does not exist yet (e.g. a new file named in a file
    // manager): an empty UTF-8 document, saved there.
    d->m_document->setText(QString());
    d->m_savedText.clear();
    d->m_area->undoStack()->clear();
    d->m_area->setCursorPosition({0, 0});
    d->m_uncertain = {};
    setFormat({QStringLiteral("utf8"), false, false, true});
    d->m_formatChanged = false;
    d->m_diskModified = {};
    d->m_diskSize = -1;
    setPath(path);
    return Result::Ok;
}

TextFile::Result TextFile::reopen(const QString& encoding) {
    d->m_error.clear();
    if (d->m_path.isEmpty()) return Result::Failed;
    return read(d->m_path, encoding, false);
}

TextFile::Result TextFile::reload() {
    d->m_error.clear();
    if (d->m_path.isEmpty()) return Result::Failed;
    // Keep the encoding the file was opened in; detect again if it no longer fits.
    const Result result = read(d->m_path, encoding(), true);
    if (result != Result::Failed) return result;
    return read(d->m_path, {}, true);
}

TextFile::Result TextFile::read(const QString& path, const QString& encoding, bool asEdit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        d->m_error = file.errorString();
        return Result::Failed;
    }
    const QFileInfo stamp(file);
    const QDateTime modified = stamp.lastModified();
    const qint64 size = stamp.size();
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        d->m_error = file.errorString();
        return Result::Failed;
    }
    if (d->m_readFilter) {
        const Result filtered = d->m_readFilter(path, &bytes);
        if (filtered != Result::Ok) return filtered;
    }
    // Code page or UTF, detected unless given; saved back the same way.
    const DecodeResult decoded = decode(bytes, encoding, d->m_legacyLanguages);
    if (!decoded.ok) {
        d->m_error = decoded.error;
        return Result::Failed;
    }
    // A given encoding (chosen, or kept on reload) keeps the alternatives
    // found on opening, e.g. for a Reopen menu.
    if (encoding.isEmpty())
        d->m_uncertain = decoded.detection.confident ? EncodingDetection() : decoded.detection;

    const BusyScope busy(d->m_busy, [this] { updateModified(); });
    // With qcodeedit-diff only the lines that differ change, as one undo step.
    d->m_document->setText(decoded.text);
    d->m_savedText = d->m_document->toPlainText();
    if (asEdit) {
        d->m_area->undoStack()->setClean();
    } else {
        d->m_area->undoStack()->clear();
        // A file opened starts at its top: with qcodeedit-diff the text
        // replaces the old one as an edit, and the cursor went with the line
        // the diff matched.
        d->m_area->setCursorPosition({0, 0});
    }
    setFormat(decoded.format);
    d->m_formatChanged = false;
    d->m_diskModified = modified;
    d->m_diskSize = size;
    setPath(path);
    emit loaded(bytes);
    return Result::Ok;
}

TextFile::Result TextFile::save() {
    if (d->m_path.isEmpty()) {
        d->m_error = tr("The document has no file name.");
        return Result::Failed;
    }
    return saveAs(d->m_path);
}

TextFile::Result TextFile::saveAs(const QString& path) {
    d->m_error.clear();
    const BusyScope busy(d->m_busy, [this] { updateModified(); });
    trimTrailingWhitespace();
    // In the file's encoding; characters it cannot store bring up the
    // guard's choice (write '?', switch to UTF-8, or cancel).
    QByteArray bytes;
    if (!d->m_guard->encodeForSave(d->m_document->toPlainText(), &bytes)) return Result::Cancelled;
    if (d->m_writeFilter) {
        const Result filtered = d->m_writeFilter(path, &bytes);
        if (filtered != Result::Ok) return filtered;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        d->m_error = file.errorString();
        return Result::Failed;
    }

    const QString previous = d->m_path;
    setPath(path);
    updateDiskStamp();
    d->m_savedText = d->m_document->toPlainText();
    d->m_formatChanged = false;
    d->m_area->undoStack()->setClean();
    if (format().mixedLineBreaks) {
        // Saved with one kind of line break for every line.
        FileFormat saved = format();
        saved.mixedLineBreaks = false;
        setFormat(saved);
    }
    emit saved(path, previous);
    return Result::Ok;
}

bool TextFile::hasExternalChanges() const {
    if (d->m_path.isEmpty()) return false;
    const QFileInfo file(d->m_path);
    return file.isFile() && (file.lastModified() != d->m_diskModified || file.size() != d->m_diskSize);
}

void TextFile::updateDiskStamp() {
    const QFileInfo file(d->m_path);
    d->m_diskModified = file.lastModified();
    d->m_diskSize = file.size();
}

// One undo step: the new text goes through setText(), which with
// qcodeedit-diff changes only the trimmed lines. The cursor's line is left
// alone, so that typing on after a space goes on as typed.
void TextFile::trimTrailingWhitespace() {
    if (d->m_trim == TrimWhitespace::Off || !lineDiff()) return;
    QStringList lines = d->m_document->toPlainText().split(QLatin1Char('\n'));
    QVector<bool> trim(lines.size(), d->m_trim == TrimWhitespace::AllLines);
    if (d->m_trim == TrimWhitespace::ChangedLines) {
        // Lines nobody touched keep their whitespace (no stray changes, e.g. in git).
        const QStringList saved = d->m_savedText.split(QLatin1Char('\n'));
        for (const LineChange& change : lineDiff()(saved, lines))
            for (int i = change.newStart; i < change.newStart + change.newCount && i < lines.size(); ++i)
                trim[i] = true;
    }
    const int cursorLine = d->m_area->cursorPosition().line;
    bool changed = false;
    for (int i = 0; i < lines.size(); ++i) {
        if (!trim[i] || i == cursorLine) continue;
        QString& line = lines[i];
        int end = int(line.size());
        while (end > 0 && (line.at(end - 1) == QLatin1Char(' ') || line.at(end - 1) == QLatin1Char('\t')))
            --end;
        if (end == line.size()) continue;
        line.truncate(end);
        changed = true;
    }
    // setText() drops one final line break: the extra one keeps an empty last line.
    if (changed) d->m_document->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}


EncodingGuard* TextFile::guard() const { return d->m_guard; }
QString TextFile::path() const { return d->m_path; }
void TextFile::setLegacyLanguages(const QStringList& languages) { d->m_legacyLanguages = languages; }
QStringList TextFile::legacyLanguages() const { return d->m_legacyLanguages; }
void TextFile::setTrimWhitespace(TrimWhitespace mode) { d->m_trim = mode; }
TextFile::TrimWhitespace TextFile::trimWhitespace() const { return d->m_trim; }
void TextFile::setReadFilter(Filter filter) { d->m_readFilter = std::move(filter); }
void TextFile::setWriteFilter(Filter filter) { d->m_writeFilter = std::move(filter); }
void TextFile::setErrorString(const QString& error) { d->m_error = error; }
QString TextFile::errorString() const { return d->m_error; }
const EncodingDetection& TextFile::uncertainDetection() const { return d->m_uncertain; }
QString TextFile::savedText() const { return d->m_savedText; }

} // namespace qce::encoding
