#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

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
    bool crlf = false;         ///< lines end with "\r\n"
    bool finalNewline = false; ///< the text ends with a line break
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

/// Decodes file bytes. With an empty `encoding` it is detected (see detect()).
DecodeResult decode(const QByteArray& bytes, const QString& encoding = {});

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
/// final line break) for saving in `format`, restoring CRLF, BOM and the
/// final line break. Fails, listing the characters, when the encoding cannot
/// store some of them; with `replaceUnrepresentable` writes '?' for each.
EncodeResult encode(const QString& text, const FileFormat& format,
                    bool replaceUnrepresentable = false);

/// Exact conversion for applications that keep line breaks themselves (e.g.
/// a diff tool preserving mixed LF/CRLF): the text holds every character of
/// the file, "\r" included, only a BOM matching the encoding is removed
/// (format.bom). format.crlf and format.finalNewline describe the content.
DecodeResult decodeExact(const QByteArray& bytes, const QString& encoding = {});

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

/// Detection data: cpg's languages.txt and the directory of its .ngram
/// models. Defaults to the cpg data found when qcodeedit was built. Without
/// models only Unicode, ASCII and the fallback are recognised.
void setDetectionData(const QString& languagesFile, const QString& modelsDir);

} // namespace qce::encoding
