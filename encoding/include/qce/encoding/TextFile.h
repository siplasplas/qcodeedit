#pragma once

#include <memory>

#include <qce/encoding/Encoding.h>

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

namespace qce {
class CodeEditArea;
class SimpleTextDocument;
}

namespace qce::encoding {

class EncodingGuard;

/// The file behind an editor: reading it in its encoding (detected or
/// given), saving it back the same way, telling whether it differs from the
/// file and whether the file changed on disk.
///
/// Shared by editors built on qcodeedit (qceditor's tabs, the siplasplas
/// editor plugin), so that they read and write files alike. It has no user
/// interface of its own: the editor shows what it reports (e.g. an uncertain
/// encoding) and asks the user. It installs an EncodingGuard on the area,
/// whose question about characters a code page cannot store is the only one
/// it may bring up.
///
/// With qcodeedit-diff linked, a reload changes only the lines that differ,
/// as one undo step, and trailing whitespace can be removed on saving.
class TextFile : public QObject {
    Q_OBJECT
public:
    enum class Result {
        Ok,
        Failed,    ///< see errorString()
        Cancelled  ///< by the user, e.g. in a filter's question; nothing to report
    };

    /// Spaces and tabs at the ends of lines removed when saving, as one undo
    /// step, never in the line with the cursor. Needs qcodeedit-diff.
    enum class TrimWhitespace { Off, ChangedLines, AllLines };

    /// `document` is shown in `area`; neither is owned.
    TextFile(CodeEditArea* area, SimpleTextDocument* document, QObject* parent = nullptr);
    ~TextFile() override;

    /// The guard keeping typed text storable (e.g. for its choice handler).
    EncodingGuard* guard() const;

    /// Empty for a document never opened or saved.
    QString path() const;
    /// Encoding, BOM and line breaks the file is saved with.
    const FileFormat& format() const;
    QString encoding() const { return format().encoding; }
    /// Saves from now on in `encoding`; the text stays. Makes the document
    /// modified (not an undo step).
    void setSaveEncoding(const QString& encoding);

    /// Differs from the file: edited (undoing back to the saved text is not a
    /// change) or with another encoding to save in.
    bool isModified() const;

    /// Languages of files not in UTF-8 (cpg keys, e.g. "pl", "cz"): only
    /// their code pages are considered when detecting; empty = all.
    void setLegacyLanguages(const QStringList& languages);
    QStringList legacyLanguages() const;

    void setTrimWhitespace(TrimWhitespace mode);
    TrimWhitespace trimWhitespace() const;

    /// Changes the bytes read from or written to `path`, e.g. decrypts and
    /// encrypts. A filter returning Failed or Cancelled stops the reading or
    /// saving; Failed should set the message through setErrorString().
    using Filter = std::function<Result(const QString& path, QByteArray* bytes)>;
    void setReadFilter(Filter filter);
    void setWriteFilter(Filter filter);
    void setErrorString(const QString& error);
    QString errorString() const;

    /// Opens a file with a new undo history and the cursor at the start; an
    /// empty `encoding` detects it. A path that does not exist yet gives an
    /// empty UTF-8 document to be saved there.
    Result open(const QString& path, const QString& encoding = {});
    /// Reads the file again as `encoding`, with a new undo history (undoing
    /// would bring back text decoded in the other encoding).
    Result reopen(const QString& encoding);
    /// Reads the file again in its encoding (detected again when it no longer
    /// fits). With qcodeedit-diff only the changed lines change, as one undo
    /// step, so the text before the reload can be brought back.
    Result reload();

    Result save();
    /// Saves to `path` (atomically where possible) in the format; the file
    /// becomes `path`.
    Result saveAs(const QString& path);

    /// The file was changed on disk since it was read or saved.
    bool hasExternalChanges() const;
    /// Takes the file as it is on disk now as the known version.
    void ignoreExternalChanges() { updateDiskStamp(); }

    /// The alternatives when the encoding was detected without confidence on
    /// opening (best first; the file is shown in the first); empty when
    /// detection was confident or the encoding was given.
    const EncodingDetection& uncertainDetection() const;

    /// The text as last read or saved (lines joined by "\n").
    QString savedText() const;

signals:
    /// isModified() changed.
    void modificationChanged(bool modified);
    /// The file was read (opened, reopened or reloaded); `content` is what it
    /// holds after the read filter, e.g. for language detection.
    void loaded(const QByteArray& content);
    /// The path changed (opened another file, or saved under another name).
    void pathChanged(const QString& path, const QString& previousPath);
    /// Saved to `path`, which was `previousPath` before (may be equal or empty).
    void saved(const QString& path, const QString& previousPath);
    /// Encoding or line breaks changed, e.g. for a status bar.
    void formatChanged();

private:
    Result read(const QString& path, const QString& encoding, bool asEdit);
    void setPath(const QString& path);
    void setFormat(const FileFormat& format);
    void trimTrailingWhitespace();
    void updateDiskStamp();
    void updateModified();

    // The state, in TextFile.cpp: fields added there keep the
    // binary interface (the size of this class does not change).
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace qce::encoding
