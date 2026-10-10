#pragma once

namespace qce {

/// Logical cursor position in a text document.
///
/// Column is a QChar (UTF-16 code unit) index. The editor keeps it on code
/// point boundaries (a surrogate pair counts as one character for the caret
/// and the visual columns, see qce/CodePoints.h); a later version may switch
/// to grapheme-cluster indices for combining characters.
///
/// Invariant: line >= 0, column >= 0.
struct TextCursor {
    int line = 0;
    int column = 0;

    friend bool operator==(const TextCursor& a, const TextCursor& b) noexcept {
        return a.line == b.line && a.column == b.column;
    }
    friend bool operator!=(const TextCursor& a, const TextCursor& b) noexcept {
        return !(a == b);
    }
    friend bool operator<(const TextCursor& a, const TextCursor& b) noexcept {
        return a.line < b.line || (a.line == b.line && a.column < b.column);
    }
    friend bool operator<=(const TextCursor& a, const TextCursor& b) noexcept {
        return !(b < a);
    }
};

} // namespace qce
