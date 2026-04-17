#include "CursorController.h"

#include <qce/ITextDocument.h>

#include <algorithm>

namespace qce {

// --- Private helpers ----------------------------------------------------

int CursorController::lineLength(int line) const {
    if (!m_doc || line < 0 || line >= m_doc->lineCount()) {
        return 0;
    }
    return m_doc->lineAt(line).size();
}

int CursorController::lineCount() const {
    return m_doc ? m_doc->lineCount() : 0;
}

// --- Movement: vertical -------------------------------------------------

TextCursor CursorController::moveUp(TextCursor c, int lines) const {
    if (lines <= 0) return c;
    c.line = std::max(0, c.line - lines);
    c.column = std::min(c.column, lineLength(c.line));
    return c;
}

TextCursor CursorController::moveDown(TextCursor c, int lines) const {
    if (lines <= 0) return c;
    const int lastLine = std::max(0, lineCount() - 1);
    c.line = std::min(lastLine, c.line + lines);
    c.column = std::min(c.column, lineLength(c.line));
    return c;
}

TextCursor CursorController::movePageUp(TextCursor c, int pageLines) const {
    return moveUp(c, std::max(1, pageLines));
}

TextCursor CursorController::movePageDown(TextCursor c, int pageLines) const {
    return moveDown(c, std::max(1, pageLines));
}

// --- Movement: horizontal -----------------------------------------------

TextCursor CursorController::moveLeft(TextCursor c) const {
    if (c.column > 0) {
        --c.column;
        return c;
    }
    // At column 0: wrap to end of previous line if possible.
    if (c.line > 0) {
        --c.line;
        c.column = lineLength(c.line);
    }
    return c;
}

TextCursor CursorController::moveRight(TextCursor c) const {
    const int len = lineLength(c.line);
    if (c.column < len) {
        ++c.column;
        return c;
    }
    // At end of line: wrap to start of next line if possible.
    if (c.line < lineCount() - 1) {
        ++c.line;
        c.column = 0;
    }
    return c;
}

// --- Movement: absolute -------------------------------------------------

TextCursor CursorController::moveToLineStart(TextCursor c) const {
    c.column = 0;
    return c;
}

TextCursor CursorController::moveToLineEnd(TextCursor c) const {
    c.column = lineLength(c.line);
    return c;
}

TextCursor CursorController::moveToDocumentStart(TextCursor /*c*/) const {
    return TextCursor{0, 0};
}

TextCursor CursorController::moveToDocumentEnd(TextCursor /*c*/) const {
    const int n = lineCount();
    if (n == 0) {
        return TextCursor{0, 0};
    }
    TextCursor r;
    r.line = n - 1;
    r.column = lineLength(r.line);
    return r;
}

// --- Movement: word -----------------------------------------------------

TextCursor CursorController::moveWordLeft(TextCursor c) const {
    if (!m_doc) return c;
    // If at column 0, wrap to end of previous line.
    if (c.column == 0) {
        if (c.line == 0) return c;
        --c.line;
        c.column = lineLength(c.line);
        return c;
    }
    const QString line = m_doc->lineAt(c.line);
    int col = c.column;
    // Skip whitespace to the left.
    while (col > 0 && line.at(col - 1).isSpace()) --col;
    // Skip non-whitespace to the left.
    while (col > 0 && !line.at(col - 1).isSpace()) --col;
    c.column = col;
    return c;
}

TextCursor CursorController::moveWordRight(TextCursor c) const {
    if (!m_doc) return c;
    const int len = lineLength(c.line);
    // If at end of line, wrap to start of next line.
    if (c.column >= len) {
        if (c.line >= lineCount() - 1) return c;
        ++c.line;
        c.column = 0;
        return c;
    }
    const QString line = m_doc->lineAt(c.line);
    int col = c.column;
    // Skip non-whitespace to the right.
    while (col < len && !line.at(col).isSpace()) ++col;
    // Skip whitespace to the right.
    while (col < len && line.at(col).isSpace()) ++col;
    c.column = col;
    return c;
}

// --- Clamp --------------------------------------------------------------

TextCursor CursorController::clamp(TextCursor c) const {
    const int n = lineCount();
    if (n == 0) {
        return TextCursor{0, 0};
    }
    c.line = std::clamp(c.line, 0, n - 1);
    c.column = std::clamp(c.column, 0, lineLength(c.line));
    return c;
}

} // namespace qce
