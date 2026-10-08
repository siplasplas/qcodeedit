#pragma once

#include <QString>

namespace qce {

/// The one definition of a word used across the editor: Ctrl+Left/Right,
/// Ctrl+Backspace/Delete and whole-word search all use these functions, so
/// they agree on where a word starts and ends.
///
/// Word characters are letters, digits, combining marks and connector
/// punctuation such as '_' (Unicode categories L, N, M, Pc); surrogate pairs
/// are classified by their code point. Other non-space characters are
/// punctuation, and a run of them counts as one stop for cursor movement, so
/// "obj.method()" stops at "obj", ".", "method" and "()".
///
/// Word wrap does not use these functions: it breaks lines by the Unicode
/// line breaking rules (as Kate does), which never split a run of letters or
/// digits either.
namespace words {

/// True when the character at `index` in `text` is a word character.
bool isWordCharAt(const QString& text, int index);

/// Column Ctrl+Right moves to from `col`: past the rest of the current word
/// (or punctuation run) and the spaces after it. Returns line.size() at the
/// end of the line.
int nextWordStop(const QString& line, int col);

/// Column Ctrl+Left moves to from `col`: back over spaces, then to the start
/// of the word (or punctuation run) before them. Returns 0 at the start.
int previousWordStop(const QString& line, int col);

/// True when text[start, end) is not glued to word characters on either side,
/// i.e. a match that a whole-word search accepts.
bool isWholeWord(const QString& text, int start, int end);

/// Wraps a regular expression `pattern` so that it matches whole words only,
/// with the same word characters as isWholeWord(). For QRegularExpression.
QString wholeWordPattern(const QString& pattern);

} // namespace words
} // namespace qce
