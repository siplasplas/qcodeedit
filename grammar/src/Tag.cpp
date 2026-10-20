#include <qce/grammar/Tag.h>

namespace qce::grammar {

std::vector<std::string_view> tagParts(std::string_view tag) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t colon = tag.find(':', start);
        parts.push_back(tag.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start));
        if (colon == std::string_view::npos) break;
        start = colon + 1;
    }
    return parts;
}

std::string_view partOfSpeech(std::string_view tag) {
    return tag.substr(0, tag.find(':'));
}

TagValues tagValues(std::string_view tag) {
    static const std::pair<const char*, uint8_t> caseBits[] = {{"nom", TagValues::Nom},   {"gen", TagValues::Gen},
                                                               {"dat", TagValues::Dat},   {"acc", TagValues::Acc},
                                                               {"inst", TagValues::Inst}, {"loc", TagValues::Loc},
                                                               {"voc", TagValues::Voc}};
    static const std::pair<const char*, uint8_t> numberBits[] = {{"sg", 1}, {"pl", 2}};
    static const std::pair<const char*, uint8_t> genderBits[] = {{"m1", 1}, {"m2", 2}, {"m3", 4}, {"f", 8}, {"n", 16}};
    TagValues values;
    const std::vector<std::string_view> parts = tagParts(tag);
    if (parts.empty()) return values;
    values.pos = std::string(parts[0]);
    for (size_t i = 1; i < parts.size(); ++i) {
        if (parts[i] == "agl") values.agglutinative = true;
        if (parts[i] == "wok") values.vocalic = true;
        std::string_view rest = parts[i];
        while (!rest.empty()) {
            const size_t dot = rest.find('.');
            const std::string_view value = rest.substr(0, dot);
            rest = dot == std::string_view::npos ? std::string_view() : rest.substr(dot + 1);
            for (const auto& [name, bit] : caseBits)
                if (value == name) values.cases |= bit;
            for (const auto& [name, bit] : numberBits)
                if (value == name) values.numbers |= bit;
            for (const auto& [name, bit] : genderBits)
                if (value == name) values.genders |= bit;
        }
    }
    return values;
}

} // namespace qce::grammar
