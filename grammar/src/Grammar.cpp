#include <qce/grammar/Grammar.h>
#include <qce/grammar/Tag.h>
#include <qce/grammar/Text.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>

namespace qce::grammar {

namespace {

// --- Readings --------------------------------------------------------------

constexpr uint8_t Nom = TagValues::Nom, Gen = TagValues::Gen, Dat = TagValues::Dat, Acc = TagValues::Acc,
                  Inst = TagValues::Inst;

// A reading as the rules see it: its lemma and tag values.
struct Parsed : TagValues {
    std::string lemma;
    bool usual = true;  // a part of speech the form has often enough in a corpus (loadForms())
};

Parsed parse(const Reading& reading) {
    Parsed parsed;
    static_cast<TagValues&>(parsed) = tagValues(reading.tag);
    parsed.lemma = reading.lemma;
    return parsed;
}

bool contains(const std::vector<std::string>& list, std::string_view item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

// One of the qualifiers ("daw.|rzad.": each joined by "|") is in the list,
// also with more after "_" ("daw._dziś_gwar.").
bool qualifiedBy(std::string_view qualifiers, const std::vector<std::string>& list) {
    for (std::string_view rest = qualifiers; !rest.empty();) {
        const size_t bar = rest.find('|');
        const std::string_view q = rest.substr(0, bar);
        for (const std::string& item : list)
            if (q.substr(0, item.size()) == item && (q.size() == item.size() || q[item.size()] == '_')) return true;
        rest = bar == std::string_view::npos ? std::string_view() : rest.substr(bar + 1);
    }
    return false;
}

struct Word {
    bool word = false;
    bool known = false;  // has readings
    bool rare = false;   // all its readings rare
    bool capital = false;
    bool name = false;     // capitalized and known so (not only as a word at a sentence's start)
    bool acronym = false;  // in capitals
    bool sentenceStart = false;
    bool finiteVerb = false;  // all its readings, or all it usually has, finite verbs
    bool frequent = false;    // seen often in the corpus of loadForms()
    std::vector<Parsed> readings;
};

bool isNominal(const std::string& pos) {
    return pos == "subst" || pos == "depr" || pos == "adj" || pos == "pact" || pos == "ppas" || pos == "ger"
        || pos == "num" || pos == "ppron12" || pos == "ppron3" || pos == "siebie";
}

bool isAdjectival(const GrammarRules& rules, const Parsed& r) {
    if (contains(rules.notAdjectives, r.lemma)) return false;
    return r.pos == "adj" || r.pos == "pact" || r.pos == "ppas";
}

bool isNoun(const GrammarRules& rules, const Parsed& r) {
    if (contains(rules.notNouns, r.lemma)) return false;
    return r.pos == "subst" || r.pos == "depr";
}

// --- Rules -----------------------------------------------------------------

// Whether a word can stand after a preposition taking `cases`: yes, no, or
// the word after decides: any word of the phrase after a possessive ("przy
// jego domu", "pod której opieką"), an adjective or numeral after an adverb
// or particle ("ze zbyt dużym").
enum class Fit { Yes, No, Phrase, Adjective };

Fit fitsPreposition(const GrammarRules& rules, const Word& next, uint8_t cases, bool too, bool vocalic,
                    bool adjectiveOnly) {
    Fit further = Fit::No;
    for (const Parsed& r : next.readings) {
        const bool adjectival = r.pos == "adj" || r.pos == "pact" || r.pos == "ppas" || r.pos == "num";
        if (adjectiveOnly && !adjectival) continue;
        // A post-prepositional adjective in a case it takes too ("po polsku",
        // "od dawna"; not "do angielsku").
        if (((isNominal(r.pos) || r.pos == "adjp") && (r.cases & cases)) || r.pos == "num") return Fit::Yes;
        if (adjectiveOnly) continue;
        // Parts of fixed phrases ("za młodu", "ad hoc"), and a conjunction
        // between prepositions ("przed i po wojnie").
        const bool coordinator = r.pos == "conj" && contains(rules.coordinators, r.lemma);
        if (coordinator || (r.usual && (r.pos == "brev" || r.pos == "frag" || r.pos == "interj")))
            return Fit::Yes;
        if (too && (r.pos == "adj" || r.pos == "pact" || r.pos == "ppas")) return Fit::Yes;
        if ((r.pos == "adv" || r.pos == "prep") && !vocalic) return Fit::Yes;
        if ((r.pos == "ppron3" || contains(rules.phraseGoesOn, r.lemma)) && (r.cases & Gen)) further = Fit::Phrase;
        else if ((r.pos == "adv" || r.pos == "part") && further == Fit::No)
            further = vocalic ? Fit::Adjective : Fit::Phrase;
    }
    return further;
}

// A preposition before a word with no reading in a case it takes, nor a
// numeral ("ze dwadzieścia": about twenty), an adverb or another
// preposition ("do około"). The forms with "e" ("ze", "we", "ode") stand
// before a noun phrase only: an adverb or particle after them only before
// an adjective ("ze zbyt dużym", but not "ze teraz"). After a possessive
// the phrase goes on ("przy jego domu"); after an adverb or particle too,
// but for the forms with "e" ("w niespełna rok"). A preposition that is
// also a particle of quantity ("około", "ponad") may stand before any case
// ("ponad połowa"); idioms keep old forms ("za mąż"). With corpus counts
// (loadForms()) the readings a word seldom has do not keep it from being a
// preposition ("w" as an abbreviation). Only words whose readings are all
// prepositions (or particles, as "ze" has one) count. Not checked before an
// abbreviation or a capital letter ("przy ul.", "pod K."), a capitalized
// word (names of streets, titles), after a word that makes it no
// preposition ("co za dzień"), and for a preposition meaning "too" before
// an adjective or adverb ("za duży").
bool governmentBroken(const GrammarRules& rules, const std::vector<Token>& tokens, const std::vector<Word>& words,
                      size_t k) {
    const Word& prep = words[k];
    uint8_t cases = 0;
    bool too = false, vocalic = false, particle = false;
    for (const Parsed& r : prep.readings) {
        if (!r.usual) continue;
        if (r.pos == "prep") cases |= r.cases;
        else if (r.pos != "part") return false;
        too = too || contains(rules.too, r.lemma);
        vocalic = vocalic || (r.pos == "prep" && r.vocalic);
        particle = particle || r.pos == "part";
    }
    if (cases == 0 || prep.acronym) return false;
    if (particle && !vocalic) cases = 0x7F;
    const std::u32string preposition = toLower(fromUtf8(tokens[k].text));
    const std::u32string pair = preposition + U" " + toLower(fromUtf8(tokens[k + 1].text));
    if (std::find(rules.idioms.begin(), rules.idioms.end(), pair) != rules.idioms.end()) return false;
    if (k > 0 && words[k - 1].word) {
        const std::u32string before = toLower(fromUtf8(tokens[k - 1].text));
        for (const auto& [word, prepositionAfter] : rules.notAfter)
            if (before == word && preposition == prepositionAfter) return false;
    }
    bool adjectiveOnly = false;
    for (size_t n = k + 1; n < k + 3 && n < words.size(); ++n) {
        const Word& next = words[n];
        if (!next.word || !next.known || next.capital) return false;
        const std::u32string nextText = fromUtf8(tokens[n].text);
        const bool abbreviation = (nextText.size() == 1 && next.capital)
            || (nextText.size() <= 4 && n + 1 < tokens.size() && tokens[n + 1].text == ".");
        if (abbreviation) return false;
        const Fit fit = fitsPreposition(rules, next, cases, too, vocalic, adjectiveOnly);
        if (fit == Fit::Yes) {
            // An adjective in the case: the noun after it must be in it too
            // ("w przeciwną stroną": "w" takes no instrumental).
            const bool adjectiveOnlyReadings = !next.readings.empty()
                && std::all_of(next.readings.begin(), next.readings.end(), [&](const Parsed& r) { return isAdjectival(rules, r); });
            if (adjectiveOnlyReadings && n + 1 < words.size() && words[n + 1].word && words[n + 1].known
                && !words[n + 1].capital) {
                const Word& noun = words[n + 1];
                const bool allNouns = std::all_of(noun.readings.begin(), noun.readings.end(),
                                                  [&](const Parsed& r) { return isNoun(rules, r); });
                // The cases in which they agree (one phrase), none taken by the preposition.
                uint8_t agreed = 0;
                for (const Parsed& a : next.readings)
                    for (const Parsed& r : noun.readings)
                        if ((a.numbers & r.numbers) && (a.genders & r.genders)) agreed |= a.cases & r.cases;
                if (allNouns && agreed && !(agreed & cases)) return true;
            }
            return false;
        }
        if (fit == Fit::No) return true;
        adjectiveOnly = fit == Fit::Adjective;
    }
    return false;
}

// An adjective right before a noun, no readings agreeing in number, case
// and gender. Only words whose readings are all adjectival, and all nouns,
// count. Not broken by a noun that can be the adjective's complement or an
// attribute of a noun before: a genitive after any adjective ("sukienkę
// czerwoną siostry"); a dative or instrumental the adjective takes
// ("wdzięczny ojcu": learnt complements; without them a dative after any
// adjective), or after a participle ("zachwycony widokiem"); an accusative
// after an active participle ("czytająca książkę"). Nor when the adjective
// agrees with a noun right before it, whose it is: the noun after begins
// another phrase ("w latach osiemdziesiątych ballada"). A feminine
// adjective agrees with a masculine personal noun in the singular: a woman
// of a profession named in the masculine ("amerykańska inżynier"). Nor by a
// capitalized noun in the nominative after a small adjective (a title or
// name: "statek pasażerski Batory"), an acronym, a capitalized adjective
// inside a sentence (a name: "Bank Narodowy"), or a pronoun ("podobny
// sobie").
constexpr uint8_t kPersonal = 1, kFeminine = 8;  // m1, f

bool agreementBroken(const GrammarRules& rules, const std::vector<Word>& words, size_t k,
                     const Complements* complements) {
    const Word& adjective = words[k];
    const Word& noun = words[k + 1];
    if (!adjective.known || !noun.known || adjective.readings.empty() || noun.readings.empty()) return false;
    if (noun.acronym || (adjective.capital && !adjective.sentenceStart)) return false;
    bool active = false, passive = false;
    for (const Parsed& a : adjective.readings) {
        if (!isAdjectival(rules, a)) return false;
        active = active || a.pos == "pact";
        passive = passive || a.pos == "ppas";
    }
    uint8_t nounCases = 0;
    for (const Parsed& n : noun.readings) {
        if (!isNoun(rules, n)) return false;
        nounCases |= n.cases;
    }
    uint8_t taken = Gen;
    if (active || passive) taken |= Dat | Inst;
    if (active) taken |= Acc;
    if (!complements) {
        taken |= Dat;
    } else {
        for (const Parsed& a : adjective.readings)
            if (const auto it = complements->find(a.lemma); it != complements->end()) taken |= it->second;
    }
    if (nounCases & taken) return false;
    if (noun.capital && !adjective.capital && (nounCases & Nom)) return false;
    const auto agree = [&](const Word& other) {
        for (const Parsed& a : adjective.readings)
            for (const Parsed& n : other.readings)
                if (isNoun(rules, n) && (a.numbers & n.numbers) && (a.cases & n.cases)
                    && ((a.genders & n.genders) || ((a.genders & kFeminine) && (n.genders & kPersonal) && (n.numbers & 1))))
                    return true;
        return false;
    };
    if (agree(noun)) return false;
    return !(k > 0 && words[k - 1].word && agree(words[k - 1]));
}

// Two finite verbs next to each other, nothing between ("mam dzwoniła":
// the mother called), not the same word twice. The forms with "będzie" are
// not of these parts of speech ("będzie dzwoniła"); names and acronyms are
// left alone ("Czeka" the secret police). Reported only with a subject one
// slip away (below): spoken words between verbs ("wiesz", "znaczy") and
// missing commas are common.
bool verbsBroken(const std::vector<Token>& tokens, const std::vector<Word>& words, size_t k) {
    return words[k].finiteVerb && words[k + 1].finiteVerb && tokens[k].text != tokens[k + 1].text
        && !words[k].name && !words[k].acronym && !(words[k].capital && !words[k].sentenceStart);
}

// A subject for a verb: a noun or pronoun in the nominative agreeing with
// it in number, and in gender with a past form.
bool subjectFits(const Word& subject, const Word& verb) {
    for (const Parsed& s : subject.readings) {
        if (!((s.pos == "subst" || s.pos == "depr" || s.pos == "ppron12" || s.pos == "ppron3") && (s.cases & Nom)))
            continue;
        for (const Parsed& v : verb.readings)
            if ((s.numbers & v.numbers) && (v.genders == 0 || (s.genders & v.genders))) return true;
    }
    return false;
}

// The rule two words next to each other break, or none.
const char* brokenRule(const GrammarRules& rules, const std::vector<Token>& tokens, const std::vector<Word>& words,
                       size_t k, const Complements* complements) {
    if (k + 1 >= words.size() || !words[k].word || !words[k + 1].word) return nullptr;
    if (governmentBroken(rules, tokens, words, k)) return "government";
    if (agreementBroken(rules, words, k, complements)) return "agreement";
    if (verbsBroken(tokens, words, k)) return "verbs";
    return nullptr;
}

// Parts of speech named alike in NKJP and PoliMorf (not "cond", which NKJP
// splits into a past form and "by").
bool comparablePart(const std::string& pos) {
    static const std::string_view parts[] = {"subst", "depr", "adj",  "adv",  "fin",    "praet", "impt",  "inf",
                                             "ger",   "pact", "ppas", "prep", "conj",   "comp",  "part",  "num",
                                             "ppron12", "ppron3", "interj", "pred", "brev", "imps", "pcon", "pant",
                                             "adjp",  "winien"};
    for (const std::string_view p : parts)
        if (pos == p) return true;
    return false;
}

// The most words offered for a finding.
constexpr size_t kMaxSuggestions = 3;

// A part of speech a form has less often than this in a corpus is left out.
constexpr double kSeldomShare = 0.02;

// --- Typing slips ----------------------------------------------------------

// A letter without its diacritic (the marked letters of the rules' pairs).
char32_t baseLetter(const GrammarRules& rules, char32_t c) {
    for (int step = 0; step < 4; ++step) {
        bool changed = false;
        for (const auto& [plain, marked] : rules.diacritics)
            if (c == marked) {
                c = plain;
                changed = true;
                break;
            }
        if (!changed) break;
    }
    return c;
}

// The key of a letter: row, and column in key widths.
bool keyOf(const GrammarRules& rules, char32_t c, int& row, double& column) {
    c = baseLetter(rules, c);
    for (size_t r = 0; r < rules.keyboard.size(); ++r)
        if (const size_t k = rules.keyboard[r].first.find(c); k != std::u32string::npos) {
            row = int(r);
            column = rules.keyboard[r].second + double(k);
            return true;
        }
    return false;
}

bool keysTouch(const GrammarRules& rules, char32_t a, char32_t b) {
    int ra, rb;
    double ca, cb;
    if (baseLetter(rules, a) == baseLetter(rules, b) || !keyOf(rules, a, ra, ca) || !keyOf(rules, b, rb, cb)
        || std::abs(ra - rb) > 1)
        return false;
    return std::abs(ca - cb) < (ra == rb ? 1.1 : 0.9);
}

// Two words differing in diacritics only.
bool diacriticsOnly(const GrammarRules& rules, std::string_view a, std::string_view b) {
    const std::u32string x = toLower(fromUtf8(a)), y = toLower(fromUtf8(b));
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (baseLetter(rules, x[i]) != baseLetter(rules, y[i])) return false;
    return true;
}

// The fields of a rules line, split at spaces.
std::vector<std::string> fieldsOf(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (const char c : line) {
        if (c == ' ' || c == '\t') {
            if (!field.empty()) fields.push_back(std::move(field));
            field.clear();
        } else {
            field += c;
        }
    }
    if (!field.empty()) fields.push_back(std::move(field));
    return fields;
}

} // namespace


bool loadGrammarRules(const std::string& path, GrammarRules& rules, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    rules = GrammarRules();
    std::string section, line;
    for (int number = 1; std::getline(in, line); ++number) {
        if (const size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        const std::vector<std::string> fields = fieldsOf(line);
        if (fields.empty()) continue;
        const auto fail = [&](const char* what) {
            if (error) *error = path + ":" + std::to_string(number) + ": " + what;
            return false;
        };
        if (fields.size() == 1 && fields[0].size() > 2 && fields[0].front() == '[' && fields[0].back() == ']') {
            section = fields[0].substr(1, fields[0].size() - 2);
            continue;
        }
        const auto addAll = [&](std::vector<std::string>& list) { list.insert(list.end(), fields.begin(), fields.end()); };
        if (section == "letters") {
            for (const std::string& f : fields) rules.letters += fromUtf8(f);
        } else if (section == "diacritics") {
            for (const std::string& f : fields) {
                const std::u32string pair = fromUtf8(f);
                if (pair.size() != 2) return fail("a diacritic pair is two letters");
                rules.diacritics.emplace_back(pair[0], pair[1]);
            }
        } else if (section == "keyboard") {
            if (fields.size() != 2) return fail("a keyboard row and its shift");
            rules.keyboard.emplace_back(fromUtf8(fields[0]), std::atof(fields[1].c_str()));
        } else if (section == "archaic") {
            addAll(rules.archaic);
        } else if (section == "rare") {
            addAll(rules.rare);
        } else if (section == "sentence-end") {
            addAll(rules.sentenceEnd);
        } else if (section == "transparent") {
            addAll(rules.transparent);
        } else if (section == "reflexive") {
            if (fields.size() != 2) return fail("a form and its cases");
            const uint8_t cases = tagValues("x:" + fields[1]).cases;
            if (!cases) return fail("no case");
            rules.reflexive[toLower(fromUtf8(fields[0]))] = cases;
        } else if (section == "not-adjectives") {
            addAll(rules.notAdjectives);
        } else if (section == "not-nouns") {
            addAll(rules.notNouns);
        } else if (section == "phrase-goes-on") {
            addAll(rules.phraseGoesOn);
        } else if (section == "coordinators") {
            addAll(rules.coordinators);
        } else if (section == "too") {
            addAll(rules.too);
        } else if (section == "not-after") {
            if (fields.size() != 2) return fail("a word and a preposition");
            rules.notAfter.emplace_back(toLower(fromUtf8(fields[0])), toLower(fromUtf8(fields[1])));
        } else if (section == "one-letter-words") {
            for (const std::string& f : fields) rules.oneLetterWords.push_back(toLower(fromUtf8(f)));
        } else if (section == "idioms") {
            if (fields.size() != 2) return fail("a preposition and a word");
            rules.idioms.push_back(toLower(fromUtf8(fields[0] + " " + fields[1])));
        } else {
            return fail("not in a known section");
        }
    }
    return true;
}

bool Grammar::loadComplements(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    m_complements.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        const size_t tab2 = line.find('\t', tab + 1);
        if (tab == std::string::npos) continue;
        const std::string caseName = line.substr(tab + 1, tab2 == std::string::npos ? std::string::npos : tab2 - tab - 1);
        m_complements[line.substr(0, tab)] |= tagValues("x:" + caseName).cases;
    }
    m_hasComplements = true;
    return true;
}

bool Grammar::loadForms(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    m_forms.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t'), tab2 = line.find('\t', tab + 1);
        if (tab2 == std::string::npos) continue;
        m_forms[line.substr(0, tab)].emplace_back(line.substr(tab + 1, tab2 - tab - 1), std::atol(line.c_str() + tab2 + 1));
    }
    return true;
}

std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    const std::u32string chars = fromUtf8(text);
    size_t offset = 0;  // bytes before chars[i]
    const auto bytes = [](char32_t c) -> size_t { return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4; };
    for (size_t i = 0; i < chars.size();) {
        const char32_t c = chars[i];
        if (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0xA0) {
            offset += bytes(c);
            ++i;
            continue;
        }
        Token token;
        token.offset = offset;
        size_t end = i + 1;
        if (isLetter(c)) {
            while (end < chars.size()
                   && (isLetter(chars[end])
                       || ((chars[end] == U'\'' || chars[end] == U'’') && end + 1 < chars.size()
                           && isLetter(chars[end + 1]))))
                ++end;
            token.word = true;
        }
        const std::u32string part = chars.substr(i, end - i);
        token.text = toUtf8(part);
        for (const char32_t p : part) offset += bytes(p);
        tokens.push_back(std::move(token));
        i = end;
    }
    return tokens;
}

std::vector<std::string> Grammar::slipVariants(std::string_view word) const {
    const GrammarRules& rules = m_rules;
    const std::u32string original = fromUtf8(word);
    const std::u32string w = toLower(original);
    const bool capital = !original.empty() && original[0] != w[0];
    std::set<std::u32string> variants;
    for (size_t i = 0; i < w.size(); ++i)
        for (const auto& [a, b] : rules.diacritics)
            if (w[i] == a || w[i] == b) {
                std::u32string v = w;
                v[i] = w[i] == a ? b : a;
                variants.insert(v);
            }
    for (size_t i = 1; i + 1 < w.size(); ++i)
        if (w[i] != w[i + 1]) {
            std::u32string v = w;
            std::swap(v[i], v[i + 1]);
            variants.insert(v);
        }
    if (w.size() >= 3)
        for (size_t i = 1; i < w.size(); ++i)
            for (const char32_t c : rules.letters)
                if (c == baseLetter(rules, c) && keysTouch(rules, w[i], c)) {
                    std::u32string v = w;
                    v[i] = c;
                    variants.insert(v);
                }
    for (size_t i = 1; i < w.size(); ++i) variants.insert(w.substr(0, i) + w.substr(i + 1));
    for (size_t i = 1; i <= w.size(); ++i)
        for (const char32_t c : rules.letters) variants.insert(w.substr(0, i) + c + w.substr(i));
    variants.erase(w);
    std::vector<std::string> result;
    for (std::u32string v : variants) {
        if (v.empty()) continue;
        if (capital) v[0] = toUpper(v[0]);
        result.push_back(toUtf8(v));
    }
    return result;
}

std::vector<Finding> Grammar::check(const std::vector<Token>& tokens) const {
    // The readings of a word: current ones, not rare unless it has no other;
    // forms of compounds ("polsko-") only before a hyphen; past forms only
    // before "-em" never alone.
    const auto analyse = [&](size_t index, const std::string& text) {
        Word word;
        word.word = tokens[index].word;
        if (!word.word) return word;
        std::vector<Reading> readings = m_morphology.analyse(text);
        const std::u32string chars = fromUtf8(text);
        const std::u32string lower = toLower(chars);
        word.capital = !chars.empty() && chars[0] != lower[0];
        word.acronym = word.capital && chars.size() > 1 && chars[1] != lower[1];
        // Capitalized and known so, or an acronym.
        word.name = word.capital && (!readings.empty() || word.acronym);
        // The first word, or after the end of a sentence (quotes, brackets, dashes between).
        word.sentenceStart = true;
        for (size_t p = index; p-- > 0;) {
            const std::string& t = tokens[p].text;
            if (contains(m_rules.transparent, t)) continue;
            word.sentenceStart = contains(m_rules.sentenceEnd, t);
            break;
        }
        // A capitalized word may be a common one (a title: "Życie"), or the first of a sentence.
        if (lower != chars)
            for (Reading& r : m_morphology.analyse(toUtf8(lower))) readings.push_back(std::move(r));
        if (const auto reflexiveForm = m_rules.reflexive.find(lower); reflexiveForm != m_rules.reflexive.end()) {
            Parsed reflexive;
            reflexive.lemma = "siebie";
            reflexive.pos = "siebie";
            reflexive.cases = reflexiveForm->second;
            word.readings = {reflexive};
            word.known = true;
            return word;
        }
        const bool beforeHyphen = index + 1 < tokens.size() && tokens[index + 1].text == "-";
        // The parts of speech a form usually has in a corpus ("mam" is
        // "mieć", hardly ever "mama").
        const auto it = m_forms.find(toUtf8(lower));
        word.frequent = it != m_forms.end();
        long total = 0;
        if (word.frequent)
            for (const auto& [pos, count] : it->second) total += count;
        const auto usual = [&](const std::string& pos) {
            if (!word.frequent || !comparablePart(pos)) return true;
            for (const auto& [p, count] : it->second)
                if (p == pos) return double(count) >= kSeldomShare * double(total);
            return false;
        };
        std::vector<Parsed> common, uncommon;
        for (const Reading& r : readings) {
            if (qualifiedBy(r.qualifiers, m_rules.archaic)) continue;
            Parsed parsed = parse(r);
            if (parsed.agglutinative) continue;
            if ((parsed.pos == "adja" || parsed.pos == "numcomp" || parsed.pos == "pacta") && !beforeHyphen) continue;
            parsed.usual = usual(parsed.pos);
            (qualifiedBy(r.qualifiers, m_rules.rare) ? uncommon : common).push_back(std::move(parsed));
        }
        // A finite verb: all its (usual) readings.
        bool anyUsual = false;
        word.finiteVerb = !common.empty();
        for (const Parsed& r : common) {
            if (!usual(r.pos)) continue;
            anyUsual = true;
            if (r.pos != "fin" && r.pos != "praet") word.finiteVerb = false;
        }
        word.finiteVerb = word.finiteVerb && anyUsual;
        word.rare = common.empty() && !uncommon.empty();
        // Rare readings only when there is no other, or with corpus counts
        // those of a usual part of speech ("marszu" is a genitive only in a
        // reading marked rare; "go" is never a noun of the game in a corpus).
        if (!m_forms.empty() && !common.empty())
            for (Parsed& r : uncommon)
                if (r.usual) common.push_back(std::move(r));
        word.readings = common.empty() ? std::move(uncommon) : std::move(common);
        word.known = !word.readings.empty();
        return word;
    };
    std::vector<Word> words;
    words.reserve(tokens.size());
    for (size_t i = 0; i < tokens.size(); ++i) words.push_back(analyse(i, tokens[i].text));

    const Complements* complements = m_hasComplements ? &m_complements : nullptr;
    const auto broken = [&](size_t k) { return brokenRule(m_rules, tokens, words, k, complements); };
    std::vector<Finding> findings;
    for (size_t k = 0; k < words.size(); ++k) {
        // A word with rare readings only, one diacritic from a common one
        // (a technical term one diacritic from a common word). Names left alone.
        if (words[k].rare && !words[k].name) {
            Finding finding{k, "rare word", {}};
            for (const std::string& variant : slipVariants(tokens[k].text)) {
                if (!diacriticsOnly(m_rules, variant, tokens[k].text)) continue;
                const Word w = analyse(k, variant);
                if (w.known && !w.rare) finding.suggestions.push_back({k, variant});
            }
            if (!finding.suggestions.empty()) findings.push_back(std::move(finding));
        }
        const char* rule = broken(k);
        if (!rule) continue;
        Finding finding{k, rule, {}};
        const bool verbs = std::string_view(rule) == "verbs";
        const bool government = std::string_view(rule) == "government";
        // How often a word is in the corpus (0 without counts).
        const auto usesOf = [&](const std::string& word) {
            long count = 0;
            if (const auto it = m_forms.find(toUtf8(toLower(fromUtf8(word)))); it != m_forms.end())
                for (const auto& [pos, n] : it->second) count += n;
            return count;
        };
        // A change fits when it leaves the words around unbroken: the pair
        // before, its own pairs and the one after.
        const auto windowClean = [&]() {
            for (size_t j = k > 0 ? k - 1 : 0; j <= k + 1 && j + 1 < words.size(); ++j)
                if (broken(j)) return false;
            return true;
        };
        // The two words as one, a space typed by mistake ("Po jechałem":
        // Pojechałem).
        bool joinedSeen = false;
        {
            const std::string joined = tokens[k].text + tokens[k + 1].text;
            const Word w = analyse(k, joined);
            if (w.known && !w.rare) {
                finding.suggestions.push_back({k, joined, 2});
                joinedSeen = m_forms.empty() || usesOf(joined) > 0;
            }
        }
        // Words one slip away. A preposition is not changed but into a
        // conjunction or particle ("ze" → "że"), common in the corpus: the
        // words after it are mended ("z naśladowanie" → naśladowaniem), each
        // keeping its part of speech. For two verbs, the first one as the
        // second one's subject.
        for (const size_t t : {k, k + 1}) {
            if (verbs && t != k) continue;
            const bool preposition = government && t == k;
            const Word kept = words[t];
            for (const std::string& variant : slipVariants(tokens[t].text)) {
                // A word of one letter only when the language has it ("w",
                // not "ż" or "P").
                const std::u32string chars = toLower(fromUtf8(variant));
                if (chars.size() == 1
                    && std::find(m_rules.oneLetterWords.begin(), m_rules.oneLetterWords.end(), chars)
                           == m_rules.oneLetterWords.end())
                    continue;
                words[t] = analyse(t, variant);
                if (!words[t].known || words[t].rare) continue;
                // Not an abbreviation (it would need a dot: "zł").
                if (std::all_of(words[t].readings.begin(), words[t].readings.end(),
                                [](const Parsed& r) { return r.pos == "brev"; }))
                    continue;
                if (preposition) {
                    // Into a conjunction or particle only ("że"), common.
                    const bool sameKind = std::any_of(words[t].readings.begin(), words[t].readings.end(),
                                                      [](const Parsed& r) { return r.pos == "prep" || isNominal(r.pos); });
                    const bool function = std::any_of(words[t].readings.begin(), words[t].readings.end(), [](const Parsed& r) {
                        return r.pos == "comp" || r.pos == "conj" || r.pos == "part";
                    });
                    if (sameKind || !function || (!m_forms.empty() && usesOf(variant) == 0)) continue;
                } else if (!verbs) {
                    // An adjective or noun stays one: a form of it mends the phrase
                    // ("angielski" not into the adverb "angielsko").
                    const bool samePart = std::any_of(kept.readings.begin(), kept.readings.end(), [&](const Parsed& o) {
                        return std::any_of(words[t].readings.begin(), words[t].readings.end(),
                                           [&](const Parsed& r) { return r.pos == o.pos; });
                    });
                    if (!samePart) continue;
                }
                if (verbs && !(subjectFits(words[t], words[k + 1]) && (m_forms.empty() || words[t].frequent))) continue;
                if (windowClean()) finding.suggestions.push_back({t, variant});
            }
            words[t] = kept;
        }
        // Likelier first: a joined word seen in the corpus, a diacritic, a
        // changed ending (the last letters), then by how often the word is
        // in the corpus. With corpus counts, other changes only to words
        // seen there (not "ze" → "zek").
        const auto diacritic = [&](const Suggestion& x) {
            return x.tokens == 1 && diacriticsOnly(m_rules, x.word, tokens[x.token].text);
        };
        const auto ending = [&](const Suggestion& x) {
            if (x.tokens != 1) return false;
            const std::u32string a = toLower(fromUtf8(tokens[x.token].text)), b = toLower(fromUtf8(x.word));
            size_t common = 0;
            while (common < a.size() && common < b.size() && a[common] == b[common]) ++common;
            return a.size() >= 4 && common + 2 >= a.size();
        };
        const auto joinedFirst = [&](const Suggestion& x) { return x.tokens > 1 && joinedSeen; };
        if (!m_forms.empty()) {
            std::vector<Suggestion> keptSuggestions;
            for (const Suggestion& x : finding.suggestions)
                if (x.tokens > 1 || diacritic(x) || ending(x) || usesOf(x.word) > 0) keptSuggestions.push_back(x);
            finding.suggestions = std::move(keptSuggestions);
        }
        // A joined word or a diacritic is far likelier than other slips:
        // with one of them, the others are left out ("Po" → "Poł").
        const auto likeliest = [&](const Suggestion& x) { return joinedFirst(x) || diacritic(x); };
        if (std::any_of(finding.suggestions.begin(), finding.suggestions.end(), likeliest))
            finding.suggestions.erase(std::remove_if(finding.suggestions.begin(), finding.suggestions.end(),
                                                     [&](const Suggestion& x) { return !likeliest(x); }),
                                      finding.suggestions.end());
        const auto rank = [&](const Suggestion& x) { return joinedFirst(x) ? 0 : diacritic(x) ? 1 : ending(x) ? 2 : 3; };
        std::stable_sort(finding.suggestions.begin(), finding.suggestions.end(), [&](const Suggestion& a, const Suggestion& b) {
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            return usesOf(a.word) > usesOf(b.word);
        });
        if (finding.suggestions.size() > kMaxSuggestions) finding.suggestions.resize(kMaxSuggestions);
        if (verbs && finding.suggestions.empty()) continue;
        findings.push_back(std::move(finding));
    }
    return findings;
}

} // namespace qce::grammar
