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

TextFile::TextFile(CodeEditArea* area, SimpleTextDocument* document, QObject* parent)
    : QObject(parent), m_area(area), m_document(document) {
    // Keeps typed and pasted text storable in the file's code page.
    m_guard = new EncodingGuard(area, this);
    connect(m_guard, &EncodingGuard::encodingChanged, this, [this] {
        // Switching to UTF-8 (by the user) is a change that needs saving;
        // reading and saving only set the format the file has.
        if (!m_settingFormat) m_formatChanged = true;
        updateModified();
        emit formatChanged();
    });
    connect(m_area->undoStack(), &QUndoStack::cleanChanged, this, &TextFile::updateModified);
}

// The area and its undo stack may go after this object: what they report
// while going must not reach it.
TextFile::~TextFile() {
    if (m_area) disconnect(m_area->undoStack(), nullptr, this, nullptr);
}

const FileFormat& TextFile::format() const {
    return m_guard->format();
}

void TextFile::setSaveEncoding(const QString& encoding) {
    m_guard->setEncoding(encoding);
}

bool TextFile::isModified() const {
    return m_formatChanged || !m_area->undoStack()->isClean();
}

void TextFile::updateModified() {
    if (m_busy) return;
    const bool modified = isModified();
    if (modified == m_reportedModified) return;
    m_reportedModified = modified;
    emit modificationChanged(modified);
}

void TextFile::setPath(const QString& path) {
    if (path == m_path) return;
    const QString previous = m_path;
    m_path = path;
    emit pathChanged(m_path, previous);
}

void TextFile::setFormat(const FileFormat& format) {
    const bool lineBreaksChanged = format.crlf != m_guard->format().crlf || format.cr != m_guard->format().cr
                                   || format.bom != m_guard->format().bom
                                   || format.mixedLineBreaks != m_guard->format().mixedLineBreaks;
    m_settingFormat = true;
    m_guard->setFormat(format);  // emits formatChanged() when the encoding changed
    m_settingFormat = false;
    if (lineBreaksChanged && format.encoding == m_guard->encoding()) emit formatChanged();
}

TextFile::Result TextFile::open(const QString& path, const QString& encoding) {
    m_error.clear();
    if (QFileInfo::exists(path)) return read(path, encoding, false);
    const BusyScope busy(m_busy, [this] { updateModified(); });
    // A name that does not exist yet (e.g. a new file named in a file
    // manager): an empty UTF-8 document, saved there.
    m_document->setText(QString());
    m_savedText.clear();
    m_area->undoStack()->clear();
    m_area->setCursorPosition({0, 0});
    m_uncertain = {};
    setFormat({QStringLiteral("utf8"), false, false, true});
    m_formatChanged = false;
    m_diskModified = {};
    m_diskSize = -1;
    setPath(path);
    return Result::Ok;
}

TextFile::Result TextFile::reopen(const QString& encoding) {
    m_error.clear();
    if (m_path.isEmpty()) return Result::Failed;
    return read(m_path, encoding, false);
}

TextFile::Result TextFile::reload() {
    m_error.clear();
    if (m_path.isEmpty()) return Result::Failed;
    // Keep the encoding the file was opened in; detect again if it no longer fits.
    const Result result = read(m_path, encoding(), true);
    if (result != Result::Failed) return result;
    return read(m_path, {}, true);
}

TextFile::Result TextFile::read(const QString& path, const QString& encoding, bool asEdit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return Result::Failed;
    }
    const QFileInfo stamp(file);
    const QDateTime modified = stamp.lastModified();
    const qint64 size = stamp.size();
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        m_error = file.errorString();
        return Result::Failed;
    }
    if (m_readFilter) {
        const Result filtered = m_readFilter(path, &bytes);
        if (filtered != Result::Ok) return filtered;
    }
    // Code page or UTF, detected unless given; saved back the same way.
    const DecodeResult decoded = decode(bytes, encoding, m_legacyLanguages);
    if (!decoded.ok) {
        m_error = decoded.error;
        return Result::Failed;
    }
    // A given encoding (chosen, or kept on reload) keeps the alternatives
    // found on opening, e.g. for a Reopen menu.
    if (encoding.isEmpty())
        m_uncertain = decoded.detection.confident ? EncodingDetection() : decoded.detection;

    const BusyScope busy(m_busy, [this] { updateModified(); });
    // With qcodeedit-diff only the lines that differ change, as one undo step.
    m_document->setText(decoded.text);
    m_savedText = m_document->toPlainText();
    if (asEdit) {
        m_area->undoStack()->setClean();
    } else {
        m_area->undoStack()->clear();
        // A file opened starts at its top: with qcodeedit-diff the text
        // replaces the old one as an edit, and the cursor went with the line
        // the diff matched.
        m_area->setCursorPosition({0, 0});
    }
    setFormat(decoded.format);
    m_formatChanged = false;
    m_diskModified = modified;
    m_diskSize = size;
    setPath(path);
    emit loaded(bytes);
    return Result::Ok;
}

TextFile::Result TextFile::save() {
    if (m_path.isEmpty()) {
        m_error = tr("The document has no file name.");
        return Result::Failed;
    }
    return saveAs(m_path);
}

TextFile::Result TextFile::saveAs(const QString& path) {
    m_error.clear();
    const BusyScope busy(m_busy, [this] { updateModified(); });
    trimTrailingWhitespace();
    // In the file's encoding; characters it cannot store bring up the
    // guard's choice (write '?', switch to UTF-8, or cancel).
    QByteArray bytes;
    if (!m_guard->encodeForSave(m_document->toPlainText(), &bytes)) return Result::Cancelled;
    if (m_writeFilter) {
        const Result filtered = m_writeFilter(path, &bytes);
        if (filtered != Result::Ok) return filtered;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = file.errorString();
        return Result::Failed;
    }

    const QString previous = m_path;
    setPath(path);
    updateDiskStamp();
    m_savedText = m_document->toPlainText();
    m_formatChanged = false;
    m_area->undoStack()->setClean();
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
    if (m_path.isEmpty()) return false;
    const QFileInfo file(m_path);
    return file.isFile() && (file.lastModified() != m_diskModified || file.size() != m_diskSize);
}

void TextFile::updateDiskStamp() {
    const QFileInfo file(m_path);
    m_diskModified = file.lastModified();
    m_diskSize = file.size();
}

// One undo step: the new text goes through setText(), which with
// qcodeedit-diff changes only the trimmed lines. The cursor's line is left
// alone, so that typing on after a space goes on as typed.
void TextFile::trimTrailingWhitespace() {
    if (m_trim == TrimWhitespace::Off || !lineDiff()) return;
    QStringList lines = m_document->toPlainText().split(QLatin1Char('\n'));
    QVector<bool> trim(lines.size(), m_trim == TrimWhitespace::AllLines);
    if (m_trim == TrimWhitespace::ChangedLines) {
        // Lines nobody touched keep their whitespace (no stray changes, e.g. in git).
        const QStringList saved = m_savedText.split(QLatin1Char('\n'));
        for (const LineChange& change : lineDiff()(saved, lines))
            for (int i = change.newStart; i < change.newStart + change.newCount && i < lines.size(); ++i)
                trim[i] = true;
    }
    const int cursorLine = m_area->cursorPosition().line;
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
    if (changed) m_document->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

} // namespace qce::encoding
