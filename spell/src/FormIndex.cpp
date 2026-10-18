#include "FormIndex.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace qce::spell::detail {

namespace {

// --- Letters ---------------------------------------------------------------

// Polish diacritics, and the AltGr level of the Polish (programmer) keyboard
// with the key's letter (xkb "pl" and "latin": π on q, œ on w, © on r, ß on t,
// ð on d, æ on f, ŋ on g, ə on j, þ on p, ø on o, µ on m, …).
const QHash<char16_t, char16_t>& foldTable() {
    static const QHash<char16_t, char16_t> table = [] {
        QHash<char16_t, char16_t> t;
        const auto add = [&t](const char16_t* from, char16_t to) {
            for (; *from; ++from) t.insert(*from, to);
        };
        add(u"ąĄ", u'a'); add(u"ćĆ©¢", u'c'); add(u"ęĘ€", u'e'); add(u"łŁ", u'l'); add(u"ńŃ", u'n');
        add(u"óÓøØ", u'o'); add(u"śŚ", u's'); add(u"źŹżŻ«»", u'z');
        add(u"πΩ@", u'q'); add(u"œŒſ§", u'w'); add(u"®¶", u'r'); add(u"ß™ŧŦ", u't');
        add(u"ðÐđĐ", u'd'); add(u"æÆ", u'f'); add(u"ŋŊ", u'g'); add(u"əƏ", u'j'); add(u"þÞ", u'p');
        add(u"µ", u'm'); add(u"ħĦ", u'h'); add(u"ĸ", u'k');
        return t;
    }();
    return table;
}

// --- Affix rules -----------------------------------------------------------

struct Condition {
    // Each element: a set of characters, negated or not; "." is a negated empty set.
    std::vector<std::pair<bool, QString>> parts;
    bool any = true;

    static Condition parse(const QString& text) {
        Condition c;
        if (text == QLatin1String(".")) return c;
        c.any = false;
        for (int i = 0; i < text.size();) {
            if (text[i] == QLatin1Char('[')) {
                const int end = text.indexOf(QLatin1Char(']'), i);
                if (end < 0) break;
                const bool negated = i + 1 < text.size() && text[i + 1] == QLatin1Char('^');
                const int from = i + 1 + (negated ? 1 : 0);
                c.parts.push_back({negated, text.mid(from, end - from)});
                i = end + 1;
            } else if (text[i] == QLatin1Char('.')) {
                c.parts.push_back({true, QString()});
                ++i;
            } else {
                c.parts.push_back({false, QString(text[i])});
                ++i;
            }
        }
        return c;
    }
    bool matchesAt(const QString& word, int start) const {
        if (any) return true;
        if (start < 0 || start + int(parts.size()) > word.size()) return false;
        for (size_t k = 0; k < parts.size(); ++k) {
            const bool in = parts[k].second.contains(word[start + int(k)]);
            if (parts[k].first ? in : !in) return false;
        }
        return true;
    }
};

struct Rule {
    QString strip;
    QString add;
    Condition condition;
};

struct Affix {
    bool cross = false;
    std::vector<Rule> rules;
};

// Hunspell's names for encodings, as Qt knows them.
QString qtEncoding(const QString& name) {
    if (name.compare(QLatin1String("microsoft-cp1251"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("windows-1251");
    return name.isEmpty() ? QStringLiteral("ISO-8859-1") : name;
}

QStringList readLines(const QString& path, const QString& encoding) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QStringDecoder decoder(encoding.toLatin1().constData());
    return QString(decoder(file.readAll())).split(QLatin1Char('\n'));
}

QString encodingOf(const QString& affPath) {
    QFile file(affPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith("SET ")) return qtEncoding(QString::fromLatin1(line.mid(4).trimmed()));
    }
    return qtEncoding({});
}

// --- Cache file ------------------------------------------------------------

constexpr char kMagic[8] = {'Q', 'C', 'E', 'F', 'O', 'R', 'M', '1'};

#pragma pack(push, 1)
struct Header {
    char magic[8];
    quint32 formCount;
    quint32 classCount;
    quint64 dicSize;
    qint64 dicTime;
    quint64 affSize;
    qint64 affTime;
    quint64 formOffsets;    // (formCount + 1) quint32 into the form text
    quint64 formText;       // UTF-8, forms ordered by class
    quint64 classOffsets;   // (classCount + 1) quint32 into the class text
    quint64 classText;      // UTF-8 class keys, sorted by their bytes
    quint64 classFirst;     // (classCount + 1) quint32: first form of each class
    quint64 alphabet;       // UTF-8: every letter of the class keys
    quint32 alphabetSize;   // bytes
};
#pragma pack(pop)

int compareBytes(const char* a, int aSize, const char* b, int bSize) {
    const int common = std::min(aSize, bSize);
    if (const int c = std::memcmp(a, b, size_t(common))) return c;
    return aSize - bSize;
}

} // namespace

QChar foldChar(QChar c) {
    const auto it = foldTable().constFind(c.unicode());
    if (it != foldTable().constEnd()) return QChar(it.value());
    return c.toLower();
}

QString classKey(const QString& word) {
    QString key;
    key.reserve(word.size());
    for (const QChar c : word)
        if (c.isLetter()) key.append(foldChar(c));
    std::sort(key.begin(), key.end());
    return key;
}

double distance(const QString& a, const QString& b, double limit) {
    const int n = int(a.size()), m = int(b.size());
    if (std::abs(n - m) > limit) return limit + 1;
    const size_t width = size_t(m + 1);
    std::vector<double> dist(size_t(n + 1) * width);
    // Cells reached by moving a letter: a move right after another costs more.
    std::vector<char> moved(dist.size(), 0);
    const auto at = [width](int i, int j) { return size_t(i) * width + size_t(j); };
    for (int j = 0; j <= m; ++j) dist[at(0, j)] = j * kInsertCost;
    QString la = a.toLower(), lb = b.toLower();
    // A typed letter with a diacritic or from the AltGr level (not ASCII).
    const auto marked = [](QChar c) { return c.unicode() > 0x7F; };
    // The last k + 1 letters of both words the same but for one letter typed
    // k places too early or too late ("ał" as "ła": k = 1; "ała" as "łaa": 2).
    const auto isMove = [&la, &lb](int i, int j, int k) {
        const QStringView typed = QStringView(la).mid(i - k - 1, k + 1);
        const QStringView meant = QStringView(lb).mid(j - k - 1, k + 1);
        if (typed == meant) return false;
        return (typed.front() == meant.back() && typed.mid(1) == meant.first(k))
            || (typed.back() == meant.front() && typed.first(k) == meant.mid(1));
    };
    for (int i = 1; i <= n; ++i) {
        dist[at(i, 0)] = i * kDeleteCost;
        double rowMin = dist[at(i, 0)];
        for (int j = 1; j <= m; ++j) {
            const QChar x = la[i - 1], y = lb[j - 1];
            double change = 1.5;
            if (x == y) {
                change = 0;
            } else if (foldChar(x) == foldChar(y)) {
                // A missing diacritic is cheap; one typed by mistake (AltGr
                // pressed on purpose) costs more, unless AltGr was still held
                // from the letter before ("żę" for "że").
                const bool removesTyped = marked(x) && !marked(y);
                change = removesTyped && !(i > 1 && marked(la[i - 2])) ? 0.6 : 0.3;
            }
            double v = std::min({dist[at(i - 1, j)] + kDeleteCost, dist[at(i, j - 1)] + kInsertCost,
                                 dist[at(i - 1, j - 1)] + change});
            for (int k = 1; k < int(std::size(kMoveCost)); ++k) {
                if (i <= k || j <= k || !isMove(i, j, k)) continue;
                const size_t from = at(i - k - 1, j - k - 1);
                const double w = dist[from] + kMoveCost[k] + (moved[from] ? kMoveAgainCost : 0);
                if (w < v) {
                    v = w;
                    moved[at(i, j)] = 1;
                }
            }
            dist[at(i, j)] = v;
            rowMin = std::min(rowMin, v);
        }
        if (rowMin > limit) return limit + 1;
    }
    return dist[at(n, m)];
}

double score(const QString& word, const QString& candidate, double limit) {
    double d = distance(word, candidate, limit);
    // A split into two words, when the word was one.
    if ((candidate.contains(QLatin1Char(' ')) && !word.contains(QLatin1Char(' ')))
            || (candidate.contains(QLatin1Char('-')) && !word.contains(QLatin1Char('-'))))
        d += 1.0;
    // A capital letter (e.g. a proper name) where the word has none.
    if (candidate != candidate.toLower() && word == word.toLower()) d += 0.4;
    return d;
}

QStringList expandDictionary(const QString& affPath, const QString& dicPath, bool* supported) {
    *supported = false;
    const QString encoding = encodingOf(affPath);
    QHash<QChar, Affix> suffixes, prefixes;
    QSet<QChar> skipFlags;  // NOSUGGEST, ONLYINCOMPOUND, FORBIDDENWORD
    for (const QString& rawLine : readLines(affPath, encoding)) {
        const QStringList t = rawLine.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.isEmpty()) continue;
        const QString& kind = t[0];
        if (kind == QLatin1String("FLAG") || kind == QLatin1String("AF") || kind == QLatin1String("NEEDAFFIX")
                || kind == QLatin1String("CIRCUMFIX") || kind == QLatin1String("COMPLEXPREFIXES")
                || kind == QLatin1String("PSEUDOROOT"))
            return {};
        if ((kind == QLatin1String("NOSUGGEST") || kind == QLatin1String("ONLYINCOMPOUND")
                || kind == QLatin1String("FORBIDDENWORD")) && t.size() > 1)
            skipFlags.insert(t[1][0]);
        if ((kind != QLatin1String("SFX") && kind != QLatin1String("PFX")) || t.size() < 4) continue;
        Affix& affix = (kind == QLatin1String("SFX") ? suffixes : prefixes)[t[1][0]];
        if (t.size() == 4) {
            affix.cross = t[2] == QLatin1String("Y");
            continue;
        }
        if (t[3].contains(QLatin1Char('/'))) return {};  // continuation classes
        Rule rule;
        rule.strip = t[2] == QLatin1String("0") ? QString() : t[2];
        rule.add = t[3] == QLatin1String("0") ? QString() : t[3];
        rule.condition = Condition::parse(t.size() > 4 ? t[4] : QStringLiteral("."));
        affix.rules.push_back(rule);
    }
    QStringList forms;
    const QStringList dic = readLines(dicPath, encoding);
    for (int lineNumber = 1; lineNumber < dic.size(); ++lineNumber) {  // the first line is a count
        const QString entry = dic[lineNumber].section(QLatin1Char('\t'), 0, 0).section(QLatin1Char(' '), 0, 0).trimmed();
        if (entry.isEmpty()) continue;
        const int slash = entry.indexOf(QLatin1Char('/'));
        const QString word = slash < 0 ? entry : entry.left(slash);
        const QString flags = slash < 0 ? QString() : entry.mid(slash + 1);
        if (std::any_of(flags.cbegin(), flags.cend(), [&](QChar f) { return skipFlags.contains(f); }))
            continue;
        forms.append(word);
        QStringList crossSuffixed;
        for (const QChar flag : flags) {
            const auto it = suffixes.constFind(flag);
            if (it == suffixes.constEnd()) continue;
            for (const Rule& rule : it->rules) {
                if (!word.endsWith(rule.strip)) continue;
                if (!rule.condition.matchesAt(word, int(word.size()) - int(rule.condition.parts.size()))) continue;
                const QString form = word.left(word.size() - rule.strip.size()) + rule.add;
                forms.append(form);
                if (it->cross) crossSuffixed.append(form);
            }
        }
        for (const QChar flag : flags) {
            const auto it = prefixes.constFind(flag);
            if (it == prefixes.constEnd()) continue;
            for (const Rule& rule : it->rules) {
                const auto apply = [&](const QString& base) {
                    if (!base.startsWith(rule.strip) || !rule.condition.matchesAt(base, 0)) return;
                    forms.append(rule.add + base.mid(rule.strip.size()));
                };
                apply(word);
                if (it->cross)
                    for (const QString& suffixed : crossSuffixed) apply(suffixed);
            }
        }
    }
    *supported = true;
    return forms;
}

QString formIndexPath(const QString& dictionaryName) {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(cache).filePath(QStringLiteral("siplasplas/spell/%1.forms").arg(dictionaryName));
}

bool buildFormIndex(const QString& affPath, const QString& dicPath, const QString& indexPath) {
    bool supported = false;
    QStringList forms = expandDictionary(affPath, dicPath, &supported);
    if (!supported || forms.isEmpty()) return false;
    std::sort(forms.begin(), forms.end());
    forms.erase(std::unique(forms.begin(), forms.end()), forms.end());

    // Compact text and offsets; the forms are then ordered by their class.
    std::string formBytes, keyBytes;
    std::vector<quint32> formStart, keyStart;
    formStart.reserve(size_t(forms.size()) + 1);
    keyStart.reserve(size_t(forms.size()) + 1);
    QSet<QChar> letters;
    for (const QString& form : forms) {
        const QString key = classKey(form);
        for (const QChar c : key) letters.insert(c);
        formStart.push_back(quint32(formBytes.size()));
        formBytes += form.toStdString();
        keyStart.push_back(quint32(keyBytes.size()));
        keyBytes += key.toStdString();
    }
    formStart.push_back(quint32(formBytes.size()));
    keyStart.push_back(quint32(keyBytes.size()));
    forms.clear();
    std::vector<quint32> order(formStart.size() - 1);
    for (quint32 i = 0; i < order.size(); ++i) order[i] = i;
    const auto keyOf = [&](quint32 i) { return std::make_pair(keyBytes.data() + keyStart[i], int(keyStart[i + 1] - keyStart[i])); };
    std::sort(order.begin(), order.end(), [&](quint32 a, quint32 b) {
        const auto ka = keyOf(a), kb = keyOf(b);
        return compareBytes(ka.first, ka.second, kb.first, kb.second) < 0;
    });

    std::string outForms, outClasses;
    std::vector<quint32> outFormOffsets, outClassOffsets, outClassFirst;
    outFormOffsets.reserve(order.size() + 1);
    for (quint32 index = 0; index < order.size(); ++index) {
        const quint32 i = order[index];
        const auto key = keyOf(i);
        if (outClassOffsets.empty()
                || compareBytes(key.first, key.second, outClasses.data() + outClassOffsets.back(),
                                int(outClasses.size() - outClassOffsets.back())) != 0) {
            outClassOffsets.push_back(quint32(outClasses.size()));
            outClasses.append(key.first, size_t(key.second));
            outClassFirst.push_back(index);
        }
        outFormOffsets.push_back(quint32(outForms.size()));
        outForms.append(formBytes.data() + formStart[i], formStart[i + 1] - formStart[i]);
    }
    outFormOffsets.push_back(quint32(outForms.size()));
    outClassOffsets.push_back(quint32(outClasses.size()));
    outClassFirst.push_back(quint32(order.size()));
    QString alphabet;
    for (const QChar c : letters) alphabet.append(c);
    std::sort(alphabet.begin(), alphabet.end());
    const std::string alphabetBytes = alphabet.toStdString();

    Header header;
    std::memcpy(header.magic, kMagic, sizeof kMagic);
    header.formCount = quint32(order.size());
    header.classCount = quint32(outClassFirst.size() - 1);
    const QFileInfo dic(dicPath), aff(affPath);
    header.dicSize = quint64(dic.size());
    header.dicTime = dic.lastModified().toMSecsSinceEpoch();
    header.affSize = quint64(aff.size());
    header.affTime = aff.lastModified().toMSecsSinceEpoch();
    quint64 pos = sizeof(Header);
    header.formOffsets = pos; pos += outFormOffsets.size() * sizeof(quint32);
    header.formText = pos; pos += outForms.size();
    header.classOffsets = pos; pos += outClassOffsets.size() * sizeof(quint32);
    header.classText = pos; pos += outClasses.size();
    header.classFirst = pos; pos += outClassFirst.size() * sizeof(quint32);
    header.alphabet = pos;
    header.alphabetSize = quint32(alphabetBytes.size());

    QDir().mkpath(QFileInfo(indexPath).absolutePath());
    QSaveFile file(indexPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto write = [&file](const void* data, size_t size) {
        return file.write(static_cast<const char*>(data), qint64(size)) == qint64(size);
    };
    const bool ok = write(&header, sizeof header)
        && write(outFormOffsets.data(), outFormOffsets.size() * sizeof(quint32))
        && write(outForms.data(), outForms.size())
        && write(outClassOffsets.data(), outClassOffsets.size() * sizeof(quint32))
        && write(outClasses.data(), outClasses.size())
        && write(outClassFirst.data(), outClassFirst.size() * sizeof(quint32))
        && write(alphabetBytes.data(), alphabetBytes.size());
    return ok && file.commit();
}

// --- Index -------------------------------------------------------------------

struct FormIndex::Private {
    QFile file;
    const uchar* data = nullptr;
    Header header{};
    QString alphabet;

    const quint32* formOffsets() const { return reinterpret_cast<const quint32*>(data + header.formOffsets); }
    const quint32* classOffsets() const { return reinterpret_cast<const quint32*>(data + header.classOffsets); }
    const quint32* classFirst() const { return reinterpret_cast<const quint32*>(data + header.classFirst); }
    QString form(quint32 i) const {
        const char* text = reinterpret_cast<const char*>(data + header.formText);
        return QString::fromUtf8(text + formOffsets()[i], int(formOffsets()[i + 1] - formOffsets()[i]));
    }
    // The class with this key: [first, end) forms; first == end when none.
    std::pair<quint32, quint32> findClass(const QByteArray& key) const {
        const char* text = reinterpret_cast<const char*>(data + header.classText);
        const quint32* offsets = classOffsets();
        quint32 low = 0, high = header.classCount;
        while (low < high) {
            const quint32 mid = low + (high - low) / 2;
            const int c = compareBytes(text + offsets[mid], int(offsets[mid + 1] - offsets[mid]),
                                       key.constData(), int(key.size()));
            if (c < 0) low = mid + 1;
            else if (c > 0) high = mid;
            else return {classFirst()[mid], classFirst()[mid + 1]};
        }
        return {0, 0};
    }
};

std::unique_ptr<FormIndex> FormIndex::open(const QString& indexPath, const QString& affPath,
                                           const QString& dicPath) {
    std::unique_ptr<FormIndex> index(new FormIndex);
    index->d = std::make_unique<Private>();
    Private& p = *index->d;
    p.file.setFileName(indexPath);
    if (!p.file.open(QIODevice::ReadOnly) || p.file.size() < qint64(sizeof(Header))) return nullptr;
    p.data = p.file.map(0, p.file.size());
    if (!p.data) return nullptr;
    std::memcpy(&p.header, p.data, sizeof(Header));
    const QFileInfo dic(dicPath), aff(affPath);
    if (std::memcmp(p.header.magic, kMagic, sizeof kMagic) != 0 || p.header.dicSize != quint64(dic.size())
            || p.header.dicTime != dic.lastModified().toMSecsSinceEpoch()
            || p.header.affSize != quint64(aff.size())
            || p.header.affTime != aff.lastModified().toMSecsSinceEpoch()
            || p.header.alphabet + p.header.alphabetSize > quint64(p.file.size()))
        return nullptr;
    p.alphabet = QString::fromUtf8(reinterpret_cast<const char*>(p.data + p.header.alphabet),
                                   int(p.header.alphabetSize));
    return index;
}

FormIndex::~FormIndex() = default;

int FormIndex::formCount() const {
    return int(d->header.formCount);
}

std::vector<std::pair<double, QString>> FormIndex::candidates(const QString& word, double limit) const {
    const QString key = classKey(word);
    if (key.isEmpty()) return {};
    // Neighbouring classes: up to three letters removed or added in all (a
    // short word: one).
    const int maxEdits = key.size() <= 2 ? 1 : 3;
    QSet<QString> probes{key};
    QList<QString> level{key};
    for (int edit = 0; edit < maxEdits; ++edit) {
        QList<QString> next;
        for (const QString& k : level) {
            // Remove one letter.
            for (int i = 0; i < k.size(); ++i) {
                if (i > 0 && k[i] == k[i - 1]) continue;
                QString shorter = k;
                shorter.remove(i, 1);
                if (!shorter.isEmpty() && !probes.contains(shorter)) { probes.insert(shorter); next.append(shorter); }
            }
            // Add one letter (in sorted place).
            for (const QChar c : d->alphabet) {
                QString longer = k;
                longer.insert(int(std::lower_bound(longer.begin(), longer.end(), c) - longer.begin()), c);
                if (!probes.contains(longer)) { probes.insert(longer); next.append(longer); }
            }
        }
        level = next;
    }
    std::vector<std::pair<double, QString>> result;
    for (const QString& probe : probes) {
        const auto [first, end] = d->findClass(probe.toUtf8());
        for (quint32 i = first; i < end; ++i) {
            QString form = d->form(i);
            const double dist = score(word, form, limit);
            if (dist <= limit) result.emplace_back(dist, std::move(form));
        }
    }
    return result;
}

QString frequencyCacheDir() {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(cache).filePath(QStringLiteral("siplasplas/spell/freq"));
}

QString frequencyPath(const QString& language) {
    const QString file = QStringLiteral("%1.lemma.freq").arg(language);
    const QString installed =
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("siplasplas/spell/freq/") + file);
    if (!installed.isEmpty()) return installed;
    const QString downloaded = QDir(frequencyCacheDir()).filePath(file);
    return QFileInfo::exists(downloaded) ? downloaded : QString();
}

FrequencyTable::FrequencyTable(const QString& path) {
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    double words = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1Char('#'))) {
            // "# 2460104 words in pl.txt, 2297785 known to pl_PL"
            static const QRegularExpression known(QStringLiteral("(\\d+) known to"));
            static const QRegularExpression all(QStringLiteral("^#\\s*(\\d+) words"));
            if (const auto m = known.match(line); m.hasMatch()) m_known = m.captured(1).toDouble();
            else if (const auto m = all.match(line); m.hasMatch()) words = m.captured(1).toDouble();
            continue;
        }
        const qsizetype tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0) continue;
        m_counts.insert(line.left(tab), line.mid(tab + 1).toUInt());
    }
    if (m_known <= 0) m_known = words;
    if (m_known <= 0) m_counts.clear();
}

double FrequencyTable::perMillion(const QString& stem) const {
    auto it = m_counts.constFind(stem);
    if (it == m_counts.cend()) it = m_counts.constFind(stem.toLower());
    return it == m_counts.cend() ? 0.0 : it.value() * 1e6 / m_known;
}

double frequencyPenalty(double perMillion) {
    const double f = std::max(perMillion, kVeryRarePerMillion);
    return f >= kCommonPerMillion ? 0.0 : kFrequencyWeight * std::log10(kCommonPerMillion / f);
}

QStringList rankSuggestions(const QString& word, std::vector<std::pair<double, QString>> candidates,
                            int maxCount, const QStringList& preferred, double closeToBest) {
    const auto rank = [&preferred](const QString& form) {
        const qsizetype i = preferred.indexOf(form);
        return i < 0 ? preferred.size() : i;
    };
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        const auto ra = rank(a.second), rb = rank(b.second);
        if (ra != rb) return ra < rb;
        if (a.second.size() != b.second.size()) return a.second.size() < b.second.size();
        return a.second < b.second;
    });
    QStringList result;
    if (candidates.empty()) return result;
    const bool allCaps = word.size() > 1 && word == word.toUpper();
    const bool capitalized = !allCaps && !word.isEmpty() && word[0].isUpper();
    for (const auto& [dist, candidate] : candidates) {
        // Close to the best only: usually one suggestion, not always maxCount.
        // (The small allowance keeps a candidate exactly kCloseToBest behind,
        // e.g. 0.3 + 0.6 summed as 0.9000001.)
        if (int(result.size()) >= std::min(maxCount, kMaxShown)
                || dist > candidates.front().first + closeToBest + 1e-9)
            break;
        QString form = candidate;
        if (allCaps) form = form.toUpper();
        else if (capitalized) form[0] = form[0].toUpper();
        // One of the forms differing only in case: the better one.
        if (!result.contains(form, Qt::CaseInsensitive)) result.append(form);
    }
    return result;
}

} // namespace qce::spell::detail
