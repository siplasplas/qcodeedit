#pragma once
// Tags of the NKJP/SGJP tagset ("adj:sg:acc:f:pos"): their parts, part of
// speech, and the values of case, number and gender the rules compare.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qce::grammar {

/// The parts of a tag: "adj:sg:acc:f:pos" → {"adj", "sg", "acc", "f", "pos"};
/// each part may hold alternatives joined by "." ("nom.voc").
std::vector<std::string_view> tagParts(std::string_view tag);

/// The part of speech of a tag (its first part): "adj", "subst", "fin", …
std::string_view partOfSpeech(std::string_view tag);

/// The part of speech and the values of case, number and gender of a tag
/// as bit sets (alternatives "nom.voc" both set).
struct TagValues {
    enum : uint8_t { Nom = 1, Gen = 2, Dat = 4, Acc = 8, Inst = 16, Loc = 32, Voc = 64 };
    std::string pos;
    uint8_t cases = 0, numbers = 0, genders = 0;
    bool agglutinative = false;  ///< "agl": a past form only before "-em", "-eś"
    bool vocalic = false;        ///< "wok": a preposition's form with "e" ("ze", "we")
};
TagValues tagValues(std::string_view tag);

} // namespace qce::grammar
