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

/// One way to decode a file, as detection proposes it.
struct EncodingOption {
    QString encoding;      ///< cpg name: cp1250, utf8, ...
    QString language;      ///< cpg language key ("pl", "cz", ...); may be empty
    /// Calibrated probability that this decoding is right (0..1). Encodings
    /// that decode the file identically share it (cp1250 and iso-8859-2
    /// without the bytes where they differ). 0 when unknown (no models).
    double confidence = 0;
};

/// The plausible decodings of a file, best first.
struct EncodingDetection {
    /// The confident winner alone, or the smallest set of distinct decodings
    /// whose confidence adds up to the threshold. Never empty from
    /// detectEncoding().
    QList<EncodingOption> options;
    /// One option whose confidence reaches the threshold: the file can be
    /// opened (or converted) without asking. Otherwise an editor opens it
    /// with options.front() and lets the user choose; a batch tool refuses.
    bool confident = false;
};

/// Detects the encoding of `bytes` with the alternatives worth considering.
///
/// - Empty input and pure ASCII: one option "utf8", confident (every
///   encoding gives the same text).
/// - Valid UTF-8 with at least 8 multi-byte sequences: one option "utf8",
///   confident, without consulting the models (legacy text practically
///   never forms valid UTF-8, and the statistics are the slow part).
/// - UTF with a BOM is recognised; other valid UTF-8 competes with the
///   legacy code pages, since a few multi-byte sequences are little
///   evidence.
/// - Legacy code pages are ranked by cpg's language models over all
///   languages, or only `languages` when given (e.g. {"pl", "cz"}).
/// - Every option decodes the whole input.
/// - Without detection models there is no evidence to ask the user about:
///   the first code page that decodes the input (by alphabet coverage for
///   the first of `languages`, or the system language), else "utf8", else
///   fallbackEncoding(), is reported as one confident option with
///   confidence 0.
///
/// `threshold` is the calibrated probability a winner needs to be confident;
/// 0.95 is cpg's default (about 99.9 % of confident winners are right).
EncodingDetection detectEncoding(const QByteArray& bytes, const QStringList& languages = {},
                                 double threshold = 0.95);

/// A piece of the file shown in every option, for a person to choose.
struct EncodingPreviewLine {
    qsizetype byteOffset = 0; ///< start of the piece in the file
    QStringList variants;     ///< the piece decoded by each option, in option order
};

/// Up to `maxLines` single-line pieces (about 80 bytes; "…" marks a cut line)
/// where `options` decode the file differently, for a choice dialog. Lines
/// with the most differing bytes come first, then in file order; keep that
/// order: the lines that speak most against the best option are often
/// unrepresentative (a fragment in another encoding) and would mislead.
/// Undefined bytes and control characters show as U+FFFD. Empty for fewer
/// than two options; single-byte code pages and UTF-8 only.
QList<EncodingPreviewLine> previewEncodings(const QByteArray& bytes,
                                            const QList<EncodingOption>& options, int maxLines = 5);

struct DecodeResult {
    bool ok = false;
    /// The content with "\n" line breaks and without BOM, for
    /// SimpleTextDocument::setText(). A final line break is still there
    /// (setText() drops it; encode() adds it back to toPlainText()).
    QString text;
    FileFormat format;
    QString error;   ///< set when !ok
    /// How the encoding was chosen when it was detected (format.encoding is
    /// detection.options.front()); empty options when it was given.
    EncodingDetection detection;
};

/// Decodes file bytes. With an empty `encoding` it is detected (see
/// detectEncoding()), legacy code pages only among those of `language` when
/// given (e.g. "pl": cp1250, iso-8859-2, cp852, ...), which is faster and
/// avoids guessing a wrong language from a few words. UTF is recognised
/// regardless. LF, CRLF and CR line breaks all become "\n".
DecodeResult decode(const QByteArray& bytes, const QString& encoding = {},
                    const QString& language = {});
/// decode() with several languages (e.g. {"pl", "cz"}); empty = all.
DecodeResult decode(const QByteArray& bytes, const QString& encoding,
                    const QStringList& languages);

/// Natural language of a text, by cpg's language models.
struct Language {
    QString code; ///< cpg language key, e.g. "pl", "en", "cz"; empty when unknown
    QString name; ///< e.g. "Polish"
    /// Calibrated probability of the language (0..1); 0 when unknown, and in
    /// availableLanguages().
    double confidence = 0;
};

/// Languages cpg knows (code and name), sorted by name; empty without its
/// detection data. For letting the user pick the language of legacy files.
QList<Language> availableLanguages();

/// The display name of a cpg language key ("pl" -> "Polish"); empty when
/// unknown or without detection data.
QString languageName(const QString& code);

/// Guesses the language of file bytes (any encoding). Takes some
/// milliseconds per 100 KB (plus loading the models once), so an editor
/// runs it with detectLanguageAsync() after showing the file. The language
/// is judged from the words around non-ASCII characters, so source code
/// with Polish comments gives "pl"; pure ASCII text gives what its words
/// suggest (often "en" for source code).
Language detectLanguage(const QByteArray& bytes);

/// Runs detectLanguage() in a worker thread and calls `done` in `context`'s
/// thread; nothing is called when `context` is gone by then.
void detectLanguageAsync(const QByteArray& bytes, QObject* context,
                         std::function<void(const Language&)> done);

/// The most likely encoding of `bytes`: detectEncoding(bytes, {language})
/// .options.front().encoding (all languages when `language` is empty).
QString detect(const QByteArray& bytes, const QString& language = {});
/// detect() with several languages; empty = all.
QString detect(const QByteArray& bytes, const QStringList& languages);

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
/// decodeExact() with several languages; empty = all.
DecodeResult decodeExact(const QByteArray& bytes, const QString& encoding,
                         const QStringList& languages);

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
