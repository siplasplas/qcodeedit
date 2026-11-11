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
#include <QMap>
#include <QRegion>
#include <QVector>

#include <functional>
#include <memory>

class QMenu;
class QTimer;
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
    ITextDocument* document() const;

    ViewportState viewportState() const;

    // --- Cursor ---
    TextCursor cursorPosition() const;
    void setCursorPosition(TextCursor pos);
    /// The text position at `viewportPoint` (in viewport() coordinates), as a
    /// click there would place the cursor; e.g. the word under a context-menu
    /// click: cursorAt(viewport()->mapFrom(this, pos)).
    TextCursor cursorAt(const QPoint& viewportPoint) const { return cursorFromPoint(viewportPoint); }

    // --- Selection ---
    bool hasSelection() const;
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
    QVector<ExtraSelection> extraSelections() const;

    /// Decorations in independent layers, so that e.g. spelling underlines and
    /// search matches do not replace each other; setExtraSelections(selections)
    /// is layer 0. Within a layer the rules above hold; across layers a higher
    /// layer overrides only the colors it sets (background, foreground,
    /// underline). All layers are cleared on any text change.
    void setExtraSelections(int layer, const QVector<ExtraSelection>& selections);
    QVector<ExtraSelection> extraSelections(int layer) const;
    /// Layer used for spelling underlines by qcodeedit-spell.
    static constexpr int SpellingLayer = 100;
    /// Layer used for grammar underlines by qcodeedit-spell.
    static constexpr int GrammarLayer = 101;

    /// The parts of `line` that may be spell checked, as [start, end) columns
    /// in order: the whole line without a highlighter, otherwise the spans
    /// whose attribute allows it (TextAttribute::spellCheck; with Kate
    /// definitions comments, strings and documentation in code, prose in
    /// markup). Highlights the document up to `line` when needed.
    QList<QPair<int, int>> spellCheckRanges(int line);

    void setSelectionColor(const QColor& color);
    QColor selectionColor() const;

    /// Per-line background provider. Returns the fill colour for a logical
    /// line, or an invalid QColor to leave the default. Intended for
    /// breakpoints, diff highlights, inline warnings, etc. The provider is
    /// queried during paint() only — callers drive repaints with viewport()->update()
    /// after changing the underlying state.
    using LineBackgroundFn = std::function<QColor(int line)>;
    void setLineBackgroundProvider(LineBackgroundFn fn);

    /// Background of the line with the caret; the text keeps its colours.
    /// Off by default (invalid QColor). A colour from the line background
    /// provider takes precedence on its lines.
    void setCurrentLineColor(const QColor& color);
    QColor currentLineColor() const;

    /// Called before user text enters the document: typed characters, paste
    /// and input-method commits. The filter may change `text`; returning
    /// false cancels the insertion. Used e.g. by qce::encoding::EncodingGuard
    /// to keep a legacy code page document representable. Pass an empty
    /// function to remove it.
    using InsertFilterFn = std::function<bool(QString& text)>;
    void setInsertFilter(InsertFilterFn fn);

    // --- Undo / redo ---
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    /// Exposes the undo stack for external wiring (e.g. menu enable/disable).
    QUndoStack* undoStack() const;

    // --- Editing ---
    /// Insert `text` at the cursor, replacing the selection, as one undo step
    /// (the insert filter is not applied). Does nothing in a read-only view.
    void insertText(const QString& text);
    /// A range of the text and what replaces it.
    struct TextReplacement {
        TextCursor start;
        TextCursor end;
        QString text;
    };
    /// Replaces the ranges (in the order of the text, not overlapping) with
    /// their texts as one undo step named `undoText` (Replace All): the lines
    /// with replacements are made anew, each run of them in one pass, so even
    /// many replacements in very long lines take linear time. The cursor goes
    /// after the first replacement. False, doing nothing, in a read-only view
    /// or with nothing to replace.
    bool replaceRanges(const QList<TextReplacement>& replacements, const QString& undoText = QString());
    /// Clipboard actions as Ctrl+X, Ctrl+C and Ctrl+V do them.
    void cut();
    void copy();
    void paste();
    /// Undo, Redo, Cut, Copy, Paste, Delete and Select All, enabled as they
    /// apply; the caller owns the menu and may add its own actions, e.g. in a
    /// handler of customContextMenuRequested.
    QMenu* createStandardContextMenu(QWidget* parent = nullptr);

    // --- Configuration ---
    void setTabWidth(int spaces);
    int  tabWidth() const;

    /// When true (default), Tab inserts a tab character and Shift+Tab removes
    /// one tab or up to tabWidth() spaces at the start of the line. When
    /// false, and in a read-only view, Tab / Shift+Tab move the focus as in
    /// other widgets. Ctrl+Tab and Shift+Ctrl+Tab always pass through (e.g.
    /// to switch tabs).
    void setTabCaptured(bool captured);
    bool tabCaptured() const;

    /// Grey hint shown while the document is empty (e.g. "Type a message");
    /// empty = none.
    void setPlaceholderText(const QString& text);
    QString placeholderText() const;

    /// The rows the text takes as drawn: wrapped rows with word wrap, lines
    /// without it (collapsed folds excluded); at least 1. E.g. for a field that
    /// grows with its text.
    int visualRowCount() const;
    /// The row `pos` is drawn in: its wrapped row with word wrap, its line
    /// without it. E.g. whether the cursor is in the first or last row:
    /// visualRow(cursorPosition()) == visualRowCount() - 1. Up and Down move
    /// by these rows.
    int visualRow(TextCursor pos) const { return visualRowOf(pos); }
    /// The rows of the viewport shown whole (a row cut off at the bottom does
    /// not count); at least 1.
    int pageLineCount() const;
    /// Shows `pos`, scrolling only when it is not in view, so that
    /// `marginRows` rows above or below it stay in view as well (fewer when
    /// the viewport has few rows): e.g. a search match not left on the last
    /// row. Rows: wrapped rows with word wrap, lines without it.
    void ensureVisible(TextCursor pos, int marginRows = 0);

    void setReadOnly(bool ro);
    bool readOnly() const;

    bool overwriteMode() const;

    void setWordWrap(bool wrap);
    bool wordWrap() const;

    void setShowWhitespace(bool show);
    bool showWhitespace() const;

    /// Attach a syntax highlighter (non-owning). Pass nullptr to disable
    /// highlighting. Schedules a full re-highlight of the document.
    void setHighlighter(IHighlighter* hl);
    IHighlighter* highlighter() const;

    /// Documents longer than this are highlighted lazily: visible lines when
    /// painted, the rest in the background (fold regions follow once the
    /// whole document is highlighted). Shorter ones count as highlighted at
    /// once: the work is deferred until they are painted, the event loop
    /// runs, or highlightedLineCount() or foldState() is asked, so that
    /// several changes in a row (document, highlighter, palette) cost one
    /// pass.
    static constexpr int kSyncHighlightLines = 5000;

    /// Number of lines from the top whose highlighting is up to date. Equals
    /// the document's line count when highlighting is complete.
    int highlightedLineCount() const;

    /// Attach a folding provider (non-owning). Pass nullptr to disable.
    /// Recomputes regions immediately.
    void setFoldingProvider(IFoldingProvider* p);
    IFoldingProvider* foldingProvider() const;

    /// Editor-owned fold state. Exposed so margins (gutter) can read it and
    /// invoke toggleFoldAt(). Non-const so FoldingGutter can call into it.
    FoldState& foldState();
    const FoldState& foldState() const;

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
    QString preeditString() const;

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void viewportChanged(const ViewportState& state);
    void cursorPositionChanged(TextCursor pos);
    void selectionChanged();
    /// The text changed: an edit, undo or redo, or a change made directly in
    /// the document (setText() as well). Emitted once the layout has caught up
    /// with it, so e.g. visualRowCount() is current; the cursor of an edit
    /// may move to its place only after it. An edit replacing a selection
    /// emits it twice: for the removal and for the insertion.
    void textChanged();
    /// Emitted first in the destructor, while the editor is still whole.
    /// Its child objects are deleted only after the editor's own part, when
    /// QObject::destroyed() comes, so a helper parented to the editor (e.g.
    /// qce::spell::CodeEditSpelling) drops its pointer to it here.
    void aboutToBeDestroyed();
    /// Background highlighting of a large document reached its end.
    void highlightingCompleted();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void changeEvent(QEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent* e) override;
    /// Qt handles Tab before keyPressEvent(), moving the focus, unless this
    /// returns false: it does while Tab is captured.
    bool focusNextPrevChild(bool next) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
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
    void onTextReplacementStarted();
    void onLinesReplaced(int first, const QStringList& removed, const QStringList& inserted);
    void onTextReplacementFinished();

private:
    // A new text set by changing the lines that differ: what it changed, and
    // where the cursor, the anchor and the top line of the view go.
    struct Replacement;
    // The state, in CodeEditArea.cpp: fields added there keep the binary
    // interface (the size of this class does not change).
    struct Private;
    std::unique_ptr<Private> d;

    QColor lineBackground(int line) const;
    void rebuildDecorations();
    void clearAllExtraSelections();

    // --- Navigation helpers ---
    void refreshViewportState();
    void updateScrollBarRanges();
    void rebindDocumentSignals(ITextDocument* newDoc);
    void applyCursorMove(TextCursor newPos);
    void applySelectionMove(TextCursor newPos);
    TextCursor cursorFromPoint(const QPoint& pt) const;
    /// `extraColumns` widens the area kept visible to the right of `pos`
    /// (used for the input-method pre-edit).
    void ensureCursorVisible(TextCursor pos, int extraColumns = 0, int marginRows = 0);

    // --- Word-wrap helpers ---
    void rebuildWrapLayout();
    /// The row layout is built (with and without word wrap: without it a row
    /// per visible line, so that folding works in both).
    bool hasRows() const;
    int  visualRowOf(TextCursor pos) const;
    /// The cell of the caret at `pos` in its row (LineRenderer::caretCell():
    /// in the order on the screen when the line has right-to-left text).
    int  caretCellOf(TextCursor pos) const;
    /// `delta` rows up (< 0) or down: wrapped rows with word wrap, keeping
    /// the character cell; lines without it. Unchanged at the first or last row.
    TextCursor moveVisualRows(TextCursor c, int delta) const;
    /// Home and End with word wrap: the screen row first, then the line.
    TextCursor moveToRowStart(TextCursor c) const;
    TextCursor moveToRowEnd(TextCursor c) const;

    // --- Highlighting helpers ---
    void rebuildHighlightCache();
    void rehighlightFrom(int startLine);
    /// Extend the highlighted prefix to at least `lineCount` lines.
    void highlightUpTo(int lineCount);
    /// One background slice; finishes folds and layout at the end.
    void highlightChunk();
    /// Stops the timer, builds pending folds and layout, emits the signal.
    void finishHighlighting();
    /// A short document whose highlighting was deferred is completed now.
    void finishShortHighlight() const;
    bool highlightComplete() const;
    /// min(startLine, pending changed line); clears the pending line.
    int  takePendingRehighlight(int startLine);
    void beginBatch();
    void endBatch();
    /// In a batch: remembers the earliest line to re-highlight from, true.
    bool batched(int startLine);

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
