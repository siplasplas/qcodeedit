#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;

/// Text file encodings for qcodeedit, on top of the cpg library: legacy code
/// pages (cp1250, iso-8859-2, cp852, Mazovia, ...) and UTF-8/16/32.
///
/// The editor always works on Unicode text. A document read from a code page
/// remembers it in a FileFormat and is written back in it; characters the
/// code page cannot store are reported instead of being lost silently.
namespace qce::encoding {

/// How a text file is stored: everything needed to write it back as read.
struct FileFormat {
    QString encoding = QStringLiteral("utf8"); ///< cpg name: utf8, utf16, cp1250, iso-8859-2, ...
    bool bom = false;          ///< the file starts with a byte order mark
    bool crlf = false;         ///< lines end with "\r\n" (Windows)
    bool finalNewline = false; ///< the text ends with a line break
    bool cr = false;           ///< lines end with "\r" (classic Mac); wins over crlf
    /// The file had more than one kind of line break. decode() gives them all
    /// as "\n"; encode() writes the most frequent kind (crlf / cr / LF).
    bool mixedLineBreaks = false;
};

struct DecodeResult {
    bool ok = false;
    /// The content with "\n" line breaks and without BOM, for
    /// SimpleTextDocument::setText(). A final line break is still there
    /// (setText() drops it; encode() adds it back to toPlainText()).
    QString text;
    FileFormat format;
    QString error;   ///< set when !ok
};

/// Decodes file bytes. With an empty `encoding` it is detected (see detect()),
/// legacy code pages only among those of `language` when given (e.g. "pl":
/// cp1250, iso-8859-2, cp852, ...), which is faster and avoids guessing a
/// wrong language from a few words. UTF is recognised regardless.
/// LF, CRLF and CR line breaks all become "\n".
DecodeResult decode(const QByteArray& bytes, const QString& encoding = {},
                    const QString& language = {});

/// Natural language of a text, by cpg's language models.
struct Language {
    QString code; ///< cpg language key, e.g. "pl", "en", "cz"; empty when unknown
    QString name; ///< e.g. "Polish"
};

/// Languages cpg knows (code and name), sorted by name; empty without its
/// detection data. For letting the user pick the language of legacy files.
QList<Language> availableLanguages();

/// Guesses the language of file bytes (any encoding). Takes some
/// milliseconds per 100 KB (plus loading the models once), so an editor
/// runs it with detectLanguageAsync() after showing the file. Source code
/// mostly gives "en" (comments and identifiers).
Language detectLanguage(const QByteArray& bytes);

/// Runs detectLanguage() in a worker thread and calls `done` in `context`'s
/// thread; nothing is called when `context` is gone by then.
void detectLanguageAsync(const QByteArray& bytes, QObject* context,
                         std::function<void(const Language&)> done);

/// Detects the encoding of `bytes`. UTF-8/16/32 are recognised by ICU; legacy
/// code pages are ranked by cpg's language models, over all languages or
/// only `language` (e.g. "pl") when given. Pure ASCII and valid UTF-8 give
/// "utf8". Falls back to fallbackEncoding() when nothing fits.
QString detect(const QByteArray& bytes, const QString& language = {});

struct EncodeResult {
    bool ok = false;
    QByteArray bytes;
    /// Characters the encoding cannot store, each once, in order of appearance.
    QList<char32_t> unrepresentable;
};

/// Encodes the document text (SimpleTextDocument::toPlainText(), without a
/// final line break) for saving in `format`, restoring CRLF or CR, BOM and
/// the final line break. Fails, listing the characters, when the encoding cannot
/// store some of them; with `replaceUnrepresentable` writes '?' for each.
EncodeResult encode(const QString& text, const FileFormat& format,
                    bool replaceUnrepresentable = false);

/// Exact conversion for applications that keep line breaks themselves (e.g.
/// a diff tool preserving mixed LF/CRLF): the text holds every character of
/// the file, "\r" included, only a BOM matching the encoding is removed
/// (format.bom). The line-break fields of format describe the content.
DecodeResult decodeExact(const QByteArray& bytes, const QString& encoding = {},
                         const QString& language = {});

/// Counterpart of decodeExact(): `text` is written as it is, with a BOM in
/// front when `bom` is set. Unrepresentable characters as in encode().
EncodeResult encodeExact(const QString& text, const QString& encoding, bool bom = false,
                         bool replaceUnrepresentable = false);

/// Characters of `text` that `encoding` cannot store, each once, in order.
/// Always empty for UTF encodings.
QList<char32_t> unrepresentable(const QString& text, const QString& encoding);

/// `text` with every character `encoding` cannot store replaced by '?'.
QString replaceUnrepresentable(const QString& text, const QString& encoding);

/// True for utf8, utf16, utf16be, utf32 and utf32be.
bool isUnicode(const QString& encoding);

/// Names of all encodings cpg supports.
QStringList availableEncodings();

/// Encoding used when detection finds nothing; default "iso-8859-1", which
/// decodes any bytes and so never loses data.
QString fallbackEncoding();
void setFallbackEncoding(const QString& encoding);

/// Loads the detection data (about 25 ms) in a worker thread, so the first
/// file with non-ASCII text does not wait for it. Call once at startup;
/// detection started meanwhile waits for the loading to finish.
void preloadDetectionData();

/// Detection data: cpg's languages.txt and the directory of its .ngram
/// models. Defaults to the cpg data found when qcodeedit was built. Without
/// models only Unicode, ASCII and the fallback are recognised.
void setDetectionData(const QString& languagesFile, const QString& modelsDir);

} // namespace qce::encoding
