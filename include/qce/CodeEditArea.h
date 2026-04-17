#pragma once

#include "TextCursor.h"
#include "ViewportState.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QRegion>

#include <memory>

class QUndoStack;

namespace qce {

class ITextDocument;
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

    void setInvertSelection(bool invert);
    bool invertSelection() const { return m_invertSelection; }

    void setSelectionForeground(const QColor& color);
    QColor selectionForeground() const { return m_selectionForeground; }

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
    QColor m_selectionColor{QStringLiteral("#94CAEF")};
    QColor m_selectionForeground{Qt::white};
    bool   m_invertSelection = false;
    bool   m_tabCaptured     = true;
    bool   m_readOnly        = false;
    bool   m_overwrite       = false;
    bool   m_wordWrap        = false;

    std::unique_ptr<LineRenderer>      m_renderer;
    std::unique_ptr<WrapLayout>        m_wrapLayout;
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
    void paintSelection(QPainter& painter);
    QRegion selectionRegion() const;
};

} // namespace qce
