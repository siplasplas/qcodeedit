#include "FormIndex.h"

#ifdef Q_OS_WIN
#include <QGuiApplication>
#include <QInputMethod>
#endif

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace qce::spell::detail {

namespace {

// --- Letters ---------------------------------------------------------------

// --- Keyboard layouts --------------------------------------------------------

struct KeyboardLayoutData {
    const char* name;
    const char16_t* rows[3];  // base characters of the letter rows
    const char16_t* pairs;    // character, base letter of its key, ...
};

#include "KeyboardLayouts.inc"

// A layout ready for use: its rows, and the characters typed with AltGr or
// Shift on a letter key, by that key's letter (lower case).
struct KeyboardLayout {
    QString name;
    QString rows[3];
    QHash<char16_t, char16_t> keyOf;
};

const std::vector<KeyboardLayout>& keyboards() {
    static const std::vector<KeyboardLayout> list = [] {
        std::vector<KeyboardLayout> result;
        for (const KeyboardLayoutData& data : kKeyboardLayouts) {
            KeyboardLayout keyboard;
            keyboard.name = QString::fromLatin1(data.name);
            for (int row = 0; row < 3; ++row) keyboard.rows[row] = QString::fromUtf16(data.rows[row]);
            const QString pairs = QString::fromUtf16(data.pairs);
            for (qsizetype i = 0; i + 1 < pairs.size(); i += 2) {
                keyboard.keyOf.insert(pairs[i].unicode(), pairs[i + 1].unicode());
                keyboard.keyOf.insert(pairs[i].toLower().unicode(), pairs[i + 1].unicode());
            }
            result.push_back(std::move(keyboard));
        }
        return result;
    }();
    return list;
}

int keyboardIndex(const QString& name) {
    for (size_t i = 0; i < keyboards().size(); ++i)
        if (keyboards()[i].name == name) return int(i);
    return -1;
}

// The layouts set up on the desktop, in its order, as xkb names ("pl", "us"),
// known ones only: KDE's setting, GNOME's, the system's default (Debian,
// Ubuntu); on Windows the input language (Polish there is the programmers'
// layout). Read once.
QStringList readDesktopKeyboardLayouts() {
    QStringList names;
#ifdef Q_OS_WIN
    if (const QInputMethod* input = QGuiApplication::inputMethod()) {
        const QString language = input->locale().name().section(QLatin1Char('_'), 0, 0);
        names << (language == QLatin1String("pl") ? QStringLiteral("pl-windows") : language);
    }
#else
    const auto add = [&names](const QString& list) {
        // "pl,us", "pl+dvp" (a variant), "pl(legacy)": the layouts' names.
        for (const QString& entry : list.split(QLatin1Char(','), Qt::SkipEmptyParts))
            names << entry.section(QLatin1Char('+'), 0, 0).section(QLatin1Char('('), 0, 0).trimmed();
    };
    QFile kde(QDir::homePath() + QStringLiteral("/.config/kxkbrc"));
    if (kde.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const QByteArray& line : kde.readAll().split('\n'))
            if (line.startsWith("LayoutList=")) add(QString::fromUtf8(line.mid(11)));
    if (names.isEmpty() && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("GNOME"))) {
        QProcess gsettings;
        gsettings.start(QStringLiteral("gsettings"),
                        {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.input-sources"),
                         QStringLiteral("sources")});
        if (gsettings.waitForFinished(2000)) {
            // [('xkb', 'pl'), ('xkb', 'us')]
            static const QRegularExpression source(QStringLiteral("\\('xkb',\\s*'([^']+)'\\)"));
            for (auto it = source.globalMatch(QString::fromUtf8(gsettings.readAllStandardOutput())); it.hasNext();)
                add(it.next().captured(1));
        }
    }
    if (names.isEmpty()) {
        QFile system(QStringLiteral("/etc/default/keyboard"));
        if (system.open(QIODevice::ReadOnly | QIODevice::Text))
            for (const QByteArray& line : system.readAll().split('\n'))
                if (line.startsWith("XKBLAYOUT=")) add(QString::fromUtf8(line.mid(10)).remove(QLatin1Char('"')));
    }
#endif
    QStringList known;
    for (const QString& name : std::as_const(names))
        if (keyboardIndex(name) >= 0 && !known.contains(name)) known << name;
    return known;
}

const QStringList& desktopKeyboardLayouts() {
    static const QStringList names = readDesktopKeyboardLayouts();
    return names;
}

// The language a layout is for ("us" English, "pl-windows" Polish).
QString languageOfLayout(const QString& layout) {
    if (layout == QLatin1String("us")) return QStringLiteral("en");
    return layout.section(QLatin1Char('-'), 0, 0);
}

// The layout setKeyboardLayout() chose for every text, or -1: by the text's
// language (keyboardLayoutFor()).
std::atomic<int> forcedKeyboard{-1};
// The layout of what this thread does now (KeyboardScope), or -1.
thread_local int scopedKeyboard = -1;

const KeyboardLayout& keyboard() {
    if (scopedKeyboard >= 0) return keyboards()[size_t(scopedKeyboard)];
    if (const int forced = forcedKeyboard.load(); forced >= 0) return keyboards()[size_t(forced)];
    // No text's language here: the desktop's first layout, else the system
    // language's.
    static const int fallback = keyboardIndex(keyboardLayoutFor(QLocale::system().name()));
    return keyboards()[size_t(fallback)];
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
    quint32 id = 0;  // in RuleText's list
};

// What a rule does to a form: removes `strip` UTF-16 units (at the end for a
// suffix, at the start for a prefix) and adds `add` there.
struct RuleText {
    int strip = 0;
    QString add;
};

// A stem with a prefix, a suffix and a second suffix applied (rule ids, 0
// for none).
QString composeForm(QString form, const std::vector<RuleText>& rules, quint32 prefix, quint32 suffix,
                    quint32 secondSuffix) {
    for (const quint32 id : {suffix, secondSuffix}) {
        if (!id) continue;
        form.chop(rules[id].strip);
        form += rules[id].add;
    }
    if (prefix) form = rules[prefix].add + form.mid(rules[prefix].strip);
    return form;
}

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

// '4': classes by the keyboard layout's letters (a file per layout).
constexpr char kMagic[8] = {'Q', 'C', 'E', 'F', 'O', 'R', 'M', '4'};

// A form in the file: its stem (3 bytes), its combination of rules (3 bytes)
// and 16 bits of its class's hash (2 bytes), little-endian.
constexpr int kRecordSize = 8;
constexpr quint32 kMaxIndex = 1u << 24;

#pragma pack(push, 1)
struct Header {
    char magic[8];
    quint32 formCount;
    quint32 bucketBits;     // 2^bucketBits buckets of forms, by class hash
    quint32 stemCount;
    quint32 ruleCount;      // rule 0 is "none"
    quint32 comboCount;
    quint32 alphabetSize;   // bytes
    quint64 dicSize;
    qint64 dicTime;
    quint64 affSize;
    qint64 affTime;
    quint64 stemOffsets;    // (stemCount + 1) quint32 into the stem text
    quint64 stemText;       // UTF-8, the .dic's words
    quint64 ruleStrip;      // ruleCount quint16: UTF-16 units the rule removes
    quint64 ruleAddOffsets; // (ruleCount + 1) quint32 into the rule text
    quint64 ruleAddText;    // UTF-8, what each rule adds
    quint64 combos;         // comboCount × 3 quint32: prefix, suffix, second suffix
    quint64 records;        // formCount × kRecordSize, ordered by bucket
    quint64 bucketStart;    // (2^bucketBits + 1) quint32: first record of each bucket
    quint64 alphabet;       // UTF-8: every letter of the class keys
};
#pragma pack(pop)

// The hash of a class key (FNV-1a over its UTF-16 units): the same in every
// program and Qt version, unlike qHash().
quint64 classHash(const QString& key) {
    quint64 h = 14695981039346656037ull;
    for (const QChar c : key) {
        h ^= c.unicode();
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

QChar foldChar(QChar c) {
    // Accented Latin letters: their base letter (ą → a, é → e, ź → z);
    // letters with a stroke too (ł → l). Looked up once for the Latin blocks,
    // where the letters are.
    static const std::vector<char16_t> latin = [] {
        std::vector<char16_t> t(0x250);
        for (char16_t u = 0; u < t.size(); ++u) {
            const QChar letter(u);
            t[u] = letter.toLower().unicode();
            if (letter.decompositionTag() != QChar::Canonical) continue;
            const QChar base = letter.decomposition().front();
            if (base.unicode() < 0x80 && base.isLetter()) t[u] = base.toLower().unicode();
        }
        for (const auto& [stroked, base] : {std::pair{u'ł', u'l'}, {u'Ł', u'l'}, {u'ø', u'o'}, {u'Ø', u'o'},
                                            {u'đ', u'd'}, {u'Đ', u'd'}, {u'ħ', u'h'}, {u'Ħ', u'h'},
                                            {u'ŧ', u't'}, {u'Ŧ', u't'}, {u'ı', u'i'}})
            t[stroked] = base;
        return t;
    }();
    const QChar folded = c.unicode() < latin.size() ? QChar(latin[c.unicode()]) : c.toLower();
    if (folded.unicode() < 0x80) return folded;
    // Typed with AltGr on a letter key: that key's letter (© → r on the
    // Polish keyboard, where AltGr was not let go after ó: "któ©re").
    const auto& keys = keyboard().keyOf;
    if (const auto it = keys.constFind(folded.unicode()); it != keys.cend()) return QChar(it.value());
    return folded;
}

QStringList keyboardLayouts() {
    QStringList names;
    for (const KeyboardLayout& layout : keyboards()) names << layout.name;
    return names;
}

QString keyboardLayoutFor(const QString& language) {
    if (const int forced = forcedKeyboard.load(); forced >= 0) return keyboards()[size_t(forced)].name;
    const QString code = language.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('-'), 0, 0).toLower();
    // One of the desktop's layouts for the language, else the first one: the
    // keyboard the user types on (an English text on a Polish keyboard).
    const QStringList& desktop = desktopKeyboardLayouts();
    for (const QString& layout : desktop)
        if (languageOfLayout(layout) == code) return layout;
    if (!desktop.isEmpty()) return desktop.front();
    // Nothing known of the desktop: the language's own layout.
#ifdef Q_OS_WIN
    if (code == QLatin1String("pl")) return QStringLiteral("pl-windows");
#endif
    if (code != QLatin1String("us") && keyboardIndex(code) >= 0) return code;
    return QStringLiteral("us");
}

QString keyboardLayout() {
    return keyboard().name;
}

QString forcedKeyboardLayout() {
    const int forced = forcedKeyboard.load();
    return forced >= 0 ? keyboards()[size_t(forced)].name : QString();
}

bool setKeyboardLayout(const QString& name) {
    if (name.isEmpty()) {
        forcedKeyboard.store(-1);
        return true;
    }
    const int index = keyboardIndex(name);
    if (index < 0) return false;
    forcedKeyboard.store(index);
    return true;
}

KeyboardScope::KeyboardScope(const QString& layout) : m_previous(scopedKeyboard) {
    scopedKeyboard = keyboardIndex(layout);
}

KeyboardScope::~KeyboardScope() {
    scopedKeyboard = m_previous;
}

bool typedOnLetterKey(QChar c) {
    if (c.isLetter() || c.isPunct()) return false;
    return onLetterKey(c);
}

bool punctuationOnLetterKey(QChar c) {
    return c.isPunct() && onLetterKey(c);
}

bool onLetterKey(QChar c) {
    // On the keyboard in use, or any the desktop has.
    if (keyboard().keyOf.contains(c.unicode())) return true;
    for (const QString& name : desktopKeyboardLayouts())
        if (keyboards()[size_t(keyboardIndex(name))].keyOf.contains(c.unicode())) return true;
    return false;
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
    static const double offsets[] = {0, 0.25, 0.75};
    const KeyboardLayout& layout = keyboard();
    // The letter's own key (ö on a German keyboard), else its base letter's.
    for (const QChar key : {c.toLower(), foldChar(c)})
        for (int row = 0; row < 3; ++row)
            if (const qsizetype column = layout.rows[row].indexOf(key); column >= 0)
                return {row, offsets[row] + double(column)};
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
    // Letters missing before the first typed one: the word's beginning.
    for (int j = 0; j <= m; ++j) dist[at(0, j)] = j * kInsertCost + (j > 0 ? kInsertAtStartCost : 0);
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
            if (la[t] != lb[q]) cost += kMovedDiacriticCost;
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
            // Another letter: cheaper on a neighbouring key ("nonth" → month).
            double change = keysTouch(x, y) ? kNeighbourKeyCost : kOtherLetterCost;
            if (x == y) {
                change = 0;
            } else if (foldChar(x) == foldChar(y)) {
                // A missing diacritic is cheap; one typed by mistake (AltGr
                // pressed on purpose) costs more, unless AltGr was still held
                // from the letter before ("żę" for "że").
                const bool removesTyped = marked(x) && !marked(y);
                change = removesTyped && !(i > 1 && marked(la[i - 2])) ? kTypedDiacriticCost : kMissingDiacriticCost;
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
        d += kSplitCost;
    // A capital letter (e.g. a proper name) where the word has none.
    if (candidate != candidate.toLower() && word == word.toLower()) d += kNewCapitalCost;
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

// More forms than this (16 bytes each while building) are left to
// hunspell: e.g. Hungarian, whose suffixes follow each other by thousands.
constexpr qsizetype kMaxForms = 30'000'000;
constexpr qsizetype kMaxFormsOfStem = 1'000'000;

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

namespace {

// A stem of the .dic (returns its number) and each form made of it.
using StemSink = std::function<quint32(const QString& stem)>;
using FormSink = std::function<void(quint32 stem, quint32 prefix, quint32 suffix, quint32 secondSuffix,
                                    const QString& form)>;

// Every form of a dictionary given to `onForm`, with the rules (`rules`,
// rule 0 being none) that make it of its stem; false when not supported.
bool expandForms(const QString& affPath, const QString& dicPath, std::vector<RuleText>& rules,
                 const StemSink& onStem, const FormSink& onForm) {
    rules.assign(1, RuleText{});
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
        if (t[0] == QLatin1String("COMPLEXPREFIXES") || t[0] == QLatin1String("PSEUDOROOT")) return false;
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
        rule.id = quint32(rules.size());
        rules.push_back({int(rule.strip.size()), rule.add});
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
        quint32 suffix, secondSuffix;  // rule ids
        bool cross;               // every suffix combines with prefixes
        bool circumfix;           // needs a CIRCUMFIX prefix
        bool needsMore;           // the outer suffix needs another affix
        std::vector<Flag> cont;   // the suffixes' continuation classes (prefixes they allow)
    };
    qsizetype count = 0;  // forms given so far
    // False when there are too many: kMaxForms in all, or kMaxFormsOfStem of
    // one stem (Hungarian: thousands of suffixes after each other).
    const auto addSuffixes = [&](const QString& word, const std::vector<Flag>& allowed,
                                 std::vector<Suffixed>& out) -> bool {
        for (const Flag flag : allowed) {
            const auto first = suffixes.constFind(flag);
            if (first == suffixes.constEnd()) continue;
            for (const Rule& rule : first->rules) {
                if (skipped(rule.cont)) continue;
                const std::optional<QString> form = suffixed(word, rule);
                if (!form) continue;
                if (qsizetype(out.size()) > kMaxFormsOfStem || count + qsizetype(out.size()) > kMaxForms) return false;
                out.push_back({*form, rule.id, 0, first->cross, has(rule.cont, circumfix), has(rule.cont, needAffix),
                               rule.cont});
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
                        out.push_back({*twice, inner.suffix, outer.id, inner.cross && second->cross,
                                       inner.circumfix || has(outer.cont, circumfix), has(outer.cont, needAffix),
                                       std::move(cont)});
                    }
                }
            }
        }
        return true;
    };

    const QStringList dic = readLines(dicPath, encoding);
    std::vector<Flag> flags, prefixFlags, extraSuffixFlags;
    // Each line's word and flags.
    const auto readEntry = [&](int lineNumber, QString& word) {
        const QString entry = dic[lineNumber].section(QLatin1Char('\t'), 0, 0).section(QLatin1Char(' '), 0, 0).trimmed();
        const int slash = entry.indexOf(QLatin1Char('/'));
        word = slash < 0 ? entry : entry.left(slash);
        if (slash < 0) flags.clear();
        else readFlags(QStringView(entry).mid(slash + 1), flags);
    };
    // A forbidden word in the .dic forbids the form whatever makes it
    // ("Kongs" in Dutch), also one made of an earlier stem.
    QSet<QString> forbiddenWords;
    QString word;
    if (forbidden)
        for (int lineNumber = 1; lineNumber < dic.size(); ++lineNumber) {
            readEntry(lineNumber, word);
            if (has(flags, forbidden)) forbiddenWords.insert(word);
        }
    quint32 stem = 0;
    // A form beginning or ending with a hyphen is a part of compound words
    // only ("apk-").
    const auto giveForm = [&](quint32 prefix, quint32 suffix, quint32 secondSuffix, const QString& form) {
        if (form.startsWith(QLatin1Char('-')) || form.endsWith(QLatin1Char('-')) || forbiddenWords.contains(form))
            return;
        ++count;
        onForm(stem, prefix, suffix, secondSuffix, form);
    };

    std::vector<Suffixed> withSuffixes, withPrefixSuffixes;
    for (int lineNumber = 1; lineNumber < dic.size(); ++lineNumber) {  // the first line is a count
        readEntry(lineNumber, word);
        if (word.isEmpty() || skipped(flags)) continue;
        stem = onStem(word);
        if (!has(flags, needAffix)) giveForm(0, 0, 0, word);
        withSuffixes.clear();
        if (!addSuffixes(word, flags, withSuffixes)) return false;
        for (const Suffixed& s : withSuffixes)
            if (!s.circumfix && !s.needsMore) giveForm(0, s.suffix, s.secondSuffix, s.form);

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
                    if (const auto form = prefixed(word, rule)) giveForm(rule.id, 0, 0, *form);
                const auto combine = [&](const Suffixed& s, bool needCross) {
                    if (s.circumfix != ruleCircumfix) return;
                    if (needCross && !(prefix->cross && s.cross)) return;
                    if (const auto form = prefixed(s.form, rule)) giveForm(rule.id, s.suffix, s.secondSuffix, *form);
                };
                for (const Suffixed& s : withSuffixes)
                    if (onWord || std::find(s.cont.cbegin(), s.cont.cend(), flag) != s.cont.cend()) combine(s, true);
                // Suffixes the prefix's continuation class allows.
                if (!onWord || rule.cont.empty()) continue;
                extraSuffixFlags.clear();
                for (const Flag f : rule.cont)
                    if (std::find(flags.cbegin(), flags.cend(), f) == flags.cend()) extraSuffixFlags.push_back(f);
                withPrefixSuffixes.clear();
                if (!addSuffixes(word, extraSuffixFlags, withPrefixSuffixes)) return false;
                for (const Suffixed& s : withPrefixSuffixes) combine(s, false);
                if (count > kMaxForms) return false;
            }
        }
        if (count > kMaxForms) return false;
    }
    return true;
}

} // namespace

QStringList expandDictionary(const QString& affPath, const QString& dicPath, bool* supported) {
    QStringList forms;
    std::vector<RuleText> rules;
    quint32 stems = 0;
    *supported = expandForms(affPath, dicPath, rules, [&](const QString&) { return stems++; },
                             [&](quint32, quint32, quint32, quint32, const QString& form) { forms.append(form); });
    if (!*supported) forms.clear();
    return forms;
}

QString formIndexPath(const QString& dictionaryName, const QString& layout) {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(cache).filePath(QStringLiteral("siplasplas/spell/%1-%2.forms").arg(dictionaryName, layout));
}

bool buildFormIndex(const QString& affPath, const QString& dicPath, const QString& indexPath) {
    // Streamed: a form is kept as its stem, its rules and its class's hash
    // (16 bytes), never as text, so even tens of millions fit.
    struct Built {
        quint64 hash;
        quint32 stem;
        quint32 combo;
    };
    std::vector<Built> built;
    std::vector<RuleText> rules;
    QByteArray stemText;
    std::vector<quint32> stemOffsets;
    std::vector<quint32> combos;  // 3 per combination
    QHash<QByteArray, quint32> comboIds;
    QSet<QChar> letters;
    const auto onStem = [&](const QString& stem) {
        stemOffsets.push_back(quint32(stemText.size()));
        stemText += stem.toUtf8();
        return quint32(stemOffsets.size() - 1);
    };
    const auto onForm = [&](quint32 stem, quint32 prefix, quint32 suffix, quint32 secondSuffix, const QString& form) {
        const quint32 parts[3] = {prefix, suffix, secondSuffix};
        const QByteArray comboKey(reinterpret_cast<const char*>(parts), sizeof parts);
        auto it = comboIds.constFind(comboKey);
        if (it == comboIds.cend()) {
            it = comboIds.insert(comboKey, quint32(comboIds.size()));
            combos.insert(combos.end(), std::begin(parts), std::end(parts));
        }
        const QString key = classKey(form);
        for (const QChar c : key) letters.insert(c);
        built.push_back({classHash(key), stem, it.value()});
    };
    if (!expandForms(affPath, dicPath, rules, onStem, onForm) || built.empty()) return false;
    stemOffsets.push_back(quint32(stemText.size()));
    if (stemOffsets.size() > kMaxIndex || comboIds.size() > qsizetype(kMaxIndex)) return false;

    // Buckets by the low bits of the hash, about two forms each; within one,
    // by the whole hash, so that a class's forms lie together.
    quint32 bucketBits = 10;
    while ((quint64(1) << bucketBits) < built.size() / 2) ++bucketBits;
    const quint64 mask = (quint64(1) << bucketBits) - 1;
    std::sort(built.begin(), built.end(), [mask](const Built& x, const Built& y) {
        const quint64 bx = x.hash & mask, by = y.hash & mask;
        return bx != by ? bx < by : x.hash != y.hash ? x.hash < y.hash : std::pair(x.stem, x.combo) < std::pair(y.stem, y.combo);
    });
    const auto stemAt = [&](quint32 i) {
        return QString::fromUtf8(stemText.constData() + stemOffsets[i], stemOffsets[i + 1] - stemOffsets[i]);
    };
    const auto formOf = [&](const Built& f) {
        return composeForm(stemAt(f.stem), rules, combos[3 * f.combo], combos[3 * f.combo + 1], combos[3 * f.combo + 2]);
    };
    // The same form made twice (two stems or rules): kept once. Equal forms
    // have equal hashes, so only those runs are compared.
    QByteArray records;
    records.reserve(qsizetype(built.size()) * kRecordSize);
    std::vector<quint32> bucketStart(size_t(mask) + 2, 0);
    quint32 formCount = 0;
    QSet<QString> seen;
    for (size_t i = 0; i < built.size();) {
        size_t end = i + 1;
        while (end < built.size() && built[end].hash == built[i].hash) ++end;
        seen.clear();
        for (size_t k = i; k < end; ++k) {
            if (end - i > 1) {
                const QString form = formOf(built[k]);
                if (seen.contains(form)) continue;
                seen.insert(form);
            }
            const Built& f = built[k];
            const quint16 fingerprint = quint16(f.hash >> 48);
            const char record[kRecordSize] = {
                char(f.stem), char(f.stem >> 8), char(f.stem >> 16),
                char(f.combo), char(f.combo >> 8), char(f.combo >> 16),
                char(fingerprint), char(fingerprint >> 8)};
            records.append(record, kRecordSize);
            ++bucketStart[size_t(f.hash & mask) + 1];
            ++formCount;
        }
        i = end;
    }
    built.clear();
    built.shrink_to_fit();
    for (size_t b = 1; b < bucketStart.size(); ++b) bucketStart[b] += bucketStart[b - 1];

    std::vector<quint16> ruleStrip;
    std::vector<quint32> ruleAddOffsets;
    QByteArray ruleAddText;
    for (const RuleText& rule : rules) {
        ruleStrip.push_back(quint16(rule.strip));
        ruleAddOffsets.push_back(quint32(ruleAddText.size()));
        ruleAddText += rule.add.toUtf8();
    }
    ruleAddOffsets.push_back(quint32(ruleAddText.size()));
    QString alphabet;
    for (const QChar c : letters) alphabet.append(c);
    std::sort(alphabet.begin(), alphabet.end());
    const QByteArray alphabetBytes = alphabet.toUtf8();

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof kMagic);
    header.formCount = formCount;
    header.bucketBits = bucketBits;
    header.stemCount = quint32(stemOffsets.size() - 1);
    header.ruleCount = quint32(rules.size());
    header.comboCount = quint32(combos.size() / 3);
    header.alphabetSize = quint32(alphabetBytes.size());
    const QFileInfo dic(dicPath), aff(affPath);
    header.dicSize = quint64(dic.size());
    header.dicTime = dic.lastModified().toMSecsSinceEpoch();
    header.affSize = quint64(aff.size());
    header.affTime = aff.lastModified().toMSecsSinceEpoch();
    quint64 pos = sizeof(Header);
    const auto place = [&pos](quint64& field, size_t size) {
        field = pos;
        pos += size;
    };
    place(header.stemOffsets, stemOffsets.size() * sizeof(quint32));
    place(header.stemText, size_t(stemText.size()));
    place(header.ruleStrip, ruleStrip.size() * sizeof(quint16));
    place(header.ruleAddOffsets, ruleAddOffsets.size() * sizeof(quint32));
    place(header.ruleAddText, size_t(ruleAddText.size()));
    place(header.combos, combos.size() * sizeof(quint32));
    place(header.records, size_t(records.size()));
    place(header.bucketStart, bucketStart.size() * sizeof(quint32));
    place(header.alphabet, size_t(alphabetBytes.size()));

    QDir().mkpath(QFileInfo(indexPath).absolutePath());
    QSaveFile file(indexPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto write = [&file](const void* data, size_t size) {
        return file.write(static_cast<const char*>(data), qint64(size)) == qint64(size);
    };
    const bool ok = write(&header, sizeof header)
        && write(stemOffsets.data(), stemOffsets.size() * sizeof(quint32))
        && write(stemText.constData(), size_t(stemText.size()))
        && write(ruleStrip.data(), ruleStrip.size() * sizeof(quint16))
        && write(ruleAddOffsets.data(), ruleAddOffsets.size() * sizeof(quint32))
        && write(ruleAddText.constData(), size_t(ruleAddText.size()))
        && write(combos.data(), combos.size() * sizeof(quint32))
        && write(records.constData(), size_t(records.size()))
        && write(bucketStart.data(), bucketStart.size() * sizeof(quint32))
        && write(alphabetBytes.constData(), size_t(alphabetBytes.size()));
    return ok && file.commit();
}

// --- Index -------------------------------------------------------------------

struct FormIndex::Private {
    QFile file;
    const uchar* data = nullptr;
    Header header{};
    QString alphabet;
    std::vector<RuleText> rules;  // few: read once

    template <typename T>
    const T* at(quint64 offset) const { return reinterpret_cast<const T*>(data + offset); }
    static quint32 read24(const uchar* p) { return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16; }

    const uchar* record(quint32 i) const { return data + header.records + quint64(i) * kRecordSize; }
    quint16 fingerprint(quint32 i) const {
        const uchar* r = record(i);
        return quint16(r[6] | r[7] << 8);
    }
    QString form(quint32 i) const {
        const uchar* r = record(i);
        const quint32 stem = read24(r), combo = read24(r + 3);
        const quint32* stemOffsets = at<quint32>(header.stemOffsets);
        const QString stemText = QString::fromUtf8(at<char>(header.stemText) + stemOffsets[stem],
                                                   qsizetype(stemOffsets[stem + 1] - stemOffsets[stem]));
        const quint32* parts = at<quint32>(header.combos) + 3 * quint64(combo);
        return composeForm(stemText, rules, parts[0], parts[1], parts[2]);
    }
    // The records of a class's bucket: [first, end).
    std::pair<quint32, quint32> bucket(quint64 hash) const {
        const quint32* start = at<quint32>(header.bucketStart);
        const quint64 b = hash & ((quint64(1) << header.bucketBits) - 1);
        return {start[b], start[b + 1]};
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
            || p.header.bucketBits > 31
            || p.header.alphabet + p.header.alphabetSize > quint64(p.file.size()))
        return nullptr;
    p.alphabet = QString::fromUtf8(p.at<char>(p.header.alphabet), qsizetype(p.header.alphabetSize));
    const quint16* strip = p.at<quint16>(p.header.ruleStrip);
    const quint32* addOffsets = p.at<quint32>(p.header.ruleAddOffsets);
    for (quint32 r = 0; r < p.header.ruleCount; ++r)
        p.rules.push_back({strip[r], QString::fromUtf8(p.at<char>(p.header.ruleAddText) + addOffsets[r],
                                                       qsizetype(addOffsets[r + 1] - addOffsets[r]))});
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
        const quint64 hash = classHash(probe);
        const quint16 fingerprint = quint16(hash >> 48);
        const auto [first, end] = d->bucket(hash);
        for (quint32 i = first; i < end; ++i) {
            // Another class in the bucket: told by the fingerprint, rarely
            // only by the form's own class.
            if (d->fingerprint(i) != fingerprint) continue;
            QString form = d->form(i);
            if (classKey(form) != probe) continue;
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
            // "# v2 2460104 words in pl.txt, 2297785 known to pl_PL" (no
            // "v2" in files of earlier versions)
            static const QRegularExpression known(QStringLiteral("(\\d+) known to"));
            static const QRegularExpression all(QStringLiteral("^#\\s*(?:v\\d+\\s+)?(\\d+) words"));
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
                                                        double closeToBest, const QHash<QString, double>* distances,
                                                        double shownLimit) {
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
    // The spelling distance of a candidate (without the frequency penalty).
    const auto spelling = [distances](const QString& form, double dist) {
        return distances ? distances->value(form.toLower(), dist) : dist;
    };
    double farthestShown = -1, bestPenalty = 0;
    for (const auto& [dist, candidate] : candidates) {
        if (int(result.size()) >= std::min(maxCount, kMaxShown)) break;
        // Close to the best only: usually one suggestion, not always maxCount.
        // (The small allowance keeps a candidate exactly kCloseToBest behind,
        // e.g. 0.3 + 0.6 summed as 0.9000001.) Rarer words fall behind, but
        // one spelled as close as a shown one is still shown unless it is
        // much rarer than the best (kRivalPenaltyGap): "fiunding" is funding
        // as much as finding; "bhyc" stays być, not the thousands of times
        // rarer hyc.
        const double penalty = dist - spelling(candidate, dist);
        const bool close = dist <= candidates.front().first + closeToBest + 1e-9 && dist <= shownLimit + 1e-9;
        const bool asNear = !result.empty() && spelling(candidate, dist) <= farthestShown + 1e-9
            && penalty <= bestPenalty + kRivalPenaltyGap + 1e-9;
        if (!close && !asNear) continue;
        if (result.empty()) bestPenalty = penalty;
        farthestShown = std::max(farthestShown, spelling(candidate, dist));
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
