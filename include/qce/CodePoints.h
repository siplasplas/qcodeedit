#pragma once

#include <QString>

namespace qce {

/// Columns are QChar (UTF-16) indices, but a character outside the Basic
/// Multilingual Plane (an emoji, an Egyptian hieroglyph, ...) is a surrogate
/// pair of two QChars. These helpers keep the caret, the visual columns and
/// the column shown to the user on code points, so that such a character is
/// one column and the caret never stands between the halves of a pair.
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
