#pragma once
// Text helpers without Qt: UTF-8 to code points and back, letters and lower
// case of the Latin scripts (Polish included), words of a text.

#include <string>
#include <string_view>
#include <vector>

namespace qce::grammar {

/// UTF-8 to code points; an invalid byte becomes U+FFFD.
std::u32string fromUtf8(std::string_view text);
std::string toUtf8(std::u32string_view text);

/// A letter of the Latin, Greek or Cyrillic blocks (enough for the words of
/// the European languages).
bool isLetter(char32_t c);
/// Lower case of the Latin letters (with diacritics) and of Greek and
/// Cyrillic; others as they are.
char32_t toLower(char32_t c);
std::u32string toLower(std::u32string_view word);
/// Upper case of the letters toLower() knows; others as they are.
char32_t toUpper(char32_t c);

/// The words of a text: letters, with apostrophes between letters ("don't").
std::vector<std::u32string> words(std::u32string_view text);

} // namespace qce::grammar
