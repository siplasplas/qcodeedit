#include <qce/spell/SpellChecker.h>

#include "FormIndex.h"
#include "UserStore.h"

#include <hunspell.hxx>

#include <QThreadPool>
#include <atomic>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QTextStream>

#include <algorithm>

namespace qce::spell {

namespace {

// How common each word of a split must be, per million words, when word
// frequencies are known: a single letter is rarely a word of its own.
constexpr double kPhraseWordPerMillion = 1;
constexpr double kPhraseLetterPerMillion = 300;

// cpg's language keys that are not ISO 639-1 codes.
QString isoCode(const QString& language) {
    static const QHash<QString, QString> cpg = {
        {QStringLiteral("cz"), QStringLiteral("cs")}, {QStringLiteral("gr"), QStringLiteral("el")},
        {QStringLiteral("dk"), QStringLiteral("da")}, {QStringLiteral("ua"), QStringLiteral("uk")},
        {QStringLiteral("se"), QStringLiteral("sv")}, {QStringLiteral("jp"), QStringLiteral("ja")}};
    const QString code = language.toLower();
    return cpg.value(code, code);
}

QString languageOf(const QString& dictionaryName) {
    return dictionaryName.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('-'), 0, 0).toLower();
}

QString displayNameOf(const QString& dictionaryName) {
    const QLocale locale(dictionaryName);
    if (locale.language() == QLocale::C) return dictionaryName;
    const QString language = QLocale::languageToString(locale.language());
    return dictionaryName.contains(QLatin1Char('_')) || dictionaryName.contains(QLatin1Char('-'))
        ? QStringLiteral("%1 (%2)").arg(language, QLocale::territoryToString(locale.territory()))
        : language;
}

// Hunspell's names for encodings, as Qt knows them.
QString qtEncodingName(const std::string& hunspellName) {
    const QString name = QString::fromStdString(hunspellName).trimmed();
    if (name.compare(QLatin1String("microsoft-cp1251"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("windows-1251");
    return name.isEmpty() ? QStringLiteral("ISO-8859-1") : name;
}

// A dictionary loaded once for the program, with the shared user dictionary.
struct Loaded {
    std::unique_ptr<Hunspell> hunspell;
    QString name;
    QString affPath;
    QString dicPath;
    QString encoding;
    bool utf8 = false;
    // The user's words and own corrections of the dictionary's language,
    // from the shared store (UserStore.h); read again when it changes.
    QString language;
    QSet<QString> userWords;
    // The same words by detail::classKey(), for suggestions (FormIndex::candidates()).
    QHash<QString, QStringList> userClasses;
    qint64 userStamp = -1;
    QHash<QString, QString> corrections;
    qint64 correctionsStamp = -1;
    // All word forms by class, for suggestions (see FormIndex.h): the cache
    // file, built in the background the first time; 0 not started, 1
    // building, 2 done (the file may still be missing: dictionary not
    // supported).
    std::unique_ptr<detail::FormIndex> index;
    std::shared_ptr<std::atomic<int>> indexState = std::make_shared<std::atomic<int>>(0);

    const detail::FormIndex* formIndex() {
        if (index) return index.get();
        const int state = indexState->load();
        if (state == 1) return nullptr;
        const QString path = detail::formIndexPath(name);
        index = detail::FormIndex::open(path, affPath, dicPath);
        if (index || state == 2) return index.get();
        indexState->store(1);
        QThreadPool::globalInstance()->start([state = indexState, aff = affPath, dic = dicPath, path]() {
            detail::buildFormIndex(aff, dic, path);
            state->store(2);
        });
        return nullptr;
    }

    // How frequent the dictionary's entries are (see FormIndex.h), read the
    // first time suggestions are wanted; empty without a file.
    // Read again when the file appears or changes (e.g. made by
    // buildWordFrequencies()).
    std::unique_ptr<detail::FrequencyTable> frequency;
    int frequencyVersion = 0;  // changes when the table is read again
    QString frequencyFile;
    QDateTime frequencyModified;
    // Remaking the file from the downloaded list: 0 not tried, 1 running, 2
    // done or failed (once per run; a new download makes the file itself).
    std::shared_ptr<std::atomic<int>> frequencyBuild = std::make_shared<std::atomic<int>>(0);
    const detail::FrequencyTable& frequencies() {
        const QString path = detail::frequencyPath(languageOf(name));
        const QDateTime modified = path.isEmpty() ? QDateTime() : QFileInfo(path).lastModified();
        // A file of our cache (not an installed one) older than the list or
        // the dictionary, or none: made again from the list, if there is one.
        const QFileInfo list(wordFrequencyListPath(name));
        if ((path.isEmpty() || path.startsWith(detail::frequencyCacheDir())) && list.exists()
                && (!modified.isValid() || modified < list.lastModified()
                    || modified < QFileInfo(dicPath).lastModified())
                && frequencyBuild->load() == 0) {
            frequencyBuild->store(1);
            QThreadPool::globalInstance()->start(
                [state = frequencyBuild, dictionary = name, listPath = list.absoluteFilePath()]() {
                    buildWordFrequencies(dictionary, listPath);
                    state->store(2);
                });
        }
        if (!frequency || path != frequencyFile || modified != frequencyModified) {
            frequency = std::make_unique<detail::FrequencyTable>(path);
            frequencyFile = path;
            frequencyModified = modified;
            ++frequencyVersion;
        }
        return *frequency;
    }
    // Occurrences per million of the word's stem (the most frequent of its
    // stems); negative when the dictionary has no frequencies or does not
    // know the word.
    double perMillion(const QString& word) {
        const detail::FrequencyTable& table = frequencies();
        if (table.isEmpty()) return -1;
        const std::string encoded = encode(word);
        if (encoded.empty()) return -1;
        const std::vector<std::string> stems = hunspell->stem(encoded);
        if (stems.empty()) return -1;
        double best = 0;
        for (const std::string& stem : stems) best = std::max(best, table.perMillion(decode(stem)));
        return best;
    }

    std::string encode(const QString& word) const {
        if (utf8) return word.toStdString();
        QStringEncoder encoder(encoding.toLatin1().constData());
        const QByteArray bytes = encoder(word);
        return encoder.hasError() ? std::string() : bytes.toStdString();
    }
    QString decode(const std::string& text) const {
        if (utf8) return QString::fromStdString(text);
        QStringDecoder decoder(encoding.toLatin1().constData());
        return decoder(QByteArray::fromStdString(text));
    }
    // Takes in the user words this or another program added or removed;
    // true when they changed.
    bool refreshUserWords() {
        const qint64 stamp = detail::UserStore::instance().stamp();
        if (stamp == userStamp) return false;
        userStamp = stamp;
        const QSet<QString> now = detail::UserStore::instance().words(language);
        if (now == userWords) return false;
        for (const QString& word : now)
            if (!userWords.contains(word)) hunspell->add(encode(word));
        for (const QString& word : std::as_const(userWords))
            if (!now.contains(word)) hunspell->remove(encode(word));
        userWords = now;
        userClasses.clear();
        for (const QString& word : now) userClasses[detail::classKey(word)].append(word);
        return true;
    }
    void refreshCorrections() {
        const qint64 stamp = detail::UserStore::instance().stamp();
        if (stamp == correctionsStamp) return;
        correctionsStamp = stamp;
        corrections = detail::UserStore::instance().corrections(language);
    }
};

struct Registry {
    QHash<QString, std::shared_ptr<Loaded>> loaded;
    int generation = 0;  // changes when any dictionary gains words

    std::shared_ptr<Loaded> load(const QString& name) {
        if (auto it = loaded.find(name); it != loaded.end()) return it.value();
        std::shared_ptr<Loaded> result;
        for (const Dictionary& dictionary : installedDictionaries()) {
            if (dictionary.name != name) continue;
            result = std::make_shared<Loaded>();
            result->name = name;
            result->affPath = dictionary.affPath;
            result->dicPath = dictionary.dicPath;
            result->hunspell = std::make_unique<Hunspell>(QFile::encodeName(dictionary.affPath).constData(),
                                                          QFile::encodeName(dictionary.dicPath).constData());
            result->encoding = qtEncodingName(result->hunspell->get_dict_encoding());
            result->utf8 = result->encoding.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0;
            result->language = languageOf(name);
            result->refreshUserWords();
            result->refreshCorrections();
            break;
        }
        loaded.insert(name, result);
        return result;
    }
};

Registry& registry() {
    static Registry instance;
    return instance;
}

bool isWordChar(QChar c) {
    return c.isLetterOrNumber() || c.isMark() || c == QLatin1Char('_');
}

bool isApostrophe(QChar c) {
    return c == QLatin1Char('\'') || c == QChar(0x2019);
}

// Tokens that are code or addresses rather than prose.
bool isTechnicalToken(const QString& token) {
    if (token.contains(QLatin1String("://")) || token.contains(QLatin1String("::"))
            || token.contains(QLatin1String("->")) || token.contains(QLatin1Char('/'))
            || token.contains(QLatin1Char('\\')))
        return true;
    if (token.contains(QLatin1Char('@')) && token.contains(QLatin1Char('.'))) return true;
    for (int i = 1; i + 1 < token.size(); ++i)  // a.b: member access, file name, domain
        if (token[i] == QLatin1Char('.') && token[i - 1].isLetterOrNumber() && token[i + 1].isLetterOrNumber())
            return true;
    return false;
}

bool worthChecking(const QString& word) {
    int letters = 0, upper = 0;
    for (int i = 0; i < word.size(); ++i) {
        const QChar c = word[i];
        if (c.isDigit() || c == QLatin1Char('_')) return false;
        if (!c.isLetter()) continue;
        ++letters;
        if (c.isUpper()) {
            ++upper;
            // An inner capital after a lowercase letter: camelCase, PascalCase.
            if (i > 0 && word[i - 1].isLower()) return false;
        }
    }
    if (letters < 2) return false;
    return upper < letters;  // ALL-CAPS: an acronym or constant
}

} // namespace

QStringList dictionaryDirectories() {
    QStringList directories;
    for (const QString& path : qEnvironmentVariable("DICPATH").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        directories << path;
    const QString user = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!user.isEmpty()) directories << user + QStringLiteral("/hunspell");
    directories << QStringLiteral("/usr/share/hunspell") << QStringLiteral("/usr/share/myspell")
                << QStringLiteral("/usr/share/myspell/dicts");
    return directories;
}

QList<Dictionary> installedDictionaries() {
    QList<Dictionary> result;
    QSet<QString> seen;
    for (const QString& directory : dictionaryDirectories()) {
        const QDir dir(directory);
        for (const QFileInfo& dic : dir.entryInfoList({QStringLiteral("*.dic")}, QDir::Files)) {
            const QString name = dic.completeBaseName();
            // Hyphenation patterns and thesauri share the extension.
            if (name.startsWith(QLatin1String("hyph_")) || name.startsWith(QLatin1String("th_"))
                    || seen.contains(name))
                continue;
            const QString aff = dir.filePath(name + QStringLiteral(".aff"));
            if (!QFileInfo::exists(aff)) continue;
            seen.insert(name);
            result.append({name, languageOf(name), displayNameOf(name), aff, dic.absoluteFilePath()});
        }
    }
    std::sort(result.begin(), result.end(), [](const Dictionary& a, const Dictionary& b) {
        return QString::localeAwareCompare(a.displayName, b.displayName) < 0;
    });
    return result;
}

QString dictionaryFor(const QString& language) {
    if (language.isEmpty()) return {};
    const QList<Dictionary> dictionaries = installedDictionaries();
    for (const Dictionary& dictionary : dictionaries)
        if (dictionary.name == language) return dictionary.name;
    const QString code = isoCode(language);
    // The language's own country first (pl_PL, de_DE, cs_CZ); English: en_US.
    const QString preferred = code == QLatin1String("en") ? QStringLiteral("en_US")
        : code == QLatin1String("cs") ? QStringLiteral("cs_CZ")
        : code + QLatin1Char('_') + code.toUpper();
    for (const Dictionary& dictionary : dictionaries)
        if (dictionary.name == preferred) return dictionary.name;
    QString first;
    for (const Dictionary& dictionary : dictionaries)
        if (dictionary.language == code && (first.isEmpty() || dictionary.name < first)) first = dictionary.name;
    return first;
}

QString packageHint(const QString& language) {
    const QString code = isoCode(languageOf(language));
    if (code.isEmpty()) return {};
    static const QHash<QString, QString> packages = {
        {QStringLiteral("en"), QStringLiteral("hunspell-en-us")}, {QStringLiteral("de"), QStringLiteral("hunspell-de-de")},
        {QStringLiteral("pt"), QStringLiteral("hunspell-pt-pt")}, {QStringLiteral("sv"), QStringLiteral("hunspell-sv")},
        {QStringLiteral("nb"), QStringLiteral("hunspell-no")}, {QStringLiteral("nn"), QStringLiteral("hunspell-no")}};
    return packages.value(code, QStringLiteral("hunspell-") + code);
}

QString userDataPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/siplasplas/spell/spelling.sqlite");
}

void moveSpellingData(const QString& from, const QString& to) {
    detail::UserStore::instance().movePath(QFileInfo(from).absoluteFilePath(), QFileInfo(to).absoluteFilePath());
}

void copySpellingData(const QString& from, const QString& to) {
    detail::UserStore::instance().copyPath(QFileInfo(from).absoluteFilePath(), QFileInfo(to).absoluteFilePath());
}

void removeSpellingData(const QString& path) {
    detail::UserStore::instance().removePath(QFileInfo(path).absoluteFilePath());
}

QString userCorrectionsPath(const QString& dictionaryName) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/siplasplas/spell/") + languageOf(dictionaryName) + QStringLiteral(".corrections");
}

QString userDictionaryPath(const QString& dictionaryName) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/siplasplas/spell/") + languageOf(dictionaryName) + QStringLiteral(".dic");
}

QString wordFrequencyListDir() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(data).filePath(QStringLiteral("word-frequencies"));
}

QString wordFrequencyListPath(const QString& language) {
    QString code = languageOf(language);
    if (code == QLatin1String("nb") || code == QLatin1String("nn")) code = QStringLiteral("no");
    return QDir(wordFrequencyListDir()).filePath(QStringLiteral("%1_50k.txt").arg(code));
}

bool hasWordFrequencies(const QString& language) {
    return !detail::frequencyPath(languageOf(language)).isEmpty()
        || QFileInfo::exists(wordFrequencyListPath(language));
}

bool buildWordFrequencies(const QString& dictionaryName, const QString& countsPath, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    const QList<Dictionary> dictionaries = installedDictionaries();
    const auto dictionary = std::find_if(dictionaries.cbegin(), dictionaries.cend(),
                                         [&](const Dictionary& d) { return d.name == dictionaryName; });
    if (dictionary == dictionaries.cend())
        return fail(QStringLiteral("The dictionary %1 is not installed").arg(dictionaryName));
    QFile in(countsPath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("Cannot read %1").arg(countsPath));

    // A private hunspell (the shared ones belong to the GUI thread).
    Loaded loaded;
    loaded.hunspell = std::make_unique<Hunspell>(QFile::encodeName(dictionary->affPath).constData(),
                                                 QFile::encodeName(dictionary->dicPath).constData());
    loaded.encoding = qtEncodingName(loaded.hunspell->get_dict_encoding());
    loaded.utf8 = loaded.encoding.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0;

    QHash<QString, double> stems;
    qint64 words = 0, known = 0;
    QTextStream stream(&in);
    while (!stream.atEnd()) {
        // "word count": the count is the last field.
        const QString line = stream.readLine().trimmed();
        const qsizetype space = line.lastIndexOf(QLatin1Char(' '));
        if (space <= 0) continue;
        const qint64 count = line.mid(space + 1).toLongLong();
        const QString word = line.left(space).trimmed();
        if (count <= 0 || word.isEmpty()) continue;
        words += count;
        const std::string encoded = loaded.encode(word);
        if (encoded.empty()) continue;
        const std::vector<std::string> found = loaded.hunspell->stem(encoded);
        if (found.empty()) continue;
        known += count;
        for (const std::string& stem : found) stems[loaded.decode(stem)] += double(count) / double(found.size());
    }
    if (known == 0) return fail(QStringLiteral("No word of %1 is in %2").arg(countsPath, dictionaryName));

    std::vector<std::pair<qint64, QString>> rows;
    rows.reserve(size_t(stems.size()));
    qint64 total = 0;
    for (auto it = stems.cbegin(); it != stems.cend(); ++it) {
        const qint64 count = std::max<qint64>(qRound64(it.value()), 1);
        rows.emplace_back(count, it.key());
        total += count;
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    // The most frequent stems only; the rest counts as very rare.
    constexpr double kKeptShare = 0.98;
    size_t kept = 0;
    for (qint64 sum = 0; kept < rows.size() && double(sum) < kKeptShare * double(total); ++kept)
        sum += rows[kept].first;

    const QString path =
        QDir(detail::frequencyCacheDir()).filePath(QStringLiteral("%1.lemma.freq").arg(languageOf(dictionaryName)));
    QDir().mkpath(detail::frequencyCacheDir());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return fail(QStringLiteral("Cannot write %1").arg(path));
    QTextStream text(&out);
    text.setEncoding(QStringConverter::Utf8);
    text << "# " << words << " words in " << QFileInfo(countsPath).fileName() << ", " << known << " known to "
         << dictionaryName << '\n';
    for (size_t i = 0; i < kept; ++i) text << rows[i].second << '\t' << rows[i].first << '\n';
    text.flush();
    if (!out.commit()) return fail(QStringLiteral("Cannot write %1").arg(path));
    return true;
}

struct SpellChecker::Private {
    QStringList names;
    QList<std::shared_ptr<Loaded>> dictionaries;
    QSet<QString> ignored;          // this session
    QString documentPath;           // whose ignored words are kept (UserStore)
    QSet<QString> keptIgnored;
    qint64 ignoredStamp = -1;
    mutable QHash<QString, bool> cache;
    mutable int cacheGeneration = -1;
    HunspellSuggestions hunspellSuggestions = HunspellSuggestions::WhenNoneFound;
    // Suggestions found before (word and count), kept while nothing they
    // depend on changes: e.g. a second review of a long text is at once.
    mutable QHash<QString, QList<Suggestion>> suggestionCache;
    mutable QString suggestionStamp;
};

SpellChecker::SpellChecker() : d(std::make_unique<Private>()) {}
SpellChecker::~SpellChecker() = default;

void SpellChecker::setLanguages(const QStringList& languages) {
    d->names.clear();
    d->dictionaries.clear();
    d->cache.clear();
    d->suggestionCache.clear();
    for (const QString& language : languages) {
        const QString name = dictionaryFor(language);
        if (name.isEmpty() || d->names.contains(name)) continue;
        if (auto loaded = registry().load(name)) {
            if (loaded->refreshUserWords()) ++registry().generation;
            d->names << name;
            d->dictionaries << loaded;
        }
    }
}

QStringList SpellChecker::dictionaries() const { return d->names; }

void SpellChecker::setHunspellSuggestions(HunspellSuggestions when) {
    d->hunspellSuggestions = when;
    d->suggestionCache.clear();
}

SpellChecker::HunspellSuggestions SpellChecker::hunspellSuggestions() const { return d->hunspellSuggestions; }

bool SpellChecker::isReady() const { return !d->dictionaries.isEmpty(); }

bool SpellChecker::check(const QString& word) const {
    if (d->dictionaries.isEmpty() || word.isEmpty() || d->ignored.contains(word) || d->keptIgnored.contains(word))
        return true;
    if (d->cacheGeneration != registry().generation) {
        d->cache.clear();
        d->cacheGeneration = registry().generation;
    }
    if (auto it = d->cache.constFind(word); it != d->cache.cend()) return it.value();
    bool right = false;
    for (const auto& loaded : d->dictionaries) {
        const std::string encoded = loaded->encode(word);
        if (!encoded.empty() && loaded->hunspell->spell(encoded)) {
            right = true;
            break;
        }
    }
    d->cache.insert(word, right);
    return right;
}

// Hunspell's suggestions of every dictionary, and the similar forms of the
// dictionaries' form indexes, ranked together by our distance; hunspell's
// order breaks ties. Without any index (still being built, or a dictionary
// it does not support) hunspell's suggestions in its order.
QStringList SpellChecker::suggestions(const QString& word, int maxCount) const {
    QStringList texts;
    for (const Suggestion& suggestion : scoredSuggestions(word, maxCount)) texts << suggestion.text;
    return texts;
}

QList<SpellChecker::Suggestion> SpellChecker::scoredSuggestions(const QString& word, int maxCount) const {
    // A correction of the user's own: the only one, sure (score 0).
    for (const auto& loaded : d->dictionaries) {
        loaded->refreshCorrections();
        if (auto it = loaded->corrections.constFind(word); it != loaded->corrections.cend())
            return {{it.value(), 0, 0}};
    }
    // Found before, unless the dictionaries (user words), the frequencies or
    // the way of finding them changed since.
    QString stamp = QString::number(registry().generation) + QLatin1Char('/')
        + QString::number(int(d->hunspellSuggestions));
    for (const auto& loaded : d->dictionaries) {
        loaded->frequencies();
        stamp += QLatin1Char('/') + QString::number(loaded->frequencyVersion);
    }
    if (stamp != d->suggestionStamp) {
        d->suggestionCache.clear();
        d->suggestionStamp = stamp;
    }
    const QString key = word + QChar(0x1f) + QString::number(maxCount);
    if (auto it = d->suggestionCache.constFind(key); it != d->suggestionCache.cend()) return it.value();
    bool indexed = false;
    QList<Suggestion> result = findSuggestions(word, maxCount, &indexed);
    // Without the forms (still being built) hunspell's alone: not kept.
    if (indexed) d->suggestionCache.insert(key, result);
    return result;
}

QList<SpellChecker::Suggestion> SpellChecker::findSuggestions(const QString& word, int maxCount, bool* indexedOut) const {
    std::vector<std::pair<double, QString>> candidates;
    bool indexed = false;
    for (const auto& loaded : d->dictionaries) {
        if (const detail::FormIndex* index = loaded->formIndex()) {
            indexed = true;
            // The user's words are not among the forms of the dictionary
            // files: looked up in the same classes ("gemnini" → an added "gemini").
            auto found = index->candidates(word, detail::kSuggestionLimit, &loaded->userClasses);
            candidates.insert(candidates.end(), std::make_move_iterator(found.begin()),
                              std::make_move_iterator(found.end()));
        }
    }
    // Hunspell's suggestions take most of the time; they may be left out
    // when the forms already have candidates.
    QStringList hunspellOrder;
    if (!indexed || candidates.empty() || d->hunspellSuggestions == HunspellSuggestions::Always) {
        for (const auto& loaded : d->dictionaries) {
            const std::string encoded = loaded->encode(word);
            if (encoded.empty()) continue;
            for (const std::string& suggestion : loaded->hunspell->suggest(encoded)) {
                const QString text = loaded->decode(suggestion);
                if (!text.isEmpty() && !hunspellOrder.contains(text)) hunspellOrder << text;
            }
        }
    }
    *indexedOut = indexed;
    if (!indexed) {
        QList<Suggestion> result;
        for (const QString& suggestion : hunspellOrder.mid(0, maxCount)) {
            const double score = detail::score(word, suggestion, detail::kSuggestionLimit);
            result.append({suggestion, score, score});
        }
        return result;
    }
    // Hunspell's own (e.g. words of the user dictionary) count with our distance too.
    QSet<QString> known;
    for (const auto& candidate : candidates) known.insert(candidate.second.toLower());
    for (const QString& suggestion : hunspellOrder) {
        if (known.contains(suggestion.toLower())) continue;
        const double dist = detail::score(word, suggestion, detail::kSuggestionLimit);
        if (dist <= detail::kSuggestionLimit) candidates.emplace_back(dist, suggestion);
    }
    // Rare words behind common ones: a penalty by the frequency of the stem
    // (none from the dictionaries without frequencies or not knowing the
    // word); a candidate split into words ("dial ac") takes its rarest
    // word's. Only the candidates that could still be shown are looked up.
    // The scores without the frequency penalty, for isConfident().
    QHash<QString, double> distances;
    for (const auto& [dist, candidate] : candidates) distances.insert(candidate.toLower(), dist);
    const bool withFrequency = hasFrequencies();
    if (withFrequency && !candidates.empty()) {
        const auto wordPenalty = [this](const QString& part) {
            const double perMillion = this->perMillion(part);
            return perMillion < 0 ? 0.0 : detail::frequencyPenalty(perMillion);
        };
        static const QRegularExpression separator(QStringLiteral("[ -]"));
        double best = candidates.front().first;
        for (const auto& candidate : candidates) best = std::min(best, candidate.first);
        const double reach = best + detail::kCloseToBest + detail::frequencyPenalty(0);
        std::erase_if(candidates, [reach](const auto& candidate) { return candidate.first > reach; });
        for (auto& [dist, candidate] : candidates) {
            double penalty = 0;
            for (const QString& part : candidate.split(separator, Qt::SkipEmptyParts))
                penalty = std::max(penalty, wordPenalty(part));
            dist += penalty;
        }
    }
    QList<Suggestion> result;
    for (const auto& [score, text] : detail::rankSuggestions(word, std::move(candidates), maxCount, hunspellOrder,
                                                        withFrequency ? detail::kCloseToBestWithFrequency
                                                                      : detail::kCloseToBest))
        result.append({text, score, distances.value(text.toLower(), score)});
    return result;
}

bool SpellChecker::isConfident(const QString& word, const QList<Suggestion>& suggestions) {
    return suggestions.size() == 1 && isClose(word, suggestions.front());
}

bool SpellChecker::isClose(const QString& word, const Suggestion& suggestion) {
    if (suggestion.distance > detail::kConfidentDistance + 1e-9 || suggestion.score > detail::kConfidentScore + 1e-9)
        return false;
    // A capital the word has not (a proper name, "serach" → Serach) is asked.
    return !(word == word.toLower() && suggestion.text != suggestion.text.toLower());
}

bool SpellChecker::hasFrequencies() const {
    for (const auto& loaded : d->dictionaries)
        if (!loaded->frequencies().isEmpty()) return true;
    return false;
}

// The most frequent of the dictionaries that have frequencies and know the word.
double SpellChecker::perMillion(const QString& word) const {
    double best = -1;
    for (const auto& loaded : d->dictionaries) best = std::max(best, loaded->perMillion(word));
    return best;
}

QStringList SpellChecker::splitSuggestions(const QString& first, const QString& second, int maxCount) const {
    const QString joined = first + second;
    const int original = int(first.size());
    // With frequencies, both words must be common: a split into rare words
    // ("sie z" → "si ez") is seldom what was meant, and a single letter must
    // be a word of its own (Polish "w", "z", "i"; not "d" of "d asie").
    const bool withFrequency = hasFrequencies();
    const auto common = [&](const QString& part) {
        if (!withFrequency) return true;
        return perMillion(part) >= (part.size() == 1 ? kPhraseLetterPerMillion : kPhraseWordPerMillion);
    };
    QStringList result;
    // The space moved by one letter, to the left first.
    for (const int split : {original - 1, original + 1}) {
        if (int(result.size()) >= maxCount) break;
        if (split < 1 || split >= joined.size()) continue;
        const QString left = joined.left(split), right = joined.mid(split);
        if (check(left) && check(right) && common(left) && common(right)) result << left + QLatin1Char(' ') + right;
    }
    return result;
}

bool SpellChecker::suggestionIndexReady() const {
    for (const auto& loaded : d->dictionaries) {
        loaded->formIndex();
        if (!loaded->index && loaded->indexState->load() != 2) return false;
    }
    return true;
}

void SpellChecker::addToDictionary(const QString& word) {
    if (d->dictionaries.isEmpty() || word.trimmed().isEmpty()) return;
    const std::shared_ptr<Loaded>& loaded = d->dictionaries.front();
    detail::UserStore::instance().addWord(loaded->language, word);
    loaded->refreshUserWords();
    ++registry().generation;
}

QStringList SpellChecker::userWords() const {
    if (d->dictionaries.isEmpty()) return {};
    const auto& loaded = d->dictionaries.front();
    loaded->refreshUserWords();
    QStringList words(loaded->userWords.cbegin(), loaded->userWords.cend());
    words.sort(Qt::CaseInsensitive);
    return words;
}

void SpellChecker::removeFromDictionary(const QString& word) {
    if (d->dictionaries.isEmpty()) return;
    const std::shared_ptr<Loaded>& loaded = d->dictionaries.front();
    detail::UserStore::instance().removeWord(loaded->language, word);
    loaded->refreshUserWords();
    ++registry().generation;
}

QList<QPair<QString, QString>> SpellChecker::userCorrections() const {
    QList<QPair<QString, QString>> result;
    if (d->dictionaries.isEmpty()) return result;
    const auto& loaded = d->dictionaries.front();
    loaded->refreshCorrections();
    for (auto it = loaded->corrections.cbegin(); it != loaded->corrections.cend(); ++it)
        result.append({it.key(), it.value()});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });
    return result;
}

void SpellChecker::addCorrection(const QString& typed, const QString& correction) {
    if (d->dictionaries.isEmpty() || typed.trimmed().isEmpty() || correction.trimmed().isEmpty()) return;
    detail::UserStore::instance().addCorrection(d->dictionaries.front()->language, typed.trimmed(), correction.trimmed());
}

void SpellChecker::removeCorrection(const QString& typed) {
    if (d->dictionaries.isEmpty()) return;
    detail::UserStore::instance().removeCorrection(d->dictionaries.front()->language, typed);
}

void SpellChecker::refreshUserDictionary() {
    for (const auto& loaded : d->dictionaries)
        if (loaded->refreshUserWords()) ++registry().generation;
    // The document's ignored words, also those another program ignored.
    if (!d->documentPath.isEmpty()) {
        const qint64 stamp = detail::UserStore::instance().stamp();
        if (stamp != d->ignoredStamp) {
            d->ignoredStamp = stamp;
            const QSet<QString> kept = detail::UserStore::instance().ignored(d->documentPath);
            if (kept != d->keptIgnored) {
                d->keptIgnored = kept;
                d->cache.clear();
            }
        }
    }
}

void SpellChecker::setDocumentPath(const QString& path) {
    d->documentPath = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    d->keptIgnored = d->documentPath.isEmpty() ? QSet<QString>()
                                                : detail::UserStore::instance().ignored(d->documentPath);
    d->ignoredStamp = detail::UserStore::instance().stamp();
    d->cache.clear();
}

QString SpellChecker::documentPath() const { return d->documentPath; }

void SpellChecker::ignore(const QString& word) {
    d->ignored.insert(word);
    d->cache.remove(word);
    if (!d->documentPath.isEmpty()) {
        detail::UserStore::instance().addIgnored(d->documentPath, word);
        d->keptIgnored.insert(word);
    }
}

QStringList SpellChecker::ignoredWords() const {
    QSet<QString> all = d->ignored;
    all.unite(d->keptIgnored);
    QStringList words(all.cbegin(), all.cend());
    words.sort(Qt::CaseInsensitive);
    return words;
}

void SpellChecker::unignore(const QString& word) {
    d->ignored.remove(word);
    d->keptIgnored.remove(word);
    d->cache.remove(word);
    if (!d->documentPath.isEmpty()) detail::UserStore::instance().removeIgnored(d->documentPath, word);
}

QList<QPair<int, int>> checkableWords(const QString& text, int start, int end) {
    QList<QPair<int, int>> words;
    start = qBound(0, start, int(text.size()));
    end = qBound(start, end, int(text.size()));
    int i = start;
    while (i < end) {
        while (i < end && text[i].isSpace()) ++i;
        const int tokenStart = i;
        while (i < end && !text[i].isSpace()) ++i;
        const QString token = text.mid(tokenStart, i - tokenStart);
        if (token.isEmpty() || isTechnicalToken(token)) continue;
        // Words inside the token: word characters, with apostrophes between
        // letters ("don't"); hyphens separate words.
        int j = 0;
        while (j < token.size()) {
            while (j < token.size() && !isWordChar(token[j])) ++j;
            const int wordStart = j;
            while (j < token.size()) {
                if (isWordChar(token[j])) ++j;
                else if (isApostrophe(token[j]) && j > wordStart && j + 1 < token.size()
                         && token[j - 1].isLetter() && token[j + 1].isLetter())
                    ++j;
                else
                    break;
            }
            if (j > wordStart && worthChecking(token.mid(wordStart, j - wordStart)))
                words.append({tokenStart + wordStart, j - wordStart});
        }
    }
    return words;
}

} // namespace qce::spell
