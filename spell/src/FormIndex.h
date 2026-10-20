#pragma once
// Suggestions from all word forms of a dictionary, found by class.
//
// A hunspell dictionary is expanded into its word forms (the affix rules of
// its .aff applied to the stems of its .dic) and kept as a cache file,
// memory-mapped, shared by all programs: each form as its stem and rules, by
// the hash of its class (see FormIndex.cpp). The forms are grouped in classes:
// the letters of a word in lower case, diacritics (Polish, French é, ç, …)
// and the AltGr characters of the Polish keyboard as their plain letters,
// sorted. Swapped
// letters, missing diacritics and a held AltGr ("ð" for "d") keep the class;
// a letter too many or too few, or a wrong one, is a neighbouring class (one
// letter less, more, or both). The forms of those few classes are ranked by a
// weighted edit distance. Checking whether a word is right stays hunspell's.

#include <QFile>
#include <QHash>
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

/// The hand that types a letter on a QWERTY keyboard (Polish diacritics and
/// AltGr characters as their keys): 0 left, 1 right, -1 not a letter key.
int handOf(QChar c);
/// Two letters whose keys touch on a QWERTY keyboard (or the same key).
bool keysTouch(QChar a, QChar b);

/// Costs of a letter missing in the typed word (inserted into it) and of a
/// letter too many (deleted from it): a letter is more often typed by the
/// way than left out, most often a key touching both meant letters around
/// it, or one of them twice ("bhyc": h between b and y), less when it touches
/// one of them.
constexpr double kInsertCost = 1.0;
constexpr double kDeleteCost = 0.9;
constexpr double kDeleteTouchingCost = 0.5;
constexpr double kDeleteTouchingOneCost = 0.7;
/// A letter typed k places too early or too late (index k). One place, two
/// adjacent letters swapped, is the usual slip when the two hands type them
/// ("po poslku": s left, l right, kSwapTwoHandsCost), less likely with one
/// hand ("serach": r and a both left, kSwapOneHandCost); two places cost more, three much more (still less than deleting and inserting
/// the letter). A move right after another adds kMoveAgainCost: two adjacent
/// swaps in a row ("dziłaja" → "działaj": "ła"/"ał", "ja"/"aj") are a letter
/// moved by three rather than two slips. A moved letter that also lacks its
/// diacritic adds the diacritic's cost ("dzielci" → "dzielić").
/// Another letter typed instead of the meant one: on a key touching it (the
/// finger slipped, "nonth" for month) costs less than any other letter.
constexpr double kNeighbourKeyCost = 1.0;
constexpr double kOtherLetterCost = 1.5;
constexpr double kSwapTwoHandsCost = 0.5;
constexpr double kSwapOneHandCost = 1.0;
constexpr double kMoveCost[] = {0, 0.5, 1.0, 1.5};
constexpr double kMoveAgainCost = 0.5;

/// Weighted Damerau–Levenshtein distance from the typed word `a` to `b`,
/// case-insensitive: a missing diacritic or AltGr difference 0.3, a typed
/// diacritic removed 0.6 (0.3 after another such letter: AltGr held), a
/// letter moved by one to three places (kMoveCost, by hands for one place),
/// a letter missing 1, one too many 0.9 (0.5 or 0.7 when its key touches the
/// meant letters around it),
/// another letter 1.5 (1 on a neighbouring key). Returns more than `limit` once it exceeds it.
double distance(const QString& a, const QString& b, double limit);

/// A capital inside the typed word that the candidate has not there: right
/// after the capitals the word begins with, Shift held a letter too long
/// ("POłączyłem" → Połączyłem); after a small letter, camelCase more likely
/// written on purpose ("PołąCzyłem", "PoŁączyłem"), so never sure. Shift
/// held over three letters (two capitals too many) is still sure, just
/// ("POŁączyłem": 0.7), over four not (1.05). AltGr held with Shift on those
/// letters, or held on after a diacritic further in the word, is the same
/// slip and costs nothing more ("ÞÓŁączyłęm": 0.7).
constexpr double kShiftHeldCost = 0.35;
constexpr double kInnerCapitalCost = 1.0;

/// distance() plus 1 for a candidate split into two words (space or hyphen),
/// 0.4 for a capital letter (a proper name) when the word has none, and
/// kShiftHeldCost or kInnerCapitalCost for each capital inside the word the
/// candidate has not.
double score(const QString& word, const QString& candidate, double limit);

/// All word forms of a dictionary; empty, with `supported` false, when its
/// .aff uses what is not implemented here (COMPLEXPREFIXES) or it has too
/// many forms (Hungarian, Galician). Flags of every FLAG kind, flag aliases
/// (AF), NEEDAFFIX, CIRCUMFIX, continuation classes (two suffixes, prefixes
/// allowed by suffixes and the other way) and forbidden words are known;
/// compound words are left to hunspell.
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
/// The same with word frequencies, which put the right word first more often:
/// still all 96 right words shown, 1.28 suggestions on average.
constexpr double kCloseToBestWithFrequency = 0.4;
constexpr int kMaxShown = 3;

/// Word frequency: the counts of dictionary entries (stems) in a corpus,
/// <language>.lemma.freq (lines "stem<TAB>count" after "# N words …, K
/// known to …" lines; made by wiki2corpus's wordfreq). Only the most frequent
/// entries are there; one that is not counts as very rare. The file is looked
/// for in <GenericDataLocation>/siplasplas/spell/freq (installed or put there
/// by hand), then in frequencyCacheDir() (downloaded); empty when neither has it.
QString frequencyPath(const QString& language);
/// <GenericCacheLocation>/siplasplas/spell/freq.
QString frequencyCacheDir();
class FrequencyTable {
public:
    /// Empty (no file, or not readable) when nothing is known.
    explicit FrequencyTable(const QString& path);
    bool isEmpty() const { return m_counts.isEmpty(); }
    /// Occurrences per million known words of the corpus; 0 when the stem is
    /// not in the table.
    double perMillion(const QString& stem) const;

private:
    QHash<QString, quint32> m_counts;
    double m_known = 0;
};

/// Added to the score of a suggestion whose stem is this frequent (per
/// million words): none from kCommonPerMillion up, kFrequencyWeight per
/// tenfold rarer, a word not in the table (very rare) as kVeryRarePerMillion.
/// On the 96 typing mistakes with counts from film subtitles: 93 right words
/// first (92 without), e.g. "dialac" → działać, not dialach; "reczy" →
/// rzeczy, not ręczy. Wikipedia's counts did worse (89): its nouns and third
/// person beat what people type ("zmianiac" → zmianach before zmieniać).
constexpr double kCommonPerMillion = 1000;
constexpr double kVeryRarePerMillion = 0.3;
constexpr double kFrequencyWeight = 0.3;
double frequencyPenalty(double perMillion);

/// Suggestions scoring more (too far from the typed word, e.g. "bookamrk" →
/// bookami) are not shown at all.
constexpr double kShownLimit = 2.6;
/// A single suggestion at most this far (kConfidentDistance, without the
/// frequency penalty) and scoring at most kConfidentScore is sure enough to
/// be applied without asking (correction while typing, checked at first in a
/// review). On the 97 typing mistakes 39 are then sure, all of them right;
/// "agents" → agent (0.9: an s touching neither t nor the end) and "search"
/// → serach (1: r and a both left hand) are only offered.
constexpr double kConfidentDistance = 0.8;
constexpr double kConfidentScore = 1.0;

/// Up to `maxCount` of the candidates with their scores, best first, only
/// those at most `closeToBest` worse than the best (a threshold, not always
/// maxCount) and at most kShownLimit; ties keep the order of `preferred`
/// (e.g. hunspell's suggestions), then the shorter. The case follows `word`
/// (Capitalized, ALL CAPS). With `distances` (spelling distances without the
/// frequency penalty, by lower-case form) a candidate spelled at most as far
/// as a shown one is shown too when the frequency table has it: frequency
/// orders such rivals but does not hide them, so a single suggestion is not
/// sure by mistake; a word missing from the table (very rare) still is.
std::vector<std::pair<double, QString>> rankSuggestions(const QString& word,
                                                        std::vector<std::pair<double, QString>> candidates,
                                                        int maxCount, const QStringList& preferred,
                                                        double closeToBest = kCloseToBest,
                                                        const QHash<QString, double>* distances = nullptr);

class FormIndex {
public:
    /// The cache file, when it exists and was built from these dictionary
    /// files as they are now; null otherwise.
    static std::unique_ptr<FormIndex> open(const QString& indexPath, const QString& affPath,
                                           const QString& dicPath);
    ~FormIndex();

    /// Forms similar to `word` (in its class and the neighbouring ones) with
    /// their distance(), at most `limit`; unordered. `extraClasses` (words by
    /// classKey(), e.g. the user's) are looked up in the same classes.
    std::vector<std::pair<double, QString>> candidates(const QString& word, double limit,
                                                       const QHash<QString, QStringList>* extraClasses = nullptr) const;

    int formCount() const;

private:
    FormIndex() = default;
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace qce::spell::detail
