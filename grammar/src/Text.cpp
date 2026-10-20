#include <qce/grammar/Text.h>

namespace qce::grammar {

std::u32string fromUtf8(std::string_view text) {
    std::u32string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + size_t(length) > text.size()) {
            result.push_back(U'�');
            ++i;
            continue;
        }
        char32_t code = length == 1 ? c : c & (0x7F >> length);
        bool valid = true;
        for (int k = 1; k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + size_t(k)]);
            if ((next >> 6) != 0x2) valid = false;
            code = (code << 6) | (next & 0x3F);
        }
        result.push_back(valid ? code : U'�');
        i += valid ? size_t(length) : 1;
    }
    return result;
}

std::string toUtf8(std::u32string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char32_t c : text) {
        if (c < 0x80) {
            result.push_back(char(c));
        } else if (c < 0x800) {
            result.push_back(char(0xC0 | (c >> 6)));
            result.push_back(char(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            result.push_back(char(0xE0 | (c >> 12)));
            result.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            result.push_back(char(0x80 | (c & 0x3F)));
        } else {
            result.push_back(char(0xF0 | (c >> 18)));
            result.push_back(char(0x80 | ((c >> 12) & 0x3F)));
            result.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            result.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return result;
}

bool isLetter(char32_t c) {
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) return true;
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;  // Latin-1 letters, Latin Extended-A and B
    if (c >= 0x370 && c <= 0x3FF) return c != 0x37E && c != 0x387;  // Greek
    return c >= 0x400 && c <= 0x4FF;                                   // Cyrillic
}

char32_t toLower(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if ((c >= 0xC0 && c <= 0xDE) && c != 0xD7) return c + 32;
    // Latin Extended-A: mostly pairs, upper case even (Ą 0x104, ą 0x105).
    if (c >= 0x100 && c <= 0x17F) {
        const bool oddUpper = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
        if (oddUpper) return (c % 2 == 1) ? c + 1 : c;
        return (c % 2 == 0) ? c + 1 : c;
    }
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;  // Greek
    if (c >= 0x410 && c <= 0x42F) return c + 32;                 // Cyrillic basic
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

char32_t toUpper(char32_t c) {
    // The capital is 32, 1 or 80 code points before the small letter (Latin
    // and Greek, Latin Extended-A, Cyrillic).
    for (const char32_t distance : {char32_t(32), char32_t(1), char32_t(80)})
        if (c >= distance && toLower(c - distance) == c && isLetter(c - distance)) return c - distance;
    return c;
}

std::u32string toLower(std::u32string_view word) {
    std::u32string result(word);
    for (char32_t& c : result) c = toLower(c);
    return result;
}

std::vector<std::u32string> words(std::u32string_view text) {
    std::vector<std::u32string> result;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !isLetter(text[i])) ++i;
        const size_t start = i;
        while (i < text.size()) {
            if (isLetter(text[i])) {
                ++i;
            } else if ((text[i] == U'\'' || text[i] == U'’') && i > start && i + 1 < text.size()
                       && isLetter(text[i + 1])) {
                ++i;
            } else {
                break;
            }
        }
        if (i > start) result.emplace_back(text.substr(start, i - start));
    }
    return result;
}

} // namespace qce::grammar
