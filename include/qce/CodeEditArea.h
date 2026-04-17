#pragma once

#include "FillerState.h"
#include "FoldState.h"
#include "HighlightState.h"
#include "StyleSpan.h"
#include "TextCursor.h"
#include "ViewportState.h"

#include <QAbstractScrollArea>
#include <QColor>
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
    void selectAll();
    void clearSelection();

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

    /// Attach a filler-line state (non-owning). Pass nullptr to disable.
    /// Call refreshFillers() after mutating the state in place.
    void setFillerState(FillerState* s);
    FillerState* fillerState() const { return m_fillerState; }
    void refreshFillers();

    void setCaretBlinkInterval(int ms);
    int  caretBlinkInterval() const;

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
    QColor m_selectionColor{QStringLiteral("#A6D2FF")};
    LineBackgroundFn m_lineBgProvider;
    bool   m_tabCaptured     = true;
    bool   m_readOnly        = false;
    bool   m_overwrite       = false;
    bool   m_wordWrap        = false;

    std::unique_ptr<LineRenderer>      m_renderer;
    std::unique_ptr<WrapLayout>        m_wrapLayout;

    IHighlighter*                      m_highlighter = nullptr;
    QVector<HighlightState>            m_lineEndStates;
    QVector<QVector<StyleSpan>>        m_lineSpans;

    IFoldingProvider*                  m_foldingProvider = nullptr;
    FoldState                          m_foldState;

    FillerState*                       m_fillerState = nullptr;
    std::unique_ptr<CursorController>  m_cursorCtrl;
    std::unique_ptr<CaretPainter>      m_caretPainter;
    QUndoStack*                        m_undoStack = nullptr;

    // --- Navigation helpers ---
    void refreshViewportState();
    void updateScrollBarRanges();
    void rebindDocumentSignals(ITextDocument* newDoc);
    void applyCursorMove(TextCursor newPos);
    void applySelectionMove(TextCursor newPos);
    TextCursor cursorFromPoint(const QPoint& pt) const;
    void ensureCursorVisible(TextCursor pos);
    int  pageLineCount() const;

    // --- Word-wrap helpers ---
    void rebuildWrapLayout();
    int  visualRowOf(TextCursor pos) const;

    // --- Highlighting helpers ---
    void rebuildHighlightCache();
    void rehighlightFrom(int startLine);

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

    // --- Painting helpers ---
    void paintLineBackgrounds(QPainter& painter);
    void paintSelection(QPainter& painter);
    QRegion selectionRegion() const;
};

} // namespace qce
