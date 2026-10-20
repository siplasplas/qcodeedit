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
#include <optional>
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
    // Continuation class: the flags of the form this rule makes (more
    // affixes it may take, CIRCUMFIX, NEEDAFFIX, ...).
    std::vector<quint32> cont;
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
    QString text = decoder(file.readAll());
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    return text.split(QLatin1Char('\n'));
}

QString encodingOf(const QString& affPath) {
    QFile file(affPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.startsWith("\xEF\xBB\xBF")) line.remove(0, 3);  // a byte order mark (pt_BR)
        if (line.startsWith("SET ")) return qtEncoding(QString::fromLatin1(line.mid(4).trimmed()));
    }
    return qtEncoding({});
}

// --- Cache file ------------------------------------------------------------

// '2': other accented letters fold to their base letter (classKey()).
constexpr char kMagic[8] = {'Q', 'C', 'E', 'F', 'O', 'R', 'M', '2'};

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
    // Other accented Latin letters: their base letter (é, è, ê → e; ç → c).
    // Looked up once for the Latin blocks, where the letters are.
    static const std::vector<char16_t> latin = [] {
        std::vector<char16_t> t(0x250);
        for (char16_t u = 0; u < t.size(); ++u) {
            const QChar letter(u);
            t[u] = letter.toLower().unicode();
            if (letter.decompositionTag() != QChar::Canonical) continue;
            const QChar base = letter.decomposition().front();
            if (base.unicode() < 0x80 && base.isLetter()) t[u] = base.toLower().unicode();
        }
        return t;
    }();
    if (c.unicode() < latin.size()) return QChar(latin[c.unicode()]);
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

namespace {

// The keys of the letters on a QWERTY keyboard: row, and column in key widths
// (the rows are staggered by a quarter and half a key).
struct KeyPosition {
    int row = -1;
    double column = 0;
};

KeyPosition keyPosition(QChar c) {
    static const char* const rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const double offsets[] = {0, 0.25, 0.75};
    const QChar key = foldChar(c);
    if (key.unicode() > 0x7F) return {};
    for (int row = 0; row < 3; ++row)
        if (const char* found = std::strchr(rows[row], char(key.unicode())); found && *found)
            return {row, offsets[row] + double(found - rows[row])};
    return {};
}

} // namespace

// foldChar() of each letter, in order.
QString foldWord(const QString& word) {
    QString folded = word;
    for (QChar& c : folded) c = foldChar(c);
    return folded;
}

int handOf(QChar c) {
    const KeyPosition key = keyPosition(c);
    if (key.row < 0) return -1;
    // Left hand: q–t, a–g, z–b; right hand the rest.
    static const double firstRight[] = {5, 5.25, 5.75};
    return key.column < firstRight[key.row] ? 0 : 1;
}

bool keysTouch(QChar a, QChar b) {
    const KeyPosition x = keyPosition(a), y = keyPosition(b);
    if (x.row < 0 || y.row < 0 || std::abs(x.row - y.row) > 1) return false;
    return std::abs(x.column - y.column) < (x.row == y.row ? 1.1 : 0.9);
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
    const QString fa = foldWord(la), fb = foldWord(lb);
    // The cost of the last k + 1 letters of both words being the same but for
    // one letter typed k places too early or too late ("ał" as "ła": k = 1;
    // "ała" as "łaa": 2), compared without diacritics, each missing one
    // adding its cost ("ci" as "ić"); negative when they are not.
    const auto moveCost = [&](int i, int j, int k) -> double {
        const QStringView typed = QStringView(fa).mid(i - k - 1, k + 1);
        const QStringView meant = QStringView(fb).mid(j - k - 1, k + 1);
        if (typed == meant) return -1;
        // `early`: the first typed letter belongs at the end.
        const bool early = typed.front() == meant.back() && typed.mid(1) == meant.first(k);
        const bool late = typed.back() == meant.front() && typed.first(k) == meant.mid(1);
        if (!early && !late) return -1;
        double cost = kMoveCost[k];
        if (k == 1) {
            const int h1 = handOf(la[i - 2]), h2 = handOf(la[i - 1]);
            cost = h1 >= 0 && h1 == h2 ? kSwapOneHandCost : kSwapTwoHandsCost;
        }
        for (int p = 0; p <= k; ++p) {
            const int t = i - k - 1 + p;
            const int q = early ? (p == 0 ? j - 1 : j - k - 1 + p - 1) : (p == k ? j - k - 1 : j - k - 1 + p + 1);
            if (la[t] != lb[q]) cost += 0.3;
        }
        return cost;
    };
    // A letter too many, typed between the meant letters lb[j - 1] and lb[j]:
    // cheaper when its key touches both (or repeats one), a little when it
    // touches one.
    const auto deleteCost = [&la, &lb, m](int i, int j) {
        const QChar x = la[i - 1];
        const bool before = j > 0 && keysTouch(x, lb[j - 1]);
        const bool after = j < m && keysTouch(x, lb[j]);
        const bool repeats = (j > 0 && foldChar(x) == foldChar(lb[j - 1])) || (j < m && foldChar(x) == foldChar(lb[j]));
        if (repeats || (before && after)) return kDeleteTouchingCost;
        return before || after ? kDeleteTouchingOneCost : kDeleteCost;
    };
    for (int i = 1; i <= n; ++i) {
        dist[at(i, 0)] = dist[at(i - 1, 0)] + deleteCost(i, 0);
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
            double v = std::min({dist[at(i - 1, j)] + deleteCost(i, j), dist[at(i, j - 1)] + kInsertCost,
                                 dist[at(i - 1, j - 1)] + change});
            for (int k = 1; k < int(std::size(kMoveCost)); ++k) {
                if (i <= k || j <= k) continue;
                const double cost = moveCost(i, j, k);
                if (cost < 0) continue;
                const size_t from = at(i - k - 1, j - k - 1);
                const double w = dist[from] + cost + (moved[from] ? kMoveAgainCost : 0);
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
    // Shift held over the first letters, maybe with AltGr ("ÞÓŁączyłęm" for
    // Połączyłem: P and O with AltGr too, and AltGr on after ł): one slip of
    // held keys, so a diacritic or AltGr character of the same key on those
    // letters, or right after another such letter, costs nothing of its own;
    // only the capitals do (below).
    QString typed = word;
    int leading = 0;
    while (leading < typed.size() && typed[leading].isUpper()) ++leading;
    if (leading >= 2 && leading < typed.size()) {
        for (int i = 0; i < typed.size() && i < candidate.size(); ++i) {
            const bool sameKey = typed[i].toLower() != candidate[i].toLower()
                && foldChar(typed[i]) == foldChar(candidate[i]);
            // In the held letters, and AltGr held on after them further on
            // (a diacritic right after another: "łę" for "łe").
            const bool held = i < leading
                || (typed[i].unicode() > 0x7F && typed[i - 1].unicode() > 0x7F);
            if (sameKey && held) typed[i] = candidate[i];
        }
    }
    double d = distance(typed, candidate, limit);
    // A split into two words, when the word was one.
    if ((candidate.contains(QLatin1Char(' ')) && !word.contains(QLatin1Char(' ')))
            || (candidate.contains(QLatin1Char('-')) && !word.contains(QLatin1Char('-'))))
        d += 1.0;
    // A capital letter (e.g. a proper name) where the word has none.
    if (candidate != candidate.toLower() && word == word.toLower()) d += 0.4;
    // A capital inside the word the candidate has not there: right after
    // the capitals the word begins with, Shift held a letter too long
    // ("POłączyłem", cheap); after a small letter rather camelCase written on
    // purpose ("PołąCzyłem", "PoŁączyłem": never sure).
    if (word.size() > 1 && word != word.toUpper()) {
        bool leadingCapitals = word[0].isUpper();
        for (int i = 1; i < word.size(); ++i) {
            const bool upper = word[i].isUpper();
            if (upper && !(i < candidate.size() && candidate[i].isUpper()))
                d += leadingCapitals ? kShiftHeldCost : kInnerCapitalCost;
            leadingCapitals = leadingCapitals && upper;
        }
    }
    return d;
}

namespace {

// How the .aff writes flags (FLAG): one character, two (long), numbers
// separated by commas (num), or one Unicode character (UTF-8).
enum class FlagKind { Char, Long, Num, Utf8 };

// More forms than this (over a gigabyte while building) are left to
// hunspell: e.g. Hungarian, whose suffixes follow each other by thousands.
constexpr qsizetype kMaxForms = 8'000'000;

// A flag as a number: the character, the two characters of a long flag, the
// number, or the code point.
using Flag = quint32;

// The flags of a word or an alias, into `flags` (cleared first).
void splitFlags(QStringView text, FlagKind kind, std::vector<Flag>& flags) {
    flags.clear();
    switch (kind) {
    case FlagKind::Char:
        for (const QChar c : text) flags.push_back(c.unicode());
        break;
    case FlagKind::Long:
        for (qsizetype i = 0; i + 1 < text.size(); i += 2) flags.push_back(Flag(text[i].unicode()) << 16 | text[i + 1].unicode());
        break;
    case FlagKind::Num:
        for (const QStringView number : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) flags.push_back(number.trimmed().toUInt());
        break;
    case FlagKind::Utf8:
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text[i].isHighSurrogate() && i + 1 < text.size()) {
                flags.push_back(QChar::surrogateToUcs4(text[i], text[i + 1]));
                ++i;
            } else {
                flags.push_back(text[i].unicode());
            }
        }
        break;
    }
}

Flag oneFlag(QStringView text, FlagKind kind) {
    std::vector<Flag> flags;
    splitFlags(text, kind, flags);
    return flags.empty() ? 0 : flags.front();
}

} // namespace

QStringList expandDictionary(const QString& affPath, const QString& dicPath, bool* supported) {
    *supported = false;
    const QString encoding = encodingOf(affPath);
    const QStringList affLines = readLines(affPath, encoding);
    // The kind of flags and their aliases first: affix rules use them too.
    FlagKind flagKind = FlagKind::Char;
    QStringList aliasTexts;   // AF: flag sets by number, from 1
    bool aliasCountRead = false;
    for (const QString& rawLine : affLines) {
        const QStringList t = rawLine.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.isEmpty()) continue;
        // COMPLEXPREFIXES stands alone on its line.
        if (t[0] == QLatin1String("COMPLEXPREFIXES") || t[0] == QLatin1String("PSEUDOROOT")) return {};
        if (t.size() < 2) continue;
        if (t[0] == QLatin1String("FLAG")) {
            if (t[1] == QLatin1String("long")) flagKind = FlagKind::Long;
            else if (t[1] == QLatin1String("num")) flagKind = FlagKind::Num;
            else if (t[1].compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0) flagKind = FlagKind::Utf8;
        } else if (t[0] == QLatin1String("AF")) {
            // The first AF line is the count, the others the sets.
            if (aliasCountRead) aliasTexts.append(t[1]);
            aliasCountRead = true;
        }
    }
    std::vector<std::vector<Flag>> aliases(size_t(aliasTexts.size()));
    for (qsizetype i = 0; i < aliasTexts.size(); ++i) splitFlags(aliasTexts[i], flagKind, aliases[size_t(i)]);
    // Flags written out, or an alias number with AF.
    const auto readFlags = [&](QStringView field, std::vector<Flag>& flags) {
        if (!aliases.empty()) {
            bool number = false;
            const qsizetype alias = field.toInt(&number);
            if (number) {
                if (alias >= 1 && alias <= qsizetype(aliases.size())) flags = aliases[size_t(alias - 1)];
                else flags.clear();
                return;
            }
        }
        splitFlags(field, flagKind, flags);
    };

    QHash<Flag, Affix> suffixes, prefixes;
    QSet<Flag> skipFlags;           // NOSUGGEST, ONLYINCOMPOUND, FORBIDDENWORD
    std::optional<Flag> needAffix;  // no word without (another) affix
    std::optional<Flag> circumfix;  // a prefix and a suffix only together
    std::optional<Flag> forbidden;  // a word no affix may make
    for (const QString& rawLine : affLines) {
        const QStringList t = rawLine.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.size() < 2) continue;
        const QString& kind = t[0];
        if (kind == QLatin1String("NEEDAFFIX")) needAffix = oneFlag(t[1], flagKind);
        else if (kind == QLatin1String("CIRCUMFIX")) circumfix = oneFlag(t[1], flagKind);
        else if (kind == QLatin1String("NOSUGGEST") || kind == QLatin1String("ONLYINCOMPOUND")
                 || kind == QLatin1String("FORBIDDENWORD"))
            skipFlags.insert(oneFlag(t[1], flagKind));
        if (kind == QLatin1String("FORBIDDENWORD")) forbidden = oneFlag(t[1], flagKind);
        if ((kind != QLatin1String("SFX") && kind != QLatin1String("PFX")) || t.size() < 4) continue;
        Affix& affix = (kind == QLatin1String("SFX") ? suffixes : prefixes)[oneFlag(t[1], flagKind)];
        if (t.size() == 4) {
            affix.cross = t[2] == QLatin1String("Y");
            continue;
        }
        Rule rule;
        rule.strip = t[2] == QLatin1String("0") ? QString() : t[2];
        const qsizetype slash = t[3].indexOf(QLatin1Char('/'));
        const QString add = slash < 0 ? t[3] : t[3].left(slash);
        rule.add = add == QLatin1String("0") ? QString() : add;
        if (slash >= 0) readFlags(QStringView(t[3]).mid(slash + 1), rule.cont);
        rule.condition = Condition::parse(t.size() > 4 ? t[4] : QStringLiteral("."));
        affix.rules.push_back(rule);
    }

    const auto has = [](const std::vector<Flag>& flags, std::optional<Flag> flag) {
        return flag && std::find(flags.cbegin(), flags.cend(), *flag) != flags.cend();
    };
    const auto skipped = [&](const std::vector<Flag>& flags) {
        return std::any_of(flags.cbegin(), flags.cend(), [&](Flag f) { return skipFlags.contains(f); });
    };
    const auto suffixed = [](const QString& base, const Rule& rule) -> std::optional<QString> {
        if (!base.endsWith(rule.strip)) return std::nullopt;
        if (!rule.condition.matchesAt(base, int(base.size()) - int(rule.condition.parts.size()))) return std::nullopt;
        return base.left(base.size() - rule.strip.size()) + rule.add;
    };
    const auto prefixed = [](const QString& base, const Rule& rule) -> std::optional<QString> {
        if (!base.startsWith(rule.strip) || !rule.condition.matchesAt(base, 0)) return std::nullopt;
        return rule.add + base.mid(rule.strip.size());
    };

    // A word with one or two suffixes (the second allowed by the first's
    // continuation class), as hunspell's twofold suffix stripping.
    struct Suffixed {
        QString form;
        bool cross;               // every suffix combines with prefixes
        bool circumfix;           // needs a CIRCUMFIX prefix
        bool needsMore;           // the outer suffix needs another affix
        std::vector<Flag> cont;   // the suffixes' continuation classes (prefixes they allow)
    };
    // False when there are too many (kMaxForms).
    const auto addSuffixes = [&](const QString& word, const std::vector<Flag>& allowed,
                                 std::vector<Suffixed>& out) -> bool {
        for (const Flag flag : allowed) {
            const auto first = suffixes.constFind(flag);
            if (first == suffixes.constEnd()) continue;
            for (const Rule& rule : first->rules) {
                if (skipped(rule.cont)) continue;
                const std::optional<QString> form = suffixed(word, rule);
                if (!form) continue;
                if (out.size() > size_t(kMaxForms)) return false;
                out.push_back({*form, first->cross, has(rule.cont, circumfix), has(rule.cont, needAffix), rule.cont});
                const Suffixed inner = out.back();
                for (const Flag outerFlag : inner.cont) {
                    const auto second = suffixes.constFind(outerFlag);
                    if (second == suffixes.constEnd()) continue;
                    for (const Rule& outer : second->rules) {
                        if (skipped(outer.cont)) continue;
                        const std::optional<QString> twice = suffixed(inner.form, outer);
                        if (!twice) continue;
                        std::vector<Flag> cont = inner.cont;
                        cont.insert(cont.end(), outer.cont.cbegin(), outer.cont.cend());
                        out.push_back({*twice, inner.cross && second->cross,
                                       inner.circumfix || has(outer.cont, circumfix), has(outer.cont, needAffix),
                                       std::move(cont)});
                    }
                }
            }
        }
        return true;
    };

    QStringList forms;
    QSet<QString> forbiddenWords;
    std::vector<Flag> flags, prefixFlags, extraSuffixFlags;
    std::vector<Suffixed> withSuffixes, withPrefixSuffixes;
    const QStringList dic = readLines(dicPath, encoding);
    for (int lineNumber = 1; lineNumber < dic.size(); ++lineNumber) {  // the first line is a count
        const QString entry = dic[lineNumber].section(QLatin1Char('\t'), 0, 0).section(QLatin1Char(' '), 0, 0).trimmed();
        if (entry.isEmpty()) continue;
        const int slash = entry.indexOf(QLatin1Char('/'));
        const QString word = slash < 0 ? entry : entry.left(slash);
        if (slash < 0) flags.clear();
        else readFlags(QStringView(entry).mid(slash + 1), flags);
        if (has(flags, forbidden)) forbiddenWords.insert(word);
        if (skipped(flags)) continue;
        if (!has(flags, needAffix)) forms.append(word);
        withSuffixes.clear();
        if (!addSuffixes(word, flags, withSuffixes)) return {};
        for (const Suffixed& s : withSuffixes)
            if (!s.circumfix && !s.needsMore) forms.append(s.form);

        // Prefixes of the word, or allowed by a suffix's continuation class.
        prefixFlags = flags;
        for (const Suffixed& s : withSuffixes) prefixFlags.insert(prefixFlags.end(), s.cont.cbegin(), s.cont.cend());
        std::sort(prefixFlags.begin(), prefixFlags.end());
        prefixFlags.erase(std::unique(prefixFlags.begin(), prefixFlags.end()), prefixFlags.end());
        for (const Flag flag : prefixFlags) {
            const auto prefix = prefixes.constFind(flag);
            if (prefix == prefixes.constEnd()) continue;
            const bool onWord = std::find(flags.cbegin(), flags.cend(), flag) != flags.cend();
            for (const Rule& rule : prefix->rules) {
                if (skipped(rule.cont)) continue;
                const bool ruleCircumfix = has(rule.cont, circumfix);
                if (onWord && !ruleCircumfix && !has(rule.cont, needAffix))
                    if (const auto form = prefixed(word, rule)) forms.append(*form);
                const auto combine = [&](const Suffixed& s, bool needCross) {
                    if (s.circumfix != ruleCircumfix) return;
                    if (needCross && !(prefix->cross && s.cross)) return;
                    if (const auto form = prefixed(s.form, rule)) forms.append(*form);
                };
                for (const Suffixed& s : withSuffixes)
                    if (onWord || std::find(s.cont.cbegin(), s.cont.cend(), flag) != s.cont.cend()) combine(s, true);
                // Suffixes the prefix's continuation class allows.
                if (!onWord || rule.cont.empty()) continue;
                extraSuffixFlags.clear();
                for (const Flag f : rule.cont)
                    if (std::find(flags.cbegin(), flags.cend(), f) == flags.cend()) extraSuffixFlags.push_back(f);
                withPrefixSuffixes.clear();
                if (!addSuffixes(word, extraSuffixFlags, withPrefixSuffixes)) return {};
                for (const Suffixed& s : withPrefixSuffixes) combine(s, false);
                if (forms.size() > kMaxForms) return {};
            }
        }
        if (forms.size() > kMaxForms) return {};
    }
    // A forbidden word in the .dic forbids the form whatever makes it ("Kongs"
    // in Dutch); a form beginning or ending with a hyphen is a part of
    // compound words only ("apk-").
    forms.removeIf([&](const QString& form) {
        return forbiddenWords.contains(form) || form.startsWith(QLatin1Char('-')) || form.endsWith(QLatin1Char('-'));
    });
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

std::vector<std::pair<double, QString>> FormIndex::candidates(const QString& word, double limit,
                                                              const QHash<QString, QStringList>* extraClasses) const {
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
        if (!extraClasses) continue;
        if (auto it = extraClasses->constFind(probe); it != extraClasses->cend())
            for (const QString& extra : it.value()) {
                const double dist = score(word, extra, limit);
                if (dist <= limit) result.emplace_back(dist, extra);
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

std::vector<std::pair<double, QString>> rankSuggestions(const QString& word,
                                                        std::vector<std::pair<double, QString>> candidates,
                                                        int maxCount, const QStringList& preferred,
                                                        double closeToBest) {
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
    std::vector<std::pair<double, QString>> result;
    if (candidates.empty()) return result;
    const auto shown = [&result](const QString& form) {
        return std::any_of(result.cbegin(), result.cend(), [&form](const auto& kept) {
            return kept.second.compare(form, Qt::CaseInsensitive) == 0;
        });
    };
    const bool allCaps = word.size() > 1 && word == word.toUpper();
    const bool capitalized = !allCaps && !word.isEmpty() && word[0].isUpper();
    for (const auto& [dist, candidate] : candidates) {
        // Close to the best only: usually one suggestion, not always maxCount.
        // (The small allowance keeps a candidate exactly kCloseToBest behind,
        // e.g. 0.3 + 0.6 summed as 0.9000001.)
        if (int(result.size()) >= std::min(maxCount, kMaxShown)
                || dist > candidates.front().first + closeToBest + 1e-9 || dist > kShownLimit + 1e-9)
            break;
        // A name differing only in case keeps its own ("warszawa" →
        // Warszawa); the others follow the word's ("POłączyłem" → Połączyłem).
        QString form = candidate;
        if (form.compare(word, Qt::CaseInsensitive) == 0 && form != form.toLower()) {
        } else if (allCaps) {
            form = form.toUpper();
        } else if (capitalized) {
            form[0] = form[0].toUpper();
        }
        // One of the forms differing only in case: the better one.
        if (!shown(form)) result.emplace_back(dist, form);
    }
    return result;
}

} // namespace qce::spell::detail
