#pragma once
// Suggestions from all word forms of a dictionary, found by class.
//
// A hunspell dictionary is expanded into its word forms (the affix rules of
// its .aff applied to the stems of its .dic) and kept as a cache file,
// memory-mapped, shared by all programs. The forms are grouped in classes:
// the letters of a word in lower case, Polish diacritics and the AltGr
// characters of the Polish keyboard as their plain letters, sorted. Swapped
// letters, missing diacritics and a held AltGr ("ð" for "d") keep the class;
// a letter too many or too few, or a wrong one, is a neighbouring class (one
// letter less, more, or both). The forms of those few classes are ranked by a
// weighted edit distance. Checking whether a word is right stays hunspell's.

#include <QFile>
#include <QString>
#include <QStringList>

#include <memory>
#include <utility>
#include <vector>

namespace qce::spell::detail {

/// The letter a character stands for: lower case, without a Polish diacritic
/// (ą → a), and an AltGr character of the Polish keyboard as the letter of
/// its key (ð → d, © → r, ß → t, œ → w, …). Other characters as they are, in
/// lower case.
QChar foldChar(QChar c);
/// The class of a word: foldChar() of its letters, sorted.
QString classKey(const QString& word);

/// Costs of a letter missing in the typed word (inserted into it) and of a
/// letter too many (deleted from it): letters are left out a bit more often.
constexpr double kInsertCost = 0.9;
constexpr double kDeleteCost = 1.0;

/// Weighted Damerau–Levenshtein distance from the typed word `a` to `b`,
/// case-insensitive: a missing diacritic or AltGr difference 0.3, a typed
/// diacritic removed 0.6 (0.3 after another such letter: AltGr held), two
/// adjacent letters swapped 0.5, a letter inserted 0.9 or deleted 1, another
/// letter 1.5. Returns more than `limit` once it exceeds it.
double distance(const QString& a, const QString& b, double limit);

/// distance() plus 1 for a candidate split into two words (space or hyphen)
/// and 0.4 for a capital letter (a proper name) when the word has none.
double score(const QString& word, const QString& candidate, double limit);

/// All word forms of a dictionary; empty, with `supported` false, when its
/// .aff uses what is not implemented here (e.g. long or numeric flags, flag
/// aliases, NEEDAFFIX, CIRCUMFIX, continuation classes).
QStringList expandDictionary(const QString& affPath, const QString& dicPath, bool* supported);

/// The cache file of a dictionary's forms:
/// <GenericCacheLocation>/siplasplas/spell/<name>.forms.
QString formIndexPath(const QString& dictionaryName);

/// Builds the cache file (written atomically); false when the dictionary is
/// not supported or the file cannot be written.
bool buildFormIndex(const QString& affPath, const QString& dicPath, const QString& indexPath);

/// Distance limit for suggestions (about two typing mistakes).
constexpr double kSuggestionLimit = 2.6;
/// Suggestions shown: those at most this much worse than the best, at most
/// kMaxShown. On 96 real typing mistakes the right word is then always among
/// them (a right one may be 0.6 behind a real word closer in spelling, e.g.
/// "dialac": dialach, dialag, działać), 1.77 suggestions on average, one for
/// half of the words; with 1.2 and 8 it was 5.5.
constexpr double kCloseToBest = 0.6;
constexpr int kMaxShown = 3;

/// Up to `maxCount` of the candidates, best first, only those close to the
/// best (a threshold, not always maxCount); ties keep the order of
/// `preferred` (e.g. hunspell's suggestions), then the shorter. The case
/// follows `word` (Capitalized, ALL CAPS).
QStringList rankSuggestions(const QString& word, std::vector<std::pair<double, QString>> candidates,
                            int maxCount, const QStringList& preferred);

class FormIndex {
public:
    /// The cache file, when it exists and was built from these dictionary
    /// files as they are now; null otherwise.
    static std::unique_ptr<FormIndex> open(const QString& indexPath, const QString& affPath,
                                           const QString& dicPath);
    ~FormIndex();

    /// Forms similar to `word` (in its class and the neighbouring ones) with
    /// their distance(), at most `limit`; unordered.
    std::vector<std::pair<double, QString>> candidates(const QString& word, double limit) const;

    int formCount() const;

private:
    FormIndex() = default;
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace qce::spell::detail
