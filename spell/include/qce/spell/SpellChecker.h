#pragma once

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

/// Spell checking for qcodeedit with hunspell: the installed dictionaries,
/// checking and suggestions, a user dictionary shared by all programs, and
/// the words of a text worth checking. Mechanisms only: menus, settings and
/// dialogs are the application's (see CodeEditSpelling for the editor).
namespace qce::spell {

/// An installed hunspell dictionary.
struct Dictionary {
    QString name;        ///< file name without extension, e.g. "pl_PL", "en_US"
    QString language;    ///< its language code, e.g. "pl", "en", "cs"
    QString displayName; ///< e.g. "Polish (Poland)", for menus and dialogs
    QString affPath;
    QString dicPath;
};

/// Directories searched for dictionaries, in order: $DICPATH, the user's
/// ~/.local/share/hunspell, then /usr/share/hunspell, /usr/share/myspell and
/// /usr/share/myspell/dicts. The first dictionary of a name wins.
QStringList dictionaryDirectories();

/// The installed dictionaries, sorted by display name.
QList<Dictionary> installedDictionaries();

/// The dictionary for `language`: a dictionary name ("pl_PL") as it is, or a
/// language code, also cpg's keys ("pl" -> pl_PL, "cz" -> cs_CZ, "en" ->
/// en_US or another English one). Empty when none is installed.
QString dictionaryFor(const QString& language);

/// The Debian/Ubuntu package that would install a dictionary for `language`
/// ("de" -> "hunspell-de-de"), as a hint for the user; empty when unknown.
QString packageHint(const QString& language);

/// The shared user dictionary of a language ("pl_PL" and "pl" -> the same
/// file): <GenericDataLocation>/siplasplas/spell/<language>.dic, one word
/// per line.
QString userDictionaryPath(const QString& dictionaryName);

/// Downloaded lists of word forms with their counts, shared by all programs
/// (e.g. also for detecting a text's language): FrequencyWords'
/// <code>_50k.txt as they are, in <GenericDataLocation>/word-frequencies.
QString wordFrequencyListDir();
/// The list of a language there ("pl", "pl_PL" -> pl_50k.txt; "nb", "nn" ->
/// no_50k.txt), whether it exists or not.
QString wordFrequencyListPath(const QString& language);

/// Word frequencies put common words before rare ones in suggestions: the
/// counts of a dictionary's stems, <language>.lemma.freq, installed in
/// <GenericDataLocation>/siplasplas/spell/freq or made by
/// buildWordFrequencies() in <GenericCacheLocation>/siplasplas/spell/freq,
/// which a checker remakes in the background from the language's list in
/// wordFrequencyListDir() when it is missing or older than the list or the
/// dictionary. True when the language ("pl", "pl_PL") has the file or the list.
bool hasWordFrequencies(const QString& language);
/// Makes the frequency file of the dictionary's language from a list of word
/// forms with their counts, lines "word count" (e.g. FrequencyWords'
/// <language>_50k.txt from film subtitles): each form counts for its stems
/// (hunspell's; a form with several shares its count, one the dictionary
/// does not know is left out), and only the most frequent stems making up
/// 98 % of the counts are kept. Uses its own hunspell, so it may run in
/// another thread; checkers take the new file in at their next suggestions.
/// False, with `error` set, when the dictionary is not installed or a file
/// cannot be read or written.
bool buildWordFrequencies(const QString& dictionaryName, const QString& countsPath, QString* error = nullptr);

/// Checks words against one or more dictionaries; a word is right when any
/// of them accepts it. Dictionaries are loaded once and shared by all
/// checkers of the program, with the shared user dictionary added to them.
/// Not thread-safe: use it in the GUI thread.
class SpellChecker {
public:
    SpellChecker();
    ~SpellChecker();
    SpellChecker(const SpellChecker&) = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    /// Dictionary names or language codes (see dictionaryFor()); those not
    /// installed are skipped. E.g. {"en", "pl"} for English comments in a
    /// Polish file.
    void setLanguages(const QStringList& languages);
    /// The dictionaries in use (names), in the given order.
    QStringList dictionaries() const;
    /// At least one dictionary is in use.
    bool isReady() const;

    /// True when the word is right in any dictionary, ignored, or nothing is
    /// in use.
    bool check(const QString& word) const;
    /// Suggestions, best first and only those close to the best: three at
    /// most, often one (and at most `maxCount`). Ranked by a distance that makes swapped letters, missing Polish
    /// diacritics and held-AltGr characters cheap, over all forms of the
    /// dictionaries (a cache file in ~/.cache/siplasplas/spell, built in the
    /// background the first time) together with hunspell's suggestions;
    /// hunspell's alone until the file is ready or for a dictionary it does
    /// not support.
    QStringList suggestions(const QString& word, int maxCount = 8) const;
    /// The suggestion files of the dictionaries in use are ready (or not
    /// possible for them); starts building them when needed.
    bool suggestionIndexReady() const;
    /// Two words typed with a space missing or in the wrong place: the
    /// letters of `first` and `second` joined and split elsewhere into two
    /// right words, "w1 w2", the space moved least first (e.g. "n", "aoknie"
    /// → "na oknie"; "mam", "yz" → "mamy z"); at most `maxCount`.
    QStringList splitSuggestions(const QString& first, const QString& second, int maxCount = 3) const;

    /// Adds the word to the shared user dictionary of the first dictionary in
    /// use (and to that dictionary now, in every checker of the program).
    void addToDictionary(const QString& word);
    /// Accepts the word in this checker only (e.g. one document, for the session).
    void ignore(const QString& word);
    /// Takes in words another program added to the shared user dictionary
    /// (checks the file's time; CodeEditSpelling calls it before each pass).
    void refreshUserDictionary();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

/// The words of text[start, end) worth checking, as (column, length):
/// runs of letters (with apostrophes and hyphens inside). Skipped: words
/// with digits or underscores, ALL-CAPS words, camelCase/PascalCase
/// identifiers with an inner capital, single letters, and whole tokens that
/// look like a URL, e-mail address, file path, "a.b" member access or "::"
/// / "->" expression.
QList<QPair<int, int>> checkableWords(const QString& text, int start, int end);

} // namespace qce::spell
