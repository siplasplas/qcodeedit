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

/// The letter a character stands for: lower case, without a diacritic or
/// stroke (ą → a, é → e, ł → l), and a character typed with AltGr on a letter
/// key of the keyboard layout as that key's letter (on the Polish keyboard ð →
/// d, © → r, ß → t, œ → w, …). Other characters as they are, in lower case.
QChar foldChar(QChar c);

/// Keyboard layouts known (KeyboardLayouts.inc, made from the X keyboard
/// layouts by tools/gen_keyboard_layouts.py): "pl", "us", "de", "fr", "es",
/// and "pl-windows" (Windows' Polish programmers: AltGr gives only Polish
/// letters). The layout tells which keys neighbour (QWERTY, QWERTZ, AZERTY)
/// and what AltGr held on a key types.
QStringList keyboardLayouts();
/// The layout a text in `language` ("pl", "pl_PL") is typed on: the one
/// setKeyboardLayout() chose; else one of the desktop's layouts for that
/// language, else the desktop's first (an English text on a Polish keyboard);
/// else the language's own ("pl", on Windows "pl-windows"), else "us".
QString keyboardLayoutFor(const QString& language);
/// The layout in use here and now (KeyboardScope, setKeyboardLayout(), else
/// the desktop's first).
QString keyboardLayout();
/// The one setKeyboardLayout() chose, or empty (by language).
QString forcedKeyboardLayout();
/// One layout for every text; empty for by language again. False for an
/// unknown name.
bool setKeyboardLayout(const QString& name);
/// The layout used by foldChar(), classKey(), distance() and score() in this
/// thread while it lives.
class KeyboardScope {
public:
    explicit KeyboardScope(const QString& layout);
    ~KeyboardScope();
    KeyboardScope(const KeyboardScope&) = delete;
    KeyboardScope& operator=(const KeyboardScope&) = delete;

private:
    int m_previous;
};
/// Not a letter, but typed on a letter key with AltGr (© on r) on the
/// layout in use or one of the desktop's: a slip inside a word, so it does
/// not end the word.
bool typedOnLetterKey(QChar c);
/// Punctuation AltGr types on a letter key („ on v, ” on b of the Polish
/// keyboard): after a letter typed with AltGr, AltGr not let go ("zró”"),
/// unless it closes a quotation.
bool punctuationOnLetterKey(QChar c);
/// Any character AltGr types on a letter key of the layout in use or the
/// desktop's.
bool onLetterKey(QChar c);
/// The class of a word: foldChar() of its letters, sorted.
QString classKey(const QString& word);

/// The hand that types a letter on a QWERTY keyboard (Polish diacritics and
/// AltGr characters as their keys): 0 left, 1 right, -1 not a letter key.
int handOf(QChar c);
/// Two letters whose keys touch on a QWERTY keyboard (or the same key).
bool keysTouch(QChar a, QChar b);

// The costs and limits below are variables, not constants, so that
// tools/spelltune can fit them to real typing mistakes; programs leave
// them as they are. The values were fitted to 772 mistakes of a Polish text
// typed by hand (docs/SPELL.md); figures in the comments measured before
// that may differ a little.

/// Costs of a letter missing in the typed word (inserted into it) and of a
/// letter too many (deleted from it): a letter is more often typed by the
/// way than left out, most often a key touching both meant letters around
/// it, or one of them twice ("bhyc": h between b and y), less when it touches
/// one of them.
inline double kInsertCost = 0.90;
inline double kDeleteCost = 1.20;
inline double kDeleteTouchingCost = 0.70;
inline double kDeleteTouchingOneCost = 0.90;
/// Added to kInsertCost when the missing letter is the word's first: a
/// word's beginning is seldom left out.
inline double kInsertAtStartCost = 0.10;
/// A missing diacritic ("zolw" for żółw), one typed by mistake (removed),
/// and a moved letter that also lacks its diacritic ("dzielci" → dzielić).
inline double kMissingDiacriticCost = 0.35;
inline double kTypedDiacriticCost = 0.70;
inline double kMovedDiacriticCost = 0.20;
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
inline double kNeighbourKeyCost = 1.10;
inline double kOtherLetterCost = 1.5;
inline double kSwapTwoHandsCost = 0.60;
inline double kSwapOneHandCost = 1.10;
inline double kMoveCost[] = {0, 0.5, 1.1, 1.6};
inline double kMoveAgainCost = 0.5;

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
inline double kShiftHeldCost = 0.35;
inline double kInnerCapitalCost = 1.0;
/// score(): a candidate split into two words, and one with a capital the
/// typed word has not (a proper name).
inline double kSplitCost = 1.0;
inline double kNewCapitalCost = 0.4;

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

/// The cache file of a dictionary's forms for a keyboard layout:
/// <GenericCacheLocation>/siplasplas/spell/<name>-<layout>.forms.
QString formIndexPath(const QString& dictionaryName, const QString& layout);

/// Builds the cache file (written atomically); false when the dictionary is
/// not supported or the file cannot be written.
bool buildFormIndex(const QString& affPath, const QString& dicPath, const QString& indexPath);

/// Distance limit for suggestions (about two typing mistakes).
inline double kSuggestionLimit = 3.4;
/// Suggestions shown: those at most this much worse than the best, at most
/// kMaxShown. On 96 real typing mistakes the right word is then always among
/// them (a right one may be 0.6 behind a real word closer in spelling, e.g.
/// "dialac": dialach, dialag, działać), 1.77 suggestions on average, one for
/// half of the words; with 1.2 and 8 it was 5.5.
inline double kCloseToBest = 0.6;
/// The same with word frequencies, which put the right word first more often:
/// still all 96 right words shown, 1.28 suggestions on average.
inline double kCloseToBestWithFrequency = 0.30;
inline int kMaxShown = 3;

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
inline double kCommonPerMillion = 1000;
inline double kVeryRarePerMillion = 0.3;
inline double kFrequencyWeight = 0.25;
/// A rival spelled as close as a shown suggestion is shown too when at most
/// 100 times rarer (its penalty at most kFrequencyWeight · log10(100) above).
inline double kRivalPenaltyGap = 0.80;
double frequencyPenalty(double perMillion);

/// Suggestions scoring more (too far from the typed word, e.g. "bookamrk" →
/// bookami) are not shown at all.
inline double kShownLimit = 3.40;
/// When nothing scores within kShownLimit, the suggestions up to this one
/// are shown instead (never sure): a far word rather than none.
inline double kFallbackShownLimit = 2.6;
/// A single suggestion at most this far (kConfidentDistance, without the
/// frequency penalty) and scoring at most kConfidentScore is sure enough to
/// be applied without asking (correction while typing, checked at first in a
/// review). On the 97 typing mistakes 39 are then sure, all of them right;
/// "agents" → agent (0.9: an s touching neither t nor the end) and "search"
/// → serach (1: r and a both left hand) are only offered.
inline double kConfidentDistance = 0.8;
inline double kConfidentScore = 1.0;

/// Up to `maxCount` of the candidates with their scores, best first, only
/// those at most `closeToBest` worse than the best (a threshold, not always
/// maxCount) and at most kShownLimit; ties keep the order of `preferred`
/// (e.g. hunspell's suggestions), then the shorter. The case follows `word`
/// (Capitalized, ALL CAPS). With `distances` (spelling distances without the
/// frequency penalty, by lower-case form) a candidate spelled at most as far
/// as a shown one is shown too unless its frequency penalty is more than
/// kRivalPenaltyGap above the best's: frequency orders such rivals but does
/// not hide them, so a single suggestion is not sure by mistake; a word
/// hundreds of times rarer still is hidden.
std::vector<std::pair<double, QString>> rankSuggestions(const QString& word,
                                                        std::vector<std::pair<double, QString>> candidates,
                                                        int maxCount, const QStringList& preferred,
                                                        double closeToBest = kCloseToBest,
                                                        const QHash<QString, double>* distances = nullptr,
                                                        double shownLimit = kShownLimit);

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
