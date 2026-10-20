#pragma once
// Grammar checking of nearby words by their readings (Morphology), with
// right words one typing slip away offered when they fit ("ze" written for
// "że": a preposition before a verb).
//
// Rules so far (Polish, NKJP tagset):
//  - government: a preposition before a word that has no reading in a case
//    the preposition takes (nor is an adverb);
//  - agreement: an adjective (or adjectival participle) right before a
//    noun, no reading of the two agreeing in number, case and gender; a
//    noun in a case the adjective takes as its complement is left alone
//    (learnt from a corpus: loadComplements());
//  - two finite verbs next to each other ("mam dzwoniła"), a noun one slip
//    away offered when it can be the second one's subject ("mama");
//  - a word known only as rare one diacritic from a common word.
// Readings only archaic or obsolete are left out; unknown words fit
// anything.

#include <qce/grammar/Morphology.h>
#include <qce/grammar/Tag.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qce::grammar {

/// A word or another character (punctuation, digit) of a text.
struct Token {
    std::string text;
    bool word = false;
    size_t offset = 0;  ///< in bytes, in the text
};

/// Words (letters, apostrophes between letters) and single other characters; spaces left out.
std::vector<Token> tokenize(std::string_view text);

/// A word to change, with what fits.
struct Suggestion {
    size_t token;
    std::string word;
    size_t tokens = 1;  ///< how many tokens from `token` it replaces (2: "Po jechałem" → Pojechałem)
};

struct Finding {
    size_t token;          ///< the first of the two words
    std::string rule;      ///< "government", "agreement"
    std::vector<Suggestion> suggestions;
};


/// The cases ("dat", "inst") an adjective takes as its complement, by lemma.
using Complements = std::unordered_map<std::string, uint8_t>;

/// The words of a language the rules need, from its rules file
/// (data/pl.grammar for Polish; the sections there describe each).
struct GrammarRules {
    std::u32string letters;
    std::vector<std::pair<char32_t, char32_t>> diacritics;    ///< plain, marked
    std::vector<std::pair<std::u32string, double>> keyboard;  ///< letter rows and their shifts
    std::vector<std::string> archaic, rare;                   ///< qualifiers
    std::vector<std::string> sentenceEnd, transparent;
    std::unordered_map<std::u32string, uint8_t> reflexive;    ///< form → cases
    std::vector<std::string> notAdjectives, notNouns, phraseGoesOn, coordinators, too;
    std::vector<std::pair<std::u32string, std::u32string>> notAfter;  ///< word, preposition
    std::vector<std::u32string> idioms;                              ///< "preposition word", in lower case
    std::vector<std::u32string> oneLetterWords;  ///< the words of one letter that may be offered
};

/// Reads a rules file; false, with `error` set, when it cannot be read or a
/// line is not understood.
bool loadGrammarRules(const std::string& path, GrammarRules& rules, std::string* error = nullptr);

class Grammar {
public:
    explicit Grammar(const Morphology& morphology) : m_morphology(morphology) {}
    std::vector<Finding> check(const std::vector<Token>& tokens) const;

    /// The language's words (loadGrammarRules()); without them the rules
    /// know no exceptions and offer no words.
    void setRules(GrammarRules rules) { m_rules = std::move(rules); }
    const GrammarRules& rules() const { return m_rules; }

    /// The words one typing slip away of the kinds offered: the user's
    /// choice of likely ones (a diacritic; letters swapped or a neighbouring
    /// key, the first letter kept, the latter in words of three letters or
    /// more; a letter missing or too many in the middle or at the end; a
    /// doubled letter).
    std::vector<std::string> slipVariants(std::string_view word) const;

    /// Reads "lemma<TAB>case<TAB>count" lines (nkjp-complements); then a
    /// dative or instrumental after an adjective agrees only when the
    /// adjective takes it. Without them a dative after any adjective does.
    bool loadComplements(const std::string& path, std::string* error = nullptr);

    /// Reads "form<TAB>part of speech<TAB>count" lines (nkjp-forms): then
    /// words one slip but a diacritic away are offered only when seen in
    /// the corpus, the more frequent first (after those of a diacritic;
    /// three at most), and a part of speech a form has in fewer than 2% of its
    /// uses is not taken into account where it decides ("mam" is "mieć",
    /// hardly ever "mama": a verb for the verbs rule; "w" a preposition, not
    /// an abbreviation).
    bool loadForms(const std::string& path, std::string* error = nullptr);

private:
    const Morphology& m_morphology;
    GrammarRules m_rules;
    std::unordered_map<std::string, std::vector<std::pair<std::string, long>>> m_forms;
    Complements m_complements;
    bool m_hasComplements = false;
};

} // namespace qce::grammar
