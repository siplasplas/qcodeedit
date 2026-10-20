#pragma once
// The morphology of a language as a minimal automaton: every reading of
// every form of a morphological dictionary (as PoliMorf), found by the form.
//
// Each reading is a string "form \0 lemma-code \0 info": the lemma written as
// how to make it of the form (bytes to cut at the start and the end, bytes
// to add), the info a number for (tag, label, qualifiers). The strings make
// a minimal acyclic automaton (Daciuk's algorithm on sorted input), in which
// forms share their beginnings and readings their ends, as Morfologik's
// dictionaries do. A file holds the automaton and the tables of infos.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace qce::grammar {

/// One reading of a form.
struct Reading {
    std::string lemma;
    std::string_view tag;         ///< "adj:sg:acc:f:pos"
    std::string_view label;       ///< "nazwa_pospolita", "imię", … or empty
    std::string_view qualifiers;  ///< "daw.", "rzad.", … or empty
};

/// Numbers of a build, for the tools.
struct MorphologyStats {
    long entries = 0;   ///< readings added
    long readings = 0;  ///< distinct (form, lemma, info) strings
    long infos = 0;     ///< distinct (tag, label, qualifiers)
    long states = 0;
    long arcs = 0;
    long fileSize = 0;
};

/// Makes a morphology file of readings added one by one, from any source
/// (spellmorf reads them from PoliMorf).
class MorphologyBuilder {
public:
    /// Adds a reading; false when it cannot be written (an empty form, a
    /// NUL byte, a lemma too unlike its form).
    bool add(std::string_view form, std::string_view lemma, std::string_view tag, std::string_view label = {},
             std::string_view qualifiers = {});
    /// Writes the file of the readings added (taking them: the builder is
    /// used once); false, with `error` set, when it cannot.
    bool write(const std::string& path, MorphologyStats* stats = nullptr, std::string* error = nullptr);

private:
    uint32_t stringId(std::string_view text);

    std::vector<std::string> m_strings{""};
    std::unordered_map<std::string, uint32_t> m_stringIds{{"", 0}};
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> m_infoIds;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> m_infos;
    std::vector<std::string> m_readings;
    long m_entries = 0;
};

class Morphology {
public:
    bool load(const std::string& path, std::string* error = nullptr);
    /// The readings of a form, as written (no case folding); none when unknown.
    std::vector<Reading> analyse(std::string_view form) const;
    bool contains(std::string_view form) const;

private:
    // An arc read at a byte offset of the automaton.
    struct Arc {
        bool valid = false;
        bool last = false;
        bool nextState = false;  // the target follows the arcs of this state
        uint8_t label = 0;
        uint32_t target = 0;  // byte offset of the target state, or the end
        size_t end = 0;       // offset after the arc
    };
    Arc arcAt(size_t pos) const;
    // The offset after the last arc of a state.
    size_t arcsEnd(size_t state) const;
    // The state reached from `state` by `label`, or none.
    uint32_t next(uint32_t state, uint8_t label) const;
    // Calls `onPath` with every byte string from `state` to the end.
    void paths(uint32_t state, std::string& path, const std::function<void(const std::string&)>& onPath) const;

    std::vector<std::string> m_strings;
    struct Info {
        uint32_t tag, label, qualifiers;
    };
    std::vector<Info> m_infos;
    uint32_t m_infoWidth = 3;
    uint32_t m_root = 0;
    std::array<uint8_t, 32> m_labels{};  // frequent labels by number (1–31)
    // The states one after another, each its arcs: a flags byte (last arc,
    // target the end, target right after this state, a frequent label's
    // number), the label byte unless frequent, and the target's offset in
    // 7-bit groups unless the end or right after. States are ordered depth
    // first, so most arcs need no address (as Morfologik's CFSA2).
    std::vector<uint8_t> m_automaton;
};

} // namespace qce::grammar
