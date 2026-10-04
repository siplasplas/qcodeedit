#pragma once

#include "FoldState.h"
#include "ExtraSelection.h"
#include "FoldMarker.h"
#include "HighlightState.h"
#include "StyleSpan.h"
#include "TextCursor.h"
#include "ViewportState.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QHash>
#include <QInputMethodEvent>
#include <QRegion>
#include <QVector>

#include <functional>
#include <memory>

class QUndoStack;

namespace qce {

class ITextDocument;
class IHighlighter;
class IFoldingProvider;
class LineRenderer;
class CursorController;
class CaretPainter;
class WrapLayout;

/// The actual text-rendering and editing widget.
class CodeEditArea : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit CodeEditArea(QWidget* parent = nullptr);
    ~CodeEditArea() override;

    void setDocument(ITextDocument* doc);
    ITextDocument* document() const { return m_doc; }

    ViewportState viewportState() const { return m_viewportState; }

    // --- Cursor ---
    TextCursor cursorPosition() const { return m_cursor; }
    void setCursorPosition(TextCursor pos);

    // --- Selection ---
    bool hasSelection() const { return m_anchor != m_cursor; }
    TextCursor selectionStart() const;
    TextCursor selectionEnd() const;
    QString selectedText() const;
    /// Clamp both endpoints, preserve direction, scroll the active cursor into view.
    /// Does not change document contents or undo history.
    void setSelection(TextCursor anchor, TextCursor cursor);
    void selectAll();
    void clearSelection();

    /// Replace independent range decorations. Reversed endpoints are normalized,
    /// positions clamped, empty ranges discarded. Later entries win as a complete
    /// style; invalid foreground preserves syntax. Actual selection wins for background.
    /// Only visible text is decorated (no newline padding or fold placeholders).
    /// Cleared on any text change/reset or document replacement; no automatic tracking.
    /// Updating decorations only repaints, without re-highlighting or recomputing folds.
    void setExtraSelections(const QVector<ExtraSelection>& selections);
    QVector<ExtraSelection> extraSelections() const { return m_extraSelections; }

    void setSelectionColor(const QColor& color);
    QColor selectionColor() const { return m_selectionColor; }

    /// Per-line background provider. Returns the fill colour for a logical
    /// line, or an invalid QColor to leave the default. Intended for
    /// breakpoints, diff highlights, inline warnings, etc. The provider is
    /// queried during paint() only — callers drive repaints with viewport()->update()
    /// after changing the underlying state.
    using LineBackgroundFn = std::function<QColor(int line)>;
    void setLineBackgroundProvider(LineBackgroundFn fn);

    // --- Undo / redo ---
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    /// Exposes the undo stack for external wiring (e.g. menu enable/disable).
    QUndoStack* undoStack() const { return m_undoStack; }

    // --- Configuration ---
    void setTabWidth(int spaces);
    int  tabWidth() const;

    /// When true (default), Tab inserts spaces and Shift+Tab dedents.
    /// When false, Tab / Shift+Tab pass through to Qt focus navigation.
    /// Ctrl+Tab and Shift+Ctrl+Tab always pass through regardless.
    void setTabCaptured(bool captured);
    bool tabCaptured() const { return m_tabCaptured; }

    void setReadOnly(bool ro);
    bool readOnly() const { return m_readOnly; }

    bool overwriteMode() const { return m_overwrite; }

    void setWordWrap(bool wrap);
    bool wordWrap() const { return m_wordWrap; }

    void setShowWhitespace(bool show);
    bool showWhitespace() const;

    /// Attach a syntax highlighter (non-owning). Pass nullptr to disable
    /// highlighting. Triggers a full re-highlight of the document.
    void setHighlighter(IHighlighter* hl);
    IHighlighter* highlighter() const { return m_highlighter; }

    /// Attach a folding provider (non-owning). Pass nullptr to disable.
    /// Recomputes regions immediately.
    void setFoldingProvider(IFoldingProvider* p);
    IFoldingProvider* foldingProvider() const { return m_foldingProvider; }

    /// Editor-owned fold state. Exposed so margins (gutter) can read it and
    /// invoke toggleFoldAt(). Non-const so FoldingGutter can call into it.
    FoldState& foldState() { return m_foldState; }
    const FoldState& foldState() const { return m_foldState; }

    /// If a region starts on `line`, toggle its collapsed state. Rebuilds
    /// layout and repaints.
    void toggleFoldAt(int line);

    /// Collapse / expand every region.
    void foldAll();
    void unfoldAll();

    void setCaretBlinkInterval(int ms);
    int  caretBlinkInterval() const;

    // --- Input methods ---
    /// Text being composed by an input method (IME pre-edit). Painted inline
    /// at the caret but not part of the document; empty when no composition
    /// is in progress.
    QString preeditString() const { return m_preedit; }

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void viewportChanged(const ViewportState& state);
    void cursorPositionChanged(TextCursor pos);
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
    void inputMethodEvent(QInputMethodEvent* e) override;

private slots:
    void onDocumentReset();
    void onLinesInserted(int startLine, int count);
    void onLinesRemoved(int startLine, int count);
    void onLinesChanged(int startLine, int count);

private:
    ITextDocument* m_doc = nullptr;
    ViewportState  m_viewportState;
    TextCursor     m_cursor;
    TextCursor     m_anchor;
    bool           m_mouseSelecting = false;
    QColor m_selectionColor{QStringLiteral("#A6D2FF")};
    LineBackgroundFn m_lineBgProvider;
    QVector<ExtraSelection> m_extraSelections;
    // Disjoint, sorted segments per logical line; overlap resolved on bulk update.
    QHash<int, QVector<ExtraSelection>> m_extraSelectionsByLine;
    bool   m_tabCaptured     = true;
    bool   m_readOnly        = false;
    bool   m_overwrite       = false;
    bool   m_wordWrap        = false;

    std::unique_ptr<LineRenderer>      m_renderer;
    std::unique_ptr<WrapLayout>        m_wrapLayout;

    IHighlighter*                      m_highlighter = nullptr;
    QVector<HighlightState>            m_lineEndStates;
    QVector<QVector<StyleSpan>>        m_lineSpans;
    /// Fold markers per line, collected while highlighting; lets the folding
    /// provider build regions without tokenising the document again.
    QVector<QVector<FoldMarker>>       m_lineFolds;
    /// Line count the caches above and the fold state match. Differs from the
    /// document while it has reported a changed line but not yet the lines
    /// it inserted or removed.
    int                                m_knownLineCount = 0;
    /// Earliest changed line whose re-highlight waits for that signal.
    int                                m_pendingRehighlightFrom = -1;

    IFoldingProvider*                  m_foldingProvider = nullptr;
    FoldState                          m_foldState;
    std::unique_ptr<CursorController>  m_cursorCtrl;
    std::unique_ptr<CaretPainter>      m_caretPainter;
    QUndoStack*                        m_undoStack = nullptr;

    // Input-method pre-edit (display only, never in the document or undo).
    QString                                 m_preedit;
    QList<QInputMethodEvent::Attribute>     m_preeditAttributes;
    int                                     m_preeditCursor = 0;  ///< QChar index in m_preedit
    bool                                    m_preeditCursorVisible = true;

    // --- Navigation helpers ---
    void refreshViewportState();
    void updateScrollBarRanges();
    void rebindDocumentSignals(ITextDocument* newDoc);
    void applyCursorMove(TextCursor newPos);
    void applySelectionMove(TextCursor newPos);
    TextCursor cursorFromPoint(const QPoint& pt) const;
    /// `extraColumns` widens the area kept visible to the right of `pos`
    /// (used for the input-method pre-edit).
    void ensureCursorVisible(TextCursor pos, int extraColumns = 0);
    int  pageLineCount() const;

    // --- Word-wrap helpers ---
    void rebuildWrapLayout();
    int  visualRowOf(TextCursor pos) const;

    // --- Highlighting helpers ---
    void rebuildHighlightCache();
    void rehighlightFrom(int startLine);
    /// min(startLine, pending changed line); clears the pending line.
    int  takePendingRehighlight(int startLine);

    // --- Folding helpers ---
    void rebuildFolds();

    // --- Edit helpers ---
    /// Insert text at cursor (replacing selection if present).
    void executeInsert(const QString& text);
    /// Remove the range [start, end) as one undo step.
    void executeRemove(TextCursor start, TextCursor end);
    /// Remove current selection as one undo step. No-op if no selection.
    void executeRemoveSelection();
    /// Called after any edit or undo/redo to sync visuals.
    void updateAfterEdit();
    /// Insert typed or committed text with the typing rules: replaces the
    /// selection, and in overwrite mode the character under the cursor, as
    /// one undo step.
    void insertTypedText(const QString& text);
    /// True if every code point of `text` is printable or a combining mark.
    static bool isInsertableText(const QString& text);

    // --- Input-method helpers ---
    /// Tell the platform input method that cursor/selection/surrounding text
    /// changed. No-op without focus.
    void updateInputMethod(Qt::InputMethodQueries queries = Qt::ImCursorRectangle
                               | Qt::ImCursorPosition | Qt::ImAnchorPosition
                               | Qt::ImSurroundingText | Qt::ImCurrentSelection);
    /// Drop a pending composition. With `resetInputMethod` the platform input
    /// method is told to discard its state as well.
    void cancelPreedit(bool resetInputMethod);
    /// Caret rectangle in viewport coordinates; with `withPreedit` it is placed
    /// at the pre-edit cursor.
    QRect caretRect(bool withPreedit) const;
    /// Pixel width of the pre-edit up to its cursor.
    int preeditCursorOffsetPx() const;
    /// Pre-edit width rounded up to whole character cells.
    int preeditColumns() const;

    // --- Painting helpers ---
    void paintLineBackgrounds(QPainter& painter);
    void paintSelection(QPainter& painter);
    void paintPreedit(QPainter& painter);
    QRegion selectionRegion() const;

    void showReadOnlyHint();
};

} // namespace qce
