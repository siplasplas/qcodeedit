#pragma once

#include "TextCursor.h"
#include "ViewportState.h"

#include <QAbstractScrollArea>

#include <memory>

namespace qce {

class ITextDocument;
class LineRenderer;
class CursorController;
class CaretPainter;

/// The actual text-rendering widget.
///
/// Inherits QAbstractScrollArea and renders the document's lines in its
/// viewport. Publishes ViewportState via viewportChanged() so that margins
/// (gutters, side bars) can paint themselves in sync without needing direct
/// access to this class's internals.
class CodeEditArea : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit CodeEditArea(QWidget* parent = nullptr);
    ~CodeEditArea() override;

    /// Attaches a document to the view. The view does not take ownership;
    /// the caller is responsible for the document's lifetime. Pass nullptr
    /// to detach.
    void setDocument(ITextDocument* doc);

    /// Returns the currently attached document, or nullptr if none.
    ITextDocument* document() const { return m_doc; }

    /// Returns the current viewport state snapshot. Cheap: just a struct copy.
    ViewportState viewportState() const { return m_viewportState; }

    // --- Cursor API ---

    /// Current cursor position. Always clamped to a valid range when there
    /// is a document; (0, 0) for an empty or detached document.
    TextCursor cursorPosition() const { return m_cursor; }

    /// Moves the cursor to the given position. Clamps to document bounds
    /// and scrolls the viewport to keep the cursor visible. Emits
    /// cursorPositionChanged if the effective position differs from the
    /// previous one.
    void setCursorPosition(TextCursor pos);

    // --- Selection API ---

    /// Returns true if there is a non-empty selection.
    bool hasSelection() const { return m_anchor != m_cursor; }

    /// Start of the selection (the lesser of anchor and cursor).
    TextCursor selectionStart() const;

    /// End of the selection (the greater of anchor and cursor).
    TextCursor selectionEnd() const;

    /// Returns the selected text, or an empty string if nothing is selected.
    QString selectedText() const;

    /// Selects the entire document.
    void selectAll();

    /// Collapses the selection to the current cursor position.
    void clearSelection();

    // --- Configuration ---

    /// Number of space characters a tab expands to. Default 4. Affects
    /// rendering only in v0.2. Must be > 0; values <= 0 are silently
    /// treated as 4.
    void setTabWidth(int spaces);
    int tabWidth() const;

    /// Caret blink interval in milliseconds. Default 500. Values <= 0 are
    /// silently treated as 500.
    void setCaretBlinkInterval(int ms);
    int caretBlinkInterval() const;

signals:
    /// Emitted whenever the viewport state changes (scroll, resize, document
    /// modification). Margins connect to this to schedule their own repaint.
    void viewportChanged(const ViewportState& state);

    /// Emitted whenever the cursor position changes. Not emitted on no-op
    /// moves (e.g. pressing Down when already on the last line).
    void cursorPositionChanged(TextCursor pos);

    /// Emitted whenever the selection changes (including when it is cleared).
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private slots:
    void onDocumentReset();
    void onLinesInserted(int startLine, int count);
    void onLinesRemoved(int startLine, int count);
    void onLinesChanged(int startLine, int count);

private:
    ITextDocument* m_doc = nullptr;
    ViewportState m_viewportState;
    TextCursor m_cursor;
    TextCursor m_anchor; // selection anchor; equals m_cursor when no selection

    // Owned helpers. unique_ptr so we can forward-declare in the header.
    std::unique_ptr<LineRenderer> m_renderer;
    std::unique_ptr<CursorController> m_cursorCtrl;
    std::unique_ptr<CaretPainter> m_caretPainter;

    // --- Private helpers ---

    void refreshViewportState();
    void updateScrollBarRanges();
    void rebindDocumentSignals(ITextDocument* newDoc);

    /// Moves cursor + collapses selection. No-op if nothing changes.
    void applyCursorMove(TextCursor newPos);

    /// Moves cursor, keeps anchor (Shift+key / mouse drag).
    void applySelectionMove(TextCursor newPos);

    /// Converts a viewport pixel position to a document cursor.
    TextCursor cursorFromPoint(const QPoint& pt) const;

    /// Paints the selection highlight behind the text.
    void paintSelection(QPainter& painter);

    void ensureCursorVisible(TextCursor pos);
    int pageLineCount() const;
};

} // namespace qce
