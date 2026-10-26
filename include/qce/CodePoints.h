#pragma once

#include <QString>

namespace qce {

/// Columns are QChar (UTF-16) indices, but a character outside the Basic
/// Multilingual Plane (an emoji, an Egyptian hieroglyph, ...) is a surrogate
/// pair of two QChars. These helpers keep the caret, the visual columns and
/// the column shown to the user on code points, so that such a character is
/// one column and the caret never stands between the halves of a pair.
///
/// The text is drawn on a grid of cells: most characters take one, an East
/// Asian wide or fullwidth character (Chinese, Japanese, Korean, ...) two,
/// as its glyph does (cellsAt()).
///
/// Each function costs one or two QChar comparisons per character; a line
/// without surrogates is not slowed down noticeably.
namespace codepoints {

/// True when text[index] and text[index + 1] form a surrogate pair.
inline bool isPairAt(const QString& text, int index) {
    return index >= 0 && index + 1 < text.size()
        && text.at(index).isHighSurrogate() && text.at(index + 1).isLowSurrogate();
}

/// True when text[index] is the second half of a surrogate pair.
inline bool continuesPair(const QString& text, int index) {
    return index > 0 && index < text.size()
        && text.at(index).isLowSurrogate() && text.at(index - 1).isHighSurrogate();
}

/// Index of the code point after the one at `index`; text.size() at the end.
inline int nextIndex(const QString& text, int index) {
    if (index >= text.size()) return int(text.size());
    return index + (isPairAt(text, index) ? 2 : 1);
}

/// Index of the code point before `index`; 0 at the start.
inline int prevIndex(const QString& text, int index) {
    index = qMin(index, int(text.size()));
    if (index <= 0) return 0;
    return index - (continuesPair(text, index - 1) ? 2 : 1);
}

/// `index` moved back to the start of the code point it is in, so that it
/// never points between the halves of a surrogate pair.
inline int toBoundary(const QString& text, int index) {
    return continuesPair(text, index) ? index - 1 : index;
}

/// An East Asian wide or fullwidth character (Unicode East_Asian_Width W or
/// F): CJK ideographs (also outside the BMP), kana, Hangul, CJK symbols and
/// punctuation, fullwidth forms. Others, e.g. Egyptian hieroglyphs or
/// emoji, are not.
inline bool isWide(char32_t c) {
    if (c < 0x1100) return false;
    return (c <= 0x115F)                    // Hangul Jamo
        || (c >= 0x2E80 && c <= 0x303E)     // CJK radicals, Kangxi, CJK symbols and punctuation
        || (c >= 0x3041 && c <= 0x33FF)     // kana, Bopomofo, Hangul compatibility, CJK compatibility
        || (c >= 0x3400 && c <= 0x4DBF)     // CJK Extension A
        || (c >= 0x4E00 && c <= 0x9FFF)     // CJK Unified Ideographs
        || (c >= 0xA000 && c <= 0xA4CF)     // Yi
        || (c >= 0xA960 && c <= 0xA97F)     // Hangul Jamo Extended-A
        || (c >= 0xAC00 && c <= 0xD7A3)     // Hangul syllables
        || (c >= 0xF900 && c <= 0xFAFF)     // CJK compatibility ideographs
        || (c >= 0xFE10 && c <= 0xFE19)     // vertical forms
        || (c >= 0xFE30 && c <= 0xFE6F)     // CJK compatibility forms, small forms
        || (c >= 0xFF00 && c <= 0xFF60)     // fullwidth forms
        || (c >= 0xFFE0 && c <= 0xFFE6)
        || (c >= 0x1B000 && c <= 0x1B2FF)   // kana supplement and extensions
        || (c >= 0x20000 && c <= 0x2FFFD)   // CJK Extensions B–F, compatibility supplement
        || (c >= 0x30000 && c <= 0x3FFFD);  // CJK Extensions G–H
}

/// Cells the character at text[index] takes (a tab aside): 0 for the second
/// half of a surrogate pair, 2 for a wide character, 1 otherwise.
inline int cellsAt(const QString& text, int index) {
    if (continuesPair(text, index)) return 0;
    const char32_t c = isPairAt(text, index) ? QChar::surrogateToUcs4(text.at(index), text.at(index + 1))
                                             : char32_t(text.at(index).unicode());
    return isWide(c) ? 2 : 1;
}

/// Number of code points in text[0, index): the column to show the user
/// (0-based). Equal to `index` when the line has no character outside
/// the BMP.
inline int codePointColumn(const QString& text, int index) {
    index = qBound(0, index, int(text.size()));
    int column = index;
    for (int i = 1; i < index; ++i)
        if (continuesPair(text, i)) --column;
    return column;
}

} // namespace codepoints
} // namespace qce
