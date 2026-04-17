#pragma once

#include <qce/TextCursor.h>

namespace qce {

class ITextDocument;

/// Pure cursor-movement logic. Does not know about widgets, rendering, or
/// Qt signals. Given a document and a current cursor position, computes the
/// new position for standard keyboard navigation commands (arrows, Home/End,
/// PageUp/Down, Ctrl+Home/End).
///
/// Kept free of QObject / Qt widgets so it can be unit-tested without a
/// QApplication. The owner (CodeEditArea) translates QKeyEvents into calls
/// on this controller and emits signals as needed.
///
/// v0.2 uses QChar index as "column". Grapheme-cluster awareness comes later.
class CursorController {
public:
    /// Non-owning reference to the document. Must outlive the controller.
    /// May be null; when null, every move is a no-op returning the input.
    explicit CursorController(const ITextDocument* doc) : m_doc(doc) {}

    void setDocument(const ITextDocument* doc) { m_doc = doc; }
    const ITextDocument* document() const { return m_doc; }

    // --- Movement operations ---
    //
    // All return the new cursor position. They do NOT mutate any state;
    // the caller decides whether to commit the new position. This makes the
    // functions trivially testable.

    TextCursor moveUp(TextCursor c, int lines = 1) const;
    TextCursor moveDown(TextCursor c, int lines = 1) const;
    TextCursor moveLeft(TextCursor c) const;
    TextCursor moveRight(TextCursor c) const;
    TextCursor moveToLineStart(TextCursor c) const;
    TextCursor moveToLineEnd(TextCursor c) const;
    TextCursor moveToDocumentStart(TextCursor c) const;
    TextCursor moveToDocumentEnd(TextCursor c) const;

    /// Page movement. pageLines is the number of lines per viewport page,
    /// supplied by the view (typically derived from viewport height / line
    /// height). Must be >= 1; the function clamps silently.
    TextCursor movePageUp(TextCursor c, int pageLines) const;
    TextCursor movePageDown(TextCursor c, int pageLines) const;

    TextCursor moveWordLeft(TextCursor c) const;
    TextCursor moveWordRight(TextCursor c) const;

    /// Clamps the cursor to the valid range of the document. Useful after
    /// document changes (e.g. lines removed below the cursor).
    TextCursor clamp(TextCursor c) const;

private:
    const ITextDocument* m_doc;

    /// Returns the length of a given line, or 0 for out-of-range indices.
    int lineLength(int line) const;

    /// Returns lineCount() from the document, or 0 if no document.
    int lineCount() const;
};

} // namespace qce
