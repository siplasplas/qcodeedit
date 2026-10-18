#include <qce/encoding/Encoding.h>

#include <cpg/Converter.h>
#include <cpg/CpManager.h>
#include <cpg/Detector.h>
#include <cpg/Language.h>
#include <cpg/NgramModel.h>

#include <QFileInfo>
#include <QLocale>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

#include <algorithm>
#include <exception>
#include <memory>
#include <mutex>

namespace qce::encoding {

namespace {

// cpg objects are expensive to build (all code page tables, the models), so
// they are shared and created on first use. Detector is not const-correct,
// hence the mutex around all cpg calls.
struct Cpg {
    std::mutex mutex;
    CpManager codepages;
    Converter converter{codepages};
    QString languagesFile = QStringLiteral(QCE_CPG_LANGUAGES_FILE);
    QString modelsDir = QStringLiteral(QCE_CPG_MODELS_DIR);
    QString fallback = QStringLiteral("iso-8859-1");
    bool detectionLoaded = false;
    std::unique_ptr<Languages> languages;
    std::unique_ptr<NgramModel> model;
    std::unique_ptr<Detector> detector;

    // Loads languages and models once; missing data leaves them null.
    void loadDetection() {
        if (detectionLoaded) return;
        detectionLoaded = true;
        if (!QFileInfo::exists(languagesFile)) return;
        try {
            languages = std::make_unique<Languages>();
            languages->readFromFile(languagesFile.toStdString());
            if (QFileInfo(modelsDir).isDir()) {
                model = std::make_unique<NgramModel>();
                model->loadDirectory(modelsDir.toStdString());
            }
        } catch (const std::exception&) {
            model.reset();
        }
        detector = std::make_unique<Detector>(codepages, *languages, model.get());
    }
};

Cpg& cpg() {
    static Cpg instance;
    return instance;
}

std::string toStd(const QString& s) { return s.toStdString(); }

QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

bool isAscii(const QByteArray& bytes) {
    for (const char c : bytes)
        if (static_cast<unsigned char>(c) >= 0x80) return false;
    return true;
}

// Converts with the shared converter; caller holds the mutex.
ConversionResult toUtf8Locked(const QString& encoding, const QByteArray& bytes) {
    return cpg().converter.toUtf8(toStd(encoding),
                                  std::string_view(bytes.constData(), size_t(bytes.size())));
}

// Byte order mark of a UTF encoding; empty for code pages.
QByteArray bomFor(const QString& encoding) {
    const QString e = encoding.toLower();
    if (e == QLatin1String("utf8"))    return QByteArray("\xEF\xBB\xBF");
    if (e == QLatin1String("utf16"))   return QByteArray("\xFF\xFE");
    if (e == QLatin1String("utf16be")) return QByteArray("\xFE\xFF");
    if (e == QLatin1String("utf32"))   return QByteArray("\xFF\xFE\x00\x00", 4);
    if (e == QLatin1String("utf32be")) return QByteArray("\x00\x00\xFE\xFF", 4);
    return {};
}

QList<char32_t> uniqueIssues(const ConversionResult& r) {
    QList<char32_t> out;
    QSet<char32_t> seen;
    for (const ConversionIssue& issue : r.issues)
        if (issue.codepoint && !seen.contains(issue.codepoint)) {
            seen.insert(issue.codepoint);
            out.append(issue.codepoint);
        }
    return out;
}

} // namespace

bool isUnicode(const QString& encoding) {
    return encoding.startsWith(QLatin1String("utf"), Qt::CaseInsensitive);
}

QStringList availableEncodings() {
    std::lock_guard lock(cpg().mutex);
    QStringList names;
    for (const std::string& name : cpg().codepages.listAll())
        names << QString::fromStdString(name);
    return names;
}

QString fallbackEncoding() {
    std::lock_guard lock(cpg().mutex);
    return cpg().fallback;
}

void setFallbackEncoding(const QString& encoding) {
    std::lock_guard lock(cpg().mutex);
    cpg().fallback = encoding;
}

void preloadDetectionData() {
    QThreadPool::globalInstance()->start([] {
        std::lock_guard lock(cpg().mutex);
        cpg().loadDetection();
    });
}

void setDetectionData(const QString& languagesFile, const QString& modelsDir) {
    std::lock_guard lock(cpg().mutex);
    Cpg& c = cpg();
    c.languagesFile = languagesFile;
    c.modelsDir = modelsDir;
    c.detectionLoaded = false;
    c.detector.reset();
    c.model.reset();
    c.languages.reset();
}

EncodingDetection detectEncoding(const QByteArray& bytes, const QStringList& languages,
                                 double threshold) {
    EncodingDetection result;
    if (bytes.isEmpty() || isAscii(bytes)) {
        result.options.append({QStringLiteral("utf8"), QString(), 1.0});
        result.confident = true;
        return result;
    }

    std::lock_guard lock(cpg().mutex);
    Cpg& c = cpg();
    c.loadDetection();
    const std::string_view view(bytes.constData(), size_t(bytes.size()));
    auto option = [&c](const DetectionResult& r) {
        return EncodingOption{QString::fromStdString(r.codepage), QString::fromStdString(r.language),
                              r.confidence};
    };

    if (c.detector && c.model) {
        std::vector<DetectionResult> candidates;
        try {
            if (languages.isEmpty()) {
                candidates = c.detector->detectCodepage(view);
            } else if (languages.size() == 1) {
                candidates = c.detector->detectCodepage(toStd(languages.front()), view);
            } else {
                std::vector<std::string> isos;
                for (const QString& language : languages) isos.push_back(toStd(language));
                candidates = c.detector->detectCodepageAmong(isos, view);
            }
        } catch (const std::exception&) {
            candidates.clear();
        }
        // UTF recognised by ICU (e.g. with a BOM) comes without a calibrated
        // confidence; it is certain.
        if (!candidates.empty() && candidates.front().confidence <= 0
                && isUnicode(QString::fromStdString(candidates.front().codepage))
                && toUtf8Locked(QString::fromStdString(candidates.front().codepage), bytes).success) {
            EncodingOption utf = option(candidates.front());
            utf.confidence = 1.0;
            result.options.append(utf);
            result.confident = true;
            return result;
        }
        // cpg guarantees that its options decode the input; checked anyway.
        for (const DetectionResult& r : Detector::plausible(candidates, threshold)) {
            if (toUtf8Locked(QString::fromStdString(r.codepage), bytes).success)
                result.options.append(option(r));
        }
        if (!result.options.isEmpty()) {
            result.confident = result.options.size() == 1
                && result.options.front().confidence >= threshold;
            return result;
        }
    }

    // Without models (or with nothing decodable): the first code page that
    // decodes the input, by alphabet coverage for the first language or the
    // system language, else UTF-8, else the fallback. Without models there
    // is no evidence to ask about, so it counts as confident.
    if (c.detector) {
        std::vector<DetectionResult> candidates;
        try {
            const QString language = languages.isEmpty()
                ? QLocale::system().name().section(QLatin1Char('_'), 0, 0) : languages.front();
            candidates = c.detector->detectCodepage(toStd(language), view);
        } catch (const std::exception&) {
            candidates.clear();
        }
        for (const DetectionResult& r : candidates) {
            if (toUtf8Locked(QString::fromStdString(r.codepage), bytes).success) {
                result.options.append({QString::fromStdString(r.codepage), QString(), 0});
                break;
            }
        }
    }
    if (result.options.isEmpty()) {
        const bool utf8 = toUtf8Locked(QStringLiteral("utf8"), bytes).success;
        result.options.append({utf8 ? QStringLiteral("utf8") : c.fallback, QString(), 0});
    }
    result.confident = !c.model;
    return result;
}

QList<EncodingPreviewLine> previewEncodings(const QByteArray& bytes,
                                            const QList<EncodingOption>& options, int maxLines) {
    QList<EncodingPreviewLine> result;
    if (options.size() < 2 || maxLines <= 0) return result;
    std::vector<DetectionResult> cpgOptions;
    for (const EncodingOption& option : options) {
        DetectionResult r;
        r.codepage = toStd(option.encoding);  // preview() reads only the code page
        cpgOptions.push_back(r);
    }
    std::lock_guard lock(cpg().mutex);
    Cpg& c = cpg();
    c.loadDetection();
    if (!c.detector) return result;
    try {
        const auto lines = c.detector->preview(std::string_view(bytes.constData(), size_t(bytes.size())),
                                               cpgOptions, size_t(maxLines));
        for (const PreviewLine& line : lines) {
            EncodingPreviewLine out;
            out.byteOffset = qsizetype(line.byteOffset);
            for (const std::string& variant : line.variants) out.variants << fromUtf8(variant);
            result.append(out);
        }
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

QString detect(const QByteArray& bytes, const QStringList& languages) {
    return detectEncoding(bytes, languages).options.front().encoding;
}

QString detect(const QByteArray& bytes, const QString& language) {
    return detect(bytes, language.isEmpty() ? QStringList() : QStringList{language});
}

DecodeResult decodeExact(const QByteArray& bytes, const QString& encoding,
                         const QString& language) {
    return decodeExact(bytes, encoding, language.isEmpty() ? QStringList() : QStringList{language});
}

DecodeResult decodeExact(const QByteArray& bytes, const QString& encoding,
                         const QStringList& languages) {
    DecodeResult result;
    if (encoding.isEmpty()) {
        result.detection = detectEncoding(bytes, languages);
        result.format.encoding = result.detection.options.front().encoding;
    } else {
        result.format.encoding = encoding;
    }

    // A BOM is cut off here and remembered; ICU would drop it silently.
    const QByteArray bom = bomFor(result.format.encoding);
    result.format.bom = !bom.isEmpty() && bytes.startsWith(bom);
    const QByteArray body = result.format.bom ? bytes.mid(bom.size()) : bytes;

    ConversionResult converted;
    {
        std::lock_guard lock(cpg().mutex);
        converted = toUtf8Locked(result.format.encoding, body);
    }
    if (!converted.success) {
        result.error = converted.error == ConversionError::UnknownCodepage
            ? QStringLiteral("Unknown encoding: %1").arg(result.format.encoding)
            : QStringLiteral("The file is not valid %1 (byte %2)")
                  .arg(result.format.encoding)
                  .arg(converted.issues.empty() ? 0 : converted.issues.front().byteOffset);
        return result;
    }

    result.text = fromUtf8(converted.output);
    // Count each kind of line break; the most frequent one is written back.
    qsizetype crlf = 0, cr = 0, lf = 0;
    const QString& t = result.text;
    for (qsizetype i = 0; i < t.size(); ++i) {
        if (t[i] == QLatin1Char('\r')) {
            if (i + 1 < t.size() && t[i + 1] == QLatin1Char('\n')) { ++crlf; ++i; }
            else ++cr;
        } else if (t[i] == QLatin1Char('\n')) {
            ++lf;
        }
    }
    result.format.crlf = crlf > 0 && crlf >= lf && crlf >= cr;
    result.format.cr = !result.format.crlf && cr > lf;
    result.format.mixedLineBreaks = (crlf > 0) + (cr > 0) + (lf > 0) > 1;
    result.format.finalNewline = t.endsWith(QLatin1Char('\n')) || t.endsWith(QLatin1Char('\r'));
    result.ok = true;
    return result;
}

DecodeResult decode(const QByteArray& bytes, const QString& encoding,
                    const QString& language) {
    return decode(bytes, encoding, language.isEmpty() ? QStringList() : QStringList{language});
}

DecodeResult decode(const QByteArray& bytes, const QString& encoding,
                    const QStringList& languages) {
    DecodeResult result = decodeExact(bytes, encoding, languages);
    // "\n" line breaks only. The final line break stays in the text:
    // SimpleTextDocument::setText() drops it, and encode() adds it back.
    if (result.ok) {
        result.text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        result.text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    }
    return result;
}

QList<Language> availableLanguages() {
    std::lock_guard lock(cpg().mutex);
    Cpg& c = cpg();
    c.loadDetection();
    QList<Language> result;
    if (!c.languages) return result;
    for (const auto& [code, name] : c.languages->listAll())
        result.append({QString::fromStdString(code), QString::fromStdString(name)});
    std::sort(result.begin(), result.end(), [](const Language& a, const Language& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return result;
}

Language detectLanguage(const QByteArray& bytes) {
    Language result;
    if (bytes.isEmpty()) return result;
    std::lock_guard lock(cpg().mutex);
    Cpg& c = cpg();
    c.loadDetection();
    if (!c.detector || !c.model) return result;
    try {
        const auto candidates = c.detector->detectCodepage(
            std::string_view(bytes.constData(), size_t(bytes.size())));
        for (const DetectionResult& candidate : candidates) {
            if (candidate.language.empty()) continue;
            result.code = QString::fromStdString(candidate.language);
            if (const auto* language = c.languages->getByIsoCode(candidate.language))
                result.name = QString::fromStdString(language->name);
            break;
        }
    } catch (const std::exception&) {
        result = {};
    }
    return result;
}

void detectLanguageAsync(const QByteArray& bytes, QObject* context,
                         std::function<void(const Language&)> done) {
    QPointer<QObject> receiver(context);
    QThreadPool::globalInstance()->start([bytes, receiver, done = std::move(done)]() {
        const Language language = detectLanguage(bytes);
        if (!receiver) return;
        QMetaObject::invokeMethod(receiver, [receiver, done, language]() {
            if (receiver) done(language);
        }, Qt::QueuedConnection);
    });
}

EncodeResult encodeExact(const QString& text, const QString& encoding, bool bom, bool replace) {
    ConversionResult converted;
    {
        std::lock_guard lock(cpg().mutex);
        converted = cpg().converter.fromUtf8(
            toStd(encoding), text.toUtf8().toStdString(),
            replace ? UnmappablePolicy::Replace : UnmappablePolicy::Reject);
    }
    EncodeResult result;
    result.unrepresentable = uniqueIssues(converted);
    result.ok = converted.success;
    if (converted.success) {
        result.bytes = QByteArray(converted.output.data(), qsizetype(converted.output.size()));
        if (bom) result.bytes.prepend(bomFor(encoding));
    }
    return result;
}

EncodeResult encode(const QString& text, const FileFormat& format, bool replace) {
    QString full = text;
    if (format.finalNewline) full += QLatin1Char('\n');
    if (format.cr) full.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    else if (format.crlf) full.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    return encodeExact(full, format.encoding, format.bom, replace);
}

QList<char32_t> unrepresentable(const QString& text, const QString& encoding) {
    if (text.isEmpty() || isUnicode(encoding)) return {};
    ConversionResult converted;
    {
        std::lock_guard lock(cpg().mutex);
        converted = cpg().converter.fromUtf8(toStd(encoding), text.toUtf8().toStdString());
    }
    return uniqueIssues(converted);
}

QString replaceUnrepresentable(const QString& text, const QString& encoding) {
    const QList<char32_t> bad = unrepresentable(text, encoding);
    if (bad.isEmpty()) return text;
    const QSet<char32_t> badSet(bad.begin(), bad.end());
    QString out;
    out.reserve(text.size());
    for (const char32_t cp : text.toUcs4()) {
        if (badSet.contains(cp)) out += QLatin1Char('?');
        else out += QString::fromUcs4(&cp, 1);
    }
    return out;
}

} // namespace qce::encoding
