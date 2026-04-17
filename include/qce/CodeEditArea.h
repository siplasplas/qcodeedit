#pragma once

#include "TextCursor.h"
#include "ViewportState.h"

#include <QAbstractScrollArea>

#include <memory>

namespace qce {

class ITextDocument;
class LineRenderer;
class CursorController;

/// The actual text-rendering widget.
///
/// Inherits QAbstractScrollArea and renders the document's lines in its
/// viewport. Publishes ViewportState via viewportChanged() so that margins
/// (gutters, side bars) can paint themselves in sync without needing direct
/// access to this class's internals.
///
/// v0.2: read-only with text rendering, keyboard navigation, and a logical
/// cursor (no visible caret yet — that comes in v0.3 alongside margins).
/// The cursor is fully plumbed: its position moves with arrows / PgUp /
/// PgDn / Home / End / Ctrl+Home / Ctrl+End, and the view auto-scrolls to
/// keep it visible. cursorPositionChanged() is emitted on every change.
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

    // --- Configuration ---

    /// Number of space characters a tab expands to. Default 4. Affects
    /// rendering only in v0.2. Must be > 0; values <= 0 are silently
    /// treated as 4.
    void setTabWidth(int spaces);
    int tabWidth() const;

signals:
    /// Emitted whenever the viewport state changes (scroll, resize, document
    /// modification). Margins connect to this to schedule their own repaint.
    void viewportChanged(const ViewportState& state);

    /// Emitted whenever the cursor position changes. Not emitted on no-op
    /// moves (e.g. pressing Down when already on the last line).
    void cursorPositionChanged(TextCursor pos);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void onDocumentReset();
    void onLinesInserted(int startLine, int count);
    void onLinesRemoved(int startLine, int count);
    void onLinesChanged(int startLine, int count);

private:
    ITextDocument* m_doc = nullptr;
    ViewportState m_viewportState;
    TextCursor m_cursor;

    // Owned helpers. unique_ptr so we can forward-declare in the header.
    std::unique_ptr<LineRenderer> m_renderer;
    std::unique_ptr<CursorController> m_cursorCtrl;

    // --- Private helpers ---

    /// Recomputes ViewportState from current scroll/size and emits
    /// viewportChanged(). Called after any relevant change.
    void refreshViewportState();

    /// Recomputes scroll bar ranges based on document size and viewport size.
    void updateScrollBarRanges();

    /// Disconnects signal handlers from the current document (if any) and
    /// connects them to the new one.
    void rebindDocumentSignals(ITextDocument* newDoc);

    /// Core cursor move: applies `newPos`, clamps, emits signals, scrolls
    /// the viewport to keep the cursor visible, and requests a repaint.
    /// No-op if newPos equals current position after clamping.
    void applyCursorMove(TextCursor newPos);

    /// Adjusts scroll bars so the given cursor is visible in the viewport.
    /// Only scrolls if the cursor is currently off-screen.
    void ensureCursorVisible(TextCursor pos);

    /// Returns the number of full lines that fit in the viewport (>=1).
    /// Used for PageUp/PageDown step size.
    int pageLineCount() const;
};

} // namespace qce
