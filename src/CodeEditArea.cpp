#include "qce/CodeEditArea.h"

#include "qce/CodePoints.h"
#include "qce/IFoldingProvider.h"
#include "qce/IHighlighter.h"
#include "qce/ITextDocument.h"
#include "qce/WordBoundary.h"
#include "CaretPainter.h"
#include "CursorController.h"
#include "EditCommands.h"
#include "LineRenderer.h"
#include "WrapLayout.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTimer>
#include <QToolTip>
#include <QUndoStack>

#include <algorithm>
#include <set>

namespace qce {

namespace {

// Columns are a grid of whole pixels (ViewportState::charWidth), but a font's
// advance is usually fractional (e.g. 7.81 px): text drawn as one run then
// drifts away from the caret, selection and mouse columns. Letter spacing
// makes every character exactly one grid cell wide.
QFont fitToCharGrid(QFont font) {
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    const qreal advance = QFontMetricsF(font).horizontalAdvance(QLatin1Char('M'));
    const int cell = qMax(1, qRound(advance));
    font.setLetterSpacing(QFont::AbsoluteSpacing, cell - advance);
    return font;
}

} // namespace

// ------------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------------

struct CodeEditArea::Replacement {
    QVector<ReplaceLinesCommand::Run> runs;
    TextCursor cursorBefore, anchorBefore;
    TextCursor cursor, anchor;
    int topLine = 0;
};

struct CodeEditArea::Private {
    std::unique_ptr<Replacement> m_replacement;
    ITextDocument* m_doc = nullptr;
    ViewportState  m_viewportState;
    TextCursor     m_cursor;
    TextCursor     m_anchor;
    bool           m_mouseSelecting = false;
    QColor m_selectionColor{QStringLiteral("#A6D2FF")};
    LineBackgroundFn m_lineBgProvider;
    QColor m_currentLineColor;
    QString m_placeholderText;
    InsertFilterFn m_insertFilter;
    // Normalized decorations per layer (empty layers are removed).
    QMap<int, QVector<ExtraSelection>> m_selectionLayers;
    // Disjoint, sorted segments per logical line, all layers merged; overlap
    // resolved on bulk update.
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
    /// Document changes made as one (a text replaced by its differing lines,
    /// its undo and redo, replaceRanges()): while the depth is above 0 each
    /// change only moves the per-line caches; the highlighting, the folds,
    /// the wrap layout and the view follow once, at the end, from the
    /// earliest line changed (-1: none).
    int                                m_batchDepth = 0;
    int                                m_batchRehighlightFrom = -1;
    /// Lines [0, m_validLines) have up-to-date spans, end states and markers.
    int                                m_validLines = 0;
    /// Folds wait for the highlighting to reach the end of the document.
    bool                               m_foldsPending = false;
    QTimer*                            m_highlightTimer = nullptr;

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
};

CodeEditArea::CodeEditArea(QWidget* parent)
    : QAbstractScrollArea(parent),
      d(std::make_unique<Private>()) {
    d->m_renderer = std::make_unique<LineRenderer>();
    d->m_wrapLayout = std::make_unique<WrapLayout>();
    d->m_cursorCtrl = std::make_unique<CursorController>(nullptr);
    d->m_caretPainter = std::make_unique<CaretPainter>(this);
    d->m_undoStack = new QUndoStack(this);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QStringList families = QFontDatabase::families();
    f.setStyleHint(QFont::TypeWriter);
    f = f.resolve(font());
    f.setPointSizeF(10);
    setFont(fitToCharGrid(f));
    d->m_renderer->setFont(font());

    viewport()->setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    // The area (not the viewport) has the focus, so it receives
    // QInputMethodEvent and answers inputMethodQuery().
    setAttribute(Qt::WA_InputMethodEnabled, true);

    connect(d->m_caretPainter.get(), &CaretPainter::blinkToggled,
            viewport(), QOverload<>::of(&QWidget::update));

    d->m_highlightTimer = new QTimer(this);
    d->m_highlightTimer->setInterval(0);
    connect(d->m_highlightTimer, &QTimer::timeout, this, &CodeEditArea::highlightChunk);

    d->m_renderer->setDecorationsProvider([this](int line) -> const QVector<ExtraSelection>* {
        auto it = d->m_extraSelectionsByLine.constFind(line);
        return it == d->m_extraSelectionsByLine.cend() ? nullptr : &it.value();
    });
    refreshViewportState();
}

CodeEditArea::~CodeEditArea() {
    emit aboutToBeDestroyed();
}

// --- Accessors (out of line: they read the fields of Private) ------------

ITextDocument* CodeEditArea::document() const { return d->m_doc; }
ViewportState CodeEditArea::viewportState() const { return d->m_viewportState; }
TextCursor CodeEditArea::cursorPosition() const { return d->m_cursor; }
bool CodeEditArea::hasSelection() const { return d->m_anchor != d->m_cursor; }
QVector<ExtraSelection> CodeEditArea::extraSelections() const { return d->m_selectionLayers.value(0); }
QVector<ExtraSelection> CodeEditArea::extraSelections(int layer) const { return d->m_selectionLayers.value(layer); }
QColor CodeEditArea::selectionColor() const { return d->m_selectionColor; }
QColor CodeEditArea::currentLineColor() const { return d->m_currentLineColor; }
void CodeEditArea::setInsertFilter(InsertFilterFn fn) { d->m_insertFilter = std::move(fn); }
QUndoStack* CodeEditArea::undoStack() const { return d->m_undoStack; }
bool CodeEditArea::tabCaptured() const { return d->m_tabCaptured; }
QString CodeEditArea::placeholderText() const { return d->m_placeholderText; }
bool CodeEditArea::readOnly() const { return d->m_readOnly; }
bool CodeEditArea::overwriteMode() const { return d->m_overwrite; }
bool CodeEditArea::wordWrap() const { return d->m_wordWrap; }
IHighlighter* CodeEditArea::highlighter() const { return d->m_highlighter; }
IFoldingProvider* CodeEditArea::foldingProvider() const { return d->m_foldingProvider; }
FoldState& CodeEditArea::foldState() { finishShortHighlight(); return d->m_foldState; }
const FoldState& CodeEditArea::foldState() const { finishShortHighlight(); return d->m_foldState; }
QString CodeEditArea::preeditString() const { return d->m_preedit; }

// ------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------

void CodeEditArea::setDocument(ITextDocument* doc) {
    if (d->m_doc == doc) {
        return;
    }
    cancelPreedit(true);
    clearAllExtraSelections();
    rebindDocumentSignals(doc);
    d->m_doc = doc;
    d->m_cursorCtrl->setDocument(doc);
    d->m_cursor = d->m_cursorCtrl->clamp(d->m_cursor);
    d->m_anchor = d->m_cursor;
    d->m_undoStack->clear();

    d->m_knownLineCount = doc ? doc->lineCount() : 0;
    d->m_pendingRehighlightFrom = -1;
    rebuildHighlightCache();
    d->m_foldState.clear();
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod();
}

void CodeEditArea::setCursorPosition(TextCursor pos) {
    cancelPreedit(true);
    applyCursorMove(pos);
}

// --- Selection ----------------------------------------------------------

TextCursor CodeEditArea::selectionStart() const {
    return (d->m_anchor <= d->m_cursor) ? d->m_anchor : d->m_cursor;
}

TextCursor CodeEditArea::selectionEnd() const {
    return (d->m_anchor <= d->m_cursor) ? d->m_cursor : d->m_anchor;
}

QString CodeEditArea::selectedText() const {
    if (!hasSelection() || !d->m_doc) {
        return {};
    }
    const TextCursor s = selectionStart();
    const TextCursor e = selectionEnd();

    if (s.line == e.line) {
        return d->m_doc->lineAt(s.line).mid(s.column, e.column - s.column);
    }

    QString result = d->m_doc->lineAt(s.line).mid(s.column);
    for (int i = s.line + 1; i < e.line; ++i) {
        result += QLatin1Char('\n') + d->m_doc->lineAt(i);
    }
    result += QLatin1Char('\n') + d->m_doc->lineAt(e.line).left(e.column);
    return result;
}

void CodeEditArea::setSelection(TextCursor anchor, TextCursor cursor) {
    anchor = d->m_cursorCtrl->clamp(anchor);
    cursor = d->m_cursorCtrl->clamp(cursor);
    const bool cursorChanged = cursor != d->m_cursor;
    const bool selectionChangedValue = anchor != d->m_anchor || cursorChanged;
    d->m_anchor = anchor;
    d->m_cursor = cursor;
    d->m_caretPainter->resetBlink();
    ensureCursorVisible(cursor);
    viewport()->update();
    updateInputMethod();
    if (cursorChanged) emit cursorPositionChanged(cursor);
    if (selectionChangedValue) emit selectionChanged();
}

void CodeEditArea::setExtraSelections(const QVector<ExtraSelection>& selections) {
    setExtraSelections(0, selections);
}

void CodeEditArea::setExtraSelections(int layer, const QVector<ExtraSelection>& selections) {
    QVector<ExtraSelection> normalized;
    if (d->m_doc && d->m_doc->lineCount() > 0) {
        for (auto selection : selections) {
            selection.start = d->m_cursorCtrl->clamp(selection.start);
            selection.end = d->m_cursorCtrl->clamp(selection.end);
            if (selection.end < selection.start) std::swap(selection.start, selection.end);
            if (selection.start == selection.end) continue;
            normalized.append(selection);
        }
    }
    if (normalized.isEmpty()) d->m_selectionLayers.remove(layer);
    else d->m_selectionLayers.insert(layer, normalized);
    rebuildDecorations();
}

void CodeEditArea::clearAllExtraSelections() {
    if (d->m_selectionLayers.isEmpty()) return;
    d->m_selectionLayers.clear();
    rebuildDecorations();
}

void CodeEditArea::rebuildDecorations() {
    d->m_extraSelectionsByLine.clear();
    if (!d->m_doc || d->m_doc->lineCount() == 0 || d->m_selectionLayers.isEmpty()) {
        viewport()->update();
        return;
    }
    // Each decoration as (layer, index); active ones are ordered by both, so
    // the last of a layer is that layer's top entry.
    struct Event { int column; int layer; int index; bool opening; };
    QHash<int, QVector<Event>> eventsByLine;
    for (auto layer = d->m_selectionLayers.cbegin(); layer != d->m_selectionLayers.cend(); ++layer) {
        for (int index = 0; index < layer->size(); ++index) {
            const ExtraSelection& selection = layer->at(index);
            for (int line = selection.start.line; line <= selection.end.line; ++line) {
                const int start = line == selection.start.line ? selection.start.column : 0;
                const int end = line == selection.end.line ? selection.end.column : d->m_doc->lineAt(line).size();
                if (start >= end) continue;
                eventsByLine[line].append({start, layer.key(), index, true});
                eventsByLine[line].append({end, layer.key(), index, false});
            }
        }
    }
    // Within a layer the later entry wins as a complete style; a higher layer
    // overrides only the colors it sets.
    auto merged = [this](const std::set<std::pair<int, int>>& active) {
        ExtraSelection style;
        for (auto it = active.cbegin(); it != active.cend(); ++it) {
            const auto next = std::next(it);
            if (next != active.cend() && next->first == it->first) continue;  // not the layer's top
            const ExtraSelection& top = d->m_selectionLayers[it->first][it->second];
            if (top.background.isValid()) style.background = top.background;
            if (top.foreground.isValid()) style.foreground = top.foreground;
            if (top.underline.isValid()) {
                style.underline = top.underline;
                style.straightUnderline = top.straightUnderline;
            }
        }
        return style;
    };
    for (auto it = eventsByLine.begin(); it != eventsByLine.end(); ++it) {
        auto& events = it.value();
        std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.column < b.column;
        });
        std::set<std::pair<int, int>> active;
        int previous = 0;
        auto& segments = d->m_extraSelectionsByLine[it.key()];
        for (qsizetype i = 0; i < events.size();) {
            const int column = events[i].column;
            if (previous < column && !active.empty()) {
                ExtraSelection style = merged(active);
                style.start = {it.key(), previous};
                style.end = {it.key(), column};
                segments.append(style);
            }
            while (i < events.size() && events[i].column == column) {
                if (events[i].opening) active.insert({events[i].layer, events[i].index});
                else active.erase({events[i].layer, events[i].index});
                ++i;
            }
            previous = column;
        }
    }
    viewport()->update();
}

void CodeEditArea::selectAll() {
    if (!d->m_doc || d->m_doc->lineCount() == 0) {
        return;
    }
    d->m_anchor = {0, 0};
    const int last = d->m_doc->lineCount() - 1;
    d->m_cursor = d->m_cursorCtrl->clamp({last, INT_MAX});
    d->m_caretPainter->resetBlink();
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(d->m_cursor);
    emit selectionChanged();
}

void CodeEditArea::clearSelection() {
    if (!hasSelection()) {
        return;
    }
    d->m_anchor = d->m_cursor;
    viewport()->update();
    updateInputMethod();
    emit selectionChanged();
}

void CodeEditArea::setSelectionColor(const QColor& color) {
    d->m_selectionColor = color;
    if (hasSelection()) {
        viewport()->update();
    }
}

void CodeEditArea::setLineBackgroundProvider(LineBackgroundFn fn) {
    d->m_lineBgProvider = std::move(fn);
    viewport()->update();
}

void CodeEditArea::setCurrentLineColor(const QColor& color) {
    d->m_currentLineColor = color;
    viewport()->update();
}

QColor CodeEditArea::lineBackground(int line) const {
    if (d->m_lineBgProvider) {
        const QColor bg = d->m_lineBgProvider(line);
        if (bg.isValid()) return bg;
    }
    return line == d->m_cursor.line ? d->m_currentLineColor : QColor();
}

// --- Undo / redo --------------------------------------------------------

void CodeEditArea::undo() {
    if (d->m_readOnly) { showReadOnlyHint(); return; }
    if (!d->m_undoStack->canUndo()) {
        return;
    }
    d->m_undoStack->undo();
    updateAfterEdit();
}

void CodeEditArea::redo() {
    if (d->m_readOnly) { showReadOnlyHint(); return; }
    if (!d->m_undoStack->canRedo()) {
        return;
    }
    d->m_undoStack->redo();
    updateAfterEdit();
}

bool CodeEditArea::canUndo() const { return d->m_undoStack->canUndo(); }
bool CodeEditArea::canRedo() const { return d->m_undoStack->canRedo(); }

// --- Editing ------------------------------------------------------------

void CodeEditArea::insertText(const QString& text) {
    if (text.isEmpty() && !hasSelection()) return;
    executeInsert(text);
}

bool CodeEditArea::replaceRanges(const QList<TextReplacement>& replacements, const QString& undoText) {
    if (!d->m_doc || replacements.isEmpty()) return false;
    if (d->m_readOnly) {
        showReadOnlyHint();
        return false;
    }
    cancelPreedit(true);
    // Runs of lines (a run goes on over the next line when that has a
    // replacement too), each made anew in one pass: its old lines joined,
    // the new text's length known first (the old one plus each
    // replacement's growth), so allocated once, the text between
    // replacements copied once, shifted by the growth so far.
    QVector<ReplaceLinesCommand::Run> runs;
    TextCursor cursorAfter = d->m_cursor;
    for (qsizetype first = 0; first < replacements.size();) {
        const int firstLine = replacements[first].start.line;
        int lastLine = replacements[first].end.line;
        qsizetype last = first;
        while (last + 1 < replacements.size() && replacements[last + 1].start.line <= lastLine + 1) {
            ++last;
            lastLine = std::max(lastLine, replacements[last].end.line);
        }
        ReplaceLinesCommand::Run run;
        run.first = firstLine;
        QVector<qsizetype> lineOffsets;  // of each line in the joined text
        qsizetype oldLength = 0;
        for (int line = firstLine; line <= lastLine; ++line) {
            run.removed << d->m_doc->lineAt(line);
            lineOffsets << oldLength;
            oldLength += run.removed.back().size() + 1;
        }
        QString old;
        old.reserve(oldLength);
        for (const QString& line : std::as_const(run.removed)) {
            if (!old.isEmpty() || &line != &run.removed.front()) old += QLatin1Char('\n');
            old += line;
        }
        const auto offsetOf = [&](TextCursor at) { return lineOffsets[at.line - firstLine] + at.column; };
        qsizetype length = old.size();
        for (qsizetype i = first; i <= last; ++i)
            length += replacements[i].text.size() - (offsetOf(replacements[i].end) - offsetOf(replacements[i].start));
        QString built;
        built.reserve(length);
        qsizetype copied = 0;
        for (qsizetype i = first; i <= last; ++i) {
            const qsizetype start = offsetOf(replacements[i].start);
            built += QStringView(old).mid(copied, start - copied);
            built += replacements[i].text;
            copied = offsetOf(replacements[i].end);
            if (i == 0) {
                const qsizetype newline = built.lastIndexOf(QLatin1Char('\n'));
                cursorAfter = {firstLine + int(built.count(QLatin1Char('\n'))),
                               int(newline < 0 ? built.size() : built.size() - newline - 1)};
            }
        }
        built += QStringView(old).mid(copied);
        run.inserted = built.split(QLatin1Char('\n'));
        runs.append(std::move(run));
        first = last + 1;
    }
    // From the last run up: the line numbers of the runs above hold.
    std::reverse(runs.begin(), runs.end());
    const TextCursor cursorBefore = d->m_cursor, anchorBefore = d->m_anchor;
    beginBatch();
    for (const ReplaceLinesCommand::Run& run : std::as_const(runs))
        d->m_doc->replaceLines(run.first, int(run.removed.size()), run.inserted);
    endBatch();
    d->m_cursor = d->m_anchor = d->m_cursorCtrl->clamp(cursorAfter);
    auto* command = new ReplaceLinesCommand(d->m_doc, runs, cursorBefore, anchorBefore, d->m_cursor, d->m_anchor,
                                            &d->m_cursor, &d->m_anchor, [this]() { beginBatch(); },
                                            [this]() { endBatch(); });
    command->setText(undoText);
    d->m_undoStack->push(command);
    updateAfterEdit();
    return true;
}

void CodeEditArea::cut() {
    if (!hasSelection()) return;
    if (d->m_readOnly) { showReadOnlyHint(); return; }
    QGuiApplication::clipboard()->setText(selectedText());
    executeRemoveSelection();
}

void CodeEditArea::copy() {
    if (hasSelection()) QGuiApplication::clipboard()->setText(selectedText());
}

void CodeEditArea::paste() {
    QString text = QGuiApplication::clipboard()->text();
    // The filter (e.g. an encoding check) may change or cancel it; a
    // read-only view only shows its hint.
    const bool accepted = d->m_readOnly || !d->m_insertFilter || d->m_insertFilter(text);
    if (accepted && !text.isEmpty()) {
        executeInsert(text);
    }
}

QMenu* CodeEditArea::createStandardContextMenu(QWidget* parent) {
    auto* menu = new QMenu(parent ? parent : this);
    const bool editable = d->m_doc && !d->m_readOnly;
    menu->addAction(tr("&Undo"), QKeySequence::Undo, this, &CodeEditArea::undo)
        ->setEnabled(editable && canUndo());
    menu->addAction(tr("&Redo"), QKeySequence::Redo, this, &CodeEditArea::redo)
        ->setEnabled(editable && canRedo());
    menu->addSeparator();
    menu->addAction(tr("Cu&t"), QKeySequence::Cut, this, &CodeEditArea::cut)
        ->setEnabled(editable && hasSelection());
    menu->addAction(tr("&Copy"), QKeySequence::Copy, this, &CodeEditArea::copy)
        ->setEnabled(hasSelection());
    menu->addAction(tr("&Paste"), QKeySequence::Paste, this, &CodeEditArea::paste)
        ->setEnabled(editable && !QGuiApplication::clipboard()->text().isEmpty());
    menu->addAction(tr("Delete"), this, &CodeEditArea::executeRemoveSelection)
        ->setEnabled(editable && hasSelection());
    menu->addSeparator();
    menu->addAction(tr("Select &All"), QKeySequence::SelectAll, this, &CodeEditArea::selectAll)
        ->setEnabled(d->m_doc != nullptr);
    return menu;
}

// --- Configuration ------------------------------------------------------

void CodeEditArea::setTabWidth(int spaces) {
    d->m_renderer->setTabWidth(spaces);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

int CodeEditArea::tabWidth() const {
    return d->m_renderer->tabWidth();
}

void CodeEditArea::setPlaceholderText(const QString& text) {
    if (d->m_placeholderText == text) return;
    d->m_placeholderText = text;
    viewport()->update();
}

int CodeEditArea::visualRowCount() const {
    if (!d->m_doc) return 1;
    // The wrap layout is empty until the viewport has a width.
    if (hasRows()) return d->m_wrapLayout->totalRows();
    int rows = 0;
    for (int line = 0; line < d->m_doc->lineCount(); ++line)
        if (d->m_foldState.isLineVisible(line)) ++rows;
    return qMax(1, rows);
}

void CodeEditArea::setTabCaptured(bool captured) {
    d->m_tabCaptured = captured;
}

bool CodeEditArea::focusNextPrevChild(bool next) {
    // Tab and Shift+Tab (without Ctrl or Alt) then reach keyPressEvent().
    if (d->m_tabCaptured && !d->m_readOnly && d->m_doc) return false;
    return QAbstractScrollArea::focusNextPrevChild(next);
}

void CodeEditArea::setReadOnly(bool ro) {
    if (d->m_readOnly == ro) return;
    d->m_readOnly = ro;
    // Like QPlainTextEdit: a read-only area takes no input-method text.
    setAttribute(Qt::WA_InputMethodEnabled, !ro);
    if (ro) cancelPreedit(true);
    updateInputMethod(Qt::ImEnabled);
}

void CodeEditArea::setShowWhitespace(bool show) {
    d->m_renderer->setShowWhitespace(show);
    viewport()->update();
}

bool CodeEditArea::showWhitespace() const {
    return d->m_renderer->showWhitespace();
}

void CodeEditArea::setHighlighter(IHighlighter* hl) {
    d->m_highlighter = hl;
    if (hl) {
        d->m_renderer->setAttributePalette(&hl->attributes());
        d->m_renderer->setSpansProvider([this](int line) -> const QVector<StyleSpan>* {
            if (line < 0 || line >= d->m_validLines || line >= d->m_lineSpans.size()) return nullptr;
            return &d->m_lineSpans[line];
        });
    } else {
        d->m_renderer->setAttributePalette(nullptr);
        d->m_renderer->setSpansProvider({});
    }
    rebuildHighlightCache();
    viewport()->update();
}

void CodeEditArea::setFoldingProvider(IFoldingProvider* p) {
    d->m_foldingProvider = p;
    d->m_foldState.clear();
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::toggleFoldAt(int line) {
    const int idx = d->m_foldState.regionStartingAt(line);
    if (idx < 0) return;
    d->m_foldState.toggle(idx);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::foldAll() {
    d->m_foldState.foldAll();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::unfoldAll() {
    d->m_foldState.unfoldAll();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::setWordWrap(bool wrap) {
    if (d->m_wordWrap == wrap) return;
    d->m_wordWrap = wrap;
    horizontalScrollBar()->setVisible(!wrap);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::setCaretBlinkInterval(int ms) {
    d->m_caretPainter->setBlinkInterval(ms);
}

int CodeEditArea::caretBlinkInterval() const {
    return d->m_caretPainter->blinkInterval();
}

// ------------------------------------------------------------------------
// Paint
// ------------------------------------------------------------------------

void CodeEditArea::paintEvent(QPaintEvent* e) {
    QPainter p(viewport());
    p.fillRect(e->rect(), palette().base());
    paintLineBackgrounds(p);
    paintSelection(p);
    // Grey hint in an empty document, where the first line would be.
    if (!d->m_placeholderText.isEmpty() && d->m_doc && d->m_doc->lineCount() <= 1
            && d->m_doc->lineAt(0).isEmpty() && d->m_preedit.isEmpty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        const int lineHeight = LineRenderer::lineHeightFor(font());
        p.drawText(QRect(LineRenderer::kLeftPaddingPx, 0,
                         viewport()->width() - LineRenderer::kLeftPaddingPx, lineHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, d->m_placeholderText);
    }
    p.setPen(palette().text().color());
    d->m_renderer->setSelectionRegion(selectionRegion());
    // Lazy highlighting: make sure every visible line is highlighted; a short
    // document all of it, so that it is never shown partly highlighted.
    if (d->m_highlighter && d->m_doc && !highlightComplete()
            && d->m_lineEndStates.size() == d->m_doc->lineCount()) {
        const int n = d->m_doc->lineCount();
        highlightUpTo(n <= kSyncHighlightLines
                      ? n : qMax(d->m_viewportState.lastVisibleLine, d->m_cursor.line) + 1);
    }
    d->m_renderer->paint(p, d->m_doc, d->m_viewportState);
    paintPreedit(p);
    if (!d->m_preedit.isEmpty() && !d->m_preeditCursorVisible) {
        return;  // the input method asked to hide the caret
    }
    int caretVisualCol, caretVisualRow;
    if (d->m_doc) {
        caretVisualRow = visualRowOf(d->m_cursor);
        caretVisualCol = caretCellOf(d->m_cursor);
    } else {
        caretVisualRow = d->m_cursor.line;
        caretVisualCol = d->m_cursor.column;
    }
    d->m_caretPainter->paint(p, d->m_cursor, caretVisualCol, caretVisualRow, d->m_viewportState, font(),
                          d->m_preedit.isEmpty() ? 0 : preeditCursorOffsetPx());
}

void CodeEditArea::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::changeEvent(QEvent* e) {
    QAbstractScrollArea::changeEvent(e);
    if (e->type() != QEvent::FontChange) return;
    // A font set by the application is fitted to the grid as well; setting
    // the fitted font comes back here and finds nothing to change.
    const QFont fitted = fitToCharGrid(font());
    if (fitted.letterSpacing() != font().letterSpacing()
            || fitted.letterSpacingType() != font().letterSpacingType()) {
        setFont(fitted);
        return;
    }
    d->m_renderer->setFont(font());
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle | Qt::ImFont);
}

void CodeEditArea::scrollContentsBy(int dx, int dy) {
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

// ------------------------------------------------------------------------
// Key handling
// ------------------------------------------------------------------------

void CodeEditArea::keyPressEvent(QKeyEvent* e) {
    if (!d->m_doc) {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }

    const bool ctrl  = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool alt   = e->modifiers() & Qt::AltModifier;
    // Windows reports AltGr as Ctrl+Alt: Ctrl shortcuts must not swallow
    // AltGr characters (Polish ą = AltGr+A, ć = AltGr+C, ż = AltGr+Z, …).
    const bool cmd   = ctrl && !alt;
    const CursorController& cc = *d->m_cursorCtrl;
    const TextCursor c = d->m_cursor;

    auto move = [&](TextCursor next) {
        if (shift) applySelectionMove(next);
        else       applyCursorMove(next);
    };

    switch (e->key()) {
    // --- Navigation ---
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int delta = e->key() == Qt::Key_Up ? -1 : 1;
        if (!cmd) {
            move(moveVisualRows(c, delta));
            break;
        }
        // Ctrl+Up/Down: the text scrolls a row, the cursor stays where it is in
        // the document while it is on screen; one that would go off it stays
        // on the first or last row of the screen (as in Kate), so that the
        // next arrow key does not jump back to it
        QScrollBar* vBar = verticalScrollBar();
        vBar->setValue(vBar->value() + delta);
        const int first = vBar->value();
        const int last  = first + pageLineCount() - 1;
        const int row   = visualRowOf(c);
        if (row < first)     move(moveVisualRows(c, first - row));
        else if (row > last) move(moveVisualRows(c, last - row));
        break;
    }
    case Qt::Key_Left:
        move(ctrl ? cc.moveWordLeft(c) : cc.moveLeft(c));
        break;
    case Qt::Key_Right:
        move(ctrl ? cc.moveWordRight(c) : cc.moveRight(c));
        break;
    case Qt::Key_Home:     move(ctrl ? cc.moveToDocumentStart(c)
                                     : moveToRowStart(c));                  break;
    case Qt::Key_End:      move(ctrl ? cc.moveToDocumentEnd(c)
                                     : moveToRowEnd(c));                    break;
    case Qt::Key_PageUp:   move(cc.movePageUp(c, pageLineCount()));         break;
    case Qt::Key_PageDown: move(cc.movePageDown(c, pageLineCount()));       break;

    // --- Edit ---
    case Qt::Key_Return:
    case Qt::Key_Enter:
        executeInsert(QStringLiteral("\n"));
        break;

    case Qt::Key_Backspace:
        if (hasSelection()) {
            executeRemoveSelection();
        } else if (ctrl) {
            // Delete to the previous word stop (joins lines at column 0).
            const TextCursor from = cc.moveWordLeft(d->m_cursor);
            if (from != d->m_cursor) executeRemove(from, d->m_cursor);
        } else if (d->m_cursor.column > 0) {
            // One code point (a surrogate pair as a whole).
            const int from = codepoints::prevIndex(d->m_doc->lineAt(d->m_cursor.line), d->m_cursor.column);
            executeRemove({d->m_cursor.line, from}, d->m_cursor);
        } else if (d->m_cursor.line > 0) {
            const int prevLen = d->m_doc->lineAt(d->m_cursor.line - 1).size();
            executeRemove({d->m_cursor.line - 1, prevLen}, d->m_cursor);
        }
        break;

    case Qt::Key_Delete:
        if (hasSelection()) {
            executeRemoveSelection();
        } else if (ctrl) {
            // Delete to the next word stop (joins lines at the line end).
            const TextCursor to = cc.moveWordRight(d->m_cursor);
            if (to != d->m_cursor) executeRemove(d->m_cursor, to);
        } else {
            const QString lineText = d->m_doc->lineAt(d->m_cursor.line);
            if (d->m_cursor.column < lineText.size()) {
                // One code point (a surrogate pair as a whole).
                executeRemove(d->m_cursor, {d->m_cursor.line, codepoints::nextIndex(lineText, d->m_cursor.column)});
            } else if (d->m_cursor.line < d->m_doc->lineCount() - 1) {
                executeRemove(d->m_cursor, {d->m_cursor.line + 1, 0});
            }
        }
        break;

    case Qt::Key_Tab:
        // Ctrl+Tab always passes through (tab switching in host application).
        if (ctrl) {
            QAbstractScrollArea::keyPressEvent(e);
            return;
        }
        if (d->m_tabCaptured) {
            executeInsert(QStringLiteral("\t"));
        } else {
            QAbstractScrollArea::keyPressEvent(e);
            return;
        }
        break;

    case Qt::Key_Backtab: // Shift+Tab
        // Ctrl+Shift+Tab always passes through.
        if (ctrl) {
            QAbstractScrollArea::keyPressEvent(e);
            return;
        }
        if (d->m_tabCaptured) {
            // Dedent: remove one leading tab, or up to tabWidth leading spaces.
            const QString line = d->m_doc->lineAt(d->m_cursor.line);
            if (!line.isEmpty() && line.at(0) == QLatin1Char('\t')) {
                executeRemove({d->m_cursor.line, 0}, {d->m_cursor.line, 1});
            } else {
                int spaces = 0;
                while (spaces < tabWidth() && spaces < line.size()
                       && line.at(spaces) == QLatin1Char(' ')) {
                    ++spaces;
                }
                if (spaces > 0) {
                    executeRemove({d->m_cursor.line, 0}, {d->m_cursor.line, spaces});
                }
            }
        } else {
            QAbstractScrollArea::keyPressEvent(e);
            return;
        }
        break;

    // --- Clipboard / undo ---
    case Qt::Key_A:
        if (cmd) { selectAll(); break; }
        goto handle_printable;

    case Qt::Key_C:
        if (cmd && hasSelection()) {
            copy();
            break;
        }
        goto handle_printable;

    case Qt::Key_X:
        if (cmd && hasSelection()) {
            cut();
            break;
        }
        goto handle_printable;

    case Qt::Key_V:
        if (cmd) {
            paste();
            break;
        }
        goto handle_printable;

    case Qt::Key_Insert:
        d->m_overwrite = !d->m_overwrite;
        d->m_caretPainter->setOverwrite(d->m_overwrite);
        break;

    case Qt::Key_Minus:
        if (cmd && !shift) { toggleFoldAt(d->m_cursor.line); break; }
        goto handle_printable;

    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (cmd && !shift) { toggleFoldAt(d->m_cursor.line); break; }
        goto handle_printable;

    case Qt::Key_Z:
        if (cmd && !shift) { undo(); break; }
        if (cmd &&  shift) { redo(); break; }
        goto handle_printable;

    case Qt::Key_Y:
        if (cmd) { redo(); break; }
        goto handle_printable;

    default:
    handle_printable: {
        const QString text = e->text();
        const bool altGr = (ctrl && alt) || (e->modifiers() & Qt::GroupSwitchModifier);
        if (!text.isEmpty() && ((!ctrl && !alt) || altGr) && isInsertableText(text)) {
            insertTypedText(text);
            break;
        }
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    }

    e->accept();
}

// ------------------------------------------------------------------------
// Mouse handling
// ------------------------------------------------------------------------

void CodeEditArea::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (!d->m_preedit.isEmpty()) {
            // Like QPlainTextEdit: a click finishes the composition where it is.
            QGuiApplication::inputMethod()->commit();
            cancelPreedit(false);
        }
        d->m_mouseSelecting = false;
        // Placeholder hit-test: if the click falls on a collapsed region's
        // "{…}" box, unfold it instead of moving the cursor.
        if (d->m_viewportState.isValid() && !d->m_viewportState.rows.isEmpty()
                && d->m_viewportState.lineHeight > 0 && d->m_doc) {
            const int rowIdx = e->pos().y() / d->m_viewportState.lineHeight;
            if (rowIdx >= 0 && rowIdx < d->m_viewportState.rows.size()) {
                const auto& row = d->m_viewportState.rows[rowIdx];
                if (!row.foldPlaceholder.isEmpty()) {
                    const QString& line = d->m_doc->lineAt(row.logicalLine);
                    const int drawEnd = (row.foldStartColumn >= 0)
                        ? qMin((int)row.endCol, row.foldStartColumn)
                        : row.endCol;
                    const int visLen = LineRenderer::visualColumn(
                        line, drawEnd, tabWidth())
                        - LineRenderer::visualColumn(line, row.startCol, tabWidth());
                    const int phX = LineRenderer::kLeftPaddingPx
                                    + visLen * d->m_viewportState.charWidth;
                    const QFontMetrics fm(font());
                    const int phW = fm.horizontalAdvance(row.foldPlaceholder) + 6;
                    if (e->pos().x() >= phX && e->pos().x() <= phX + phW) {
                        toggleFoldAt(row.logicalLine);
                        e->accept();
                        return;
                    }
                }
            }
        }
        d->m_mouseSelecting = true;
        const TextCursor pos = cursorFromPoint(e->pos());
        if (e->modifiers() & Qt::ShiftModifier) {
            applySelectionMove(pos);
        } else {
            applyCursorMove(pos);
        }
        e->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(e);
}

// A double click selects the word under the mouse (letters, digits, '_'; see
// words::isWordCharAt), or the run of other characters there.
void CodeEditArea::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !d->m_doc) {
        QAbstractScrollArea::mouseDoubleClickEvent(e);
        return;
    }
    d->m_mouseSelecting = false;
    const TextCursor pos = cursorFromPoint(e->pos());
    const QString& line = d->m_doc->lineAt(pos.line);
    int start = pos.column, end = pos.column;
    // The character clicked on (the cursor goes to the nearest boundary, so
    // a click on a character's right half gives the column after it); at the
    // end of the line the one before it.
    const int half = qMax(1, d->m_viewportState.charWidth / 2);
    const TextCursor left = cursorFromPoint(e->pos() - QPoint(half, 0));
    int at = left.line == pos.line && left.column < pos.column ? pos.column - 1 : pos.column;
    at = qMin(at, int(line.size()) - 1);
    if (at >= 0 && !line[at].isSpace()) {
        const bool word = words::isWordCharAt(line, at);
        const auto same = [&](int i) {
            return !line[i].isSpace() && words::isWordCharAt(line, i) == word;
        };
        start = at;
        end = at + 1;
        while (start > 0 && same(start - 1)) --start;
        while (end < line.size() && same(end)) ++end;
    }
    setSelection({pos.line, start}, {pos.line, end});
    e->accept();
}

void CodeEditArea::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        if (d->m_mouseSelecting) {
            applySelectionMove(cursorFromPoint(e->pos()));
        }
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(e);
}

void CodeEditArea::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        d->m_mouseSelecting = false;
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void CodeEditArea::focusInEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusInEvent(e);
    d->m_caretPainter->setFocused(true);
}

void CodeEditArea::focusOutEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusOutEvent(e);
    d->m_caretPainter->setFocused(false);
    // Qt commits the composition before the focus moves; whatever is left
    // is stale.
    cancelPreedit(false);
}

// ------------------------------------------------------------------------
// Input methods
// ------------------------------------------------------------------------

void CodeEditArea::inputMethodEvent(QInputMethodEvent* e) {
    if (!d->m_doc || d->m_readOnly) {
        cancelPreedit(false);
        e->ignore();
        return;
    }

    // Replacement range: relative to the cursor, limited to the current line.
    const int line = d->m_cursor.line;
    const int lineLen = d->m_doc->lineAt(line).size();
    int repFrom = 0, repTo = 0;
    if (e->replacementLength() > 0) {
        repFrom = qBound(0, d->m_cursor.column + e->replacementStart(), lineLen);
        repTo   = qBound(repFrom,
                         d->m_cursor.column + e->replacementStart() + e->replacementLength(),
                         lineLen);
    }

    const QString commit = e->commitString();
    if (!commit.isEmpty() || repFrom < repTo) {
        // Replacement and commit form a single undo step.
        d->m_undoStack->beginMacro(QString());
        if (repFrom < repTo) executeRemove({line, repFrom}, {line, repTo});
        if (!commit.isEmpty()) insertTypedText(commit);
        d->m_undoStack->endMacro();
    }

    d->m_preedit = e->preeditString();
    d->m_preeditAttributes = e->attributes();
    d->m_preeditCursor = d->m_preedit.size();
    d->m_preeditCursorVisible = true;
    for (const QInputMethodEvent::Attribute& a : std::as_const(d->m_preeditAttributes)) {
        if (a.type == QInputMethodEvent::Cursor) {
            d->m_preeditCursor = qBound(0, a.start, int(d->m_preedit.size()));
            d->m_preeditCursorVisible = a.length != 0;
        }
    }
    if (d->m_preedit.isEmpty()) {
        d->m_preeditAttributes.clear();
    } else {
        ensureCursorVisible(d->m_cursor, preeditColumns());
    }

    d->m_caretPainter->resetBlink();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
    e->accept();
}

QVariant CodeEditArea::inputMethodQuery(Qt::InputMethodQuery query) const {
    const QString line = d->m_doc ? d->m_doc->lineAt(d->m_cursor.line) : QString();
    switch (query) {
    case Qt::ImEnabled:
        return !d->m_readOnly;
    case Qt::ImCursorRectangle:
        return caretRect(true).translated(viewport()->pos());
    case Qt::ImFont:
        return font();
    case Qt::ImCursorPosition:
        return d->m_cursor.column;
    case Qt::ImAnchorPosition:
        // Columns refer to the current line; an anchor on another line is
        // reported at the matching end of it.
        if (d->m_anchor.line == d->m_cursor.line) return d->m_anchor.column;
        return d->m_anchor.line < d->m_cursor.line ? 0 : int(line.size());
    case Qt::ImSurroundingText:
        return line;
    case Qt::ImCurrentSelection:
        return (hasSelection() && d->m_anchor.line == d->m_cursor.line) ? selectedText() : QString();
    case Qt::ImTextBeforeCursor:
        return line.left(d->m_cursor.column);
    case Qt::ImTextAfterCursor:
        return line.mid(d->m_cursor.column);
    case Qt::ImHints:
        return int(inputMethodHints() | Qt::ImhMultiLine);
    case Qt::ImInputItemClipRectangle:
        return viewport()->geometry();
    default:
        return QAbstractScrollArea::inputMethodQuery(query);
    }
}

void CodeEditArea::updateInputMethod(Qt::InputMethodQueries queries) {
    if (hasFocus()) {
        QGuiApplication::inputMethod()->update(queries);
    }
}

void CodeEditArea::cancelPreedit(bool resetInputMethod) {
    if (d->m_preedit.isEmpty()) return;
    d->m_preedit.clear();
    d->m_preeditAttributes.clear();
    d->m_preeditCursor = 0;
    d->m_preeditCursorVisible = true;
    if (resetInputMethod && hasFocus()) {
        QGuiApplication::inputMethod()->reset();
    }
    viewport()->update();
}

QRect CodeEditArea::caretRect(bool withPreedit) const {
    const ViewportState& vp = d->m_viewportState;
    const QFontMetrics fm(font());
    const int height = fm.ascent() + fm.descent();
    if (!d->m_doc || !vp.isValid()) {
        return QRect(LineRenderer::kLeftPaddingPx, 0, 1, height);
    }
    const int row = visualRowOf(d->m_cursor);
    const int visualCol = caretCellOf(d->m_cursor);
    int x =LineRenderer::kLeftPaddingPx + visualCol * vp.charWidth - vp.contentOffsetX;
    if (withPreedit && !d->m_preedit.isEmpty()) {
        x += preeditCursorOffsetPx();
    }
    const int y = vp.contentOffsetY + (row - vp.firstVisibleRow) * vp.lineHeight;
    return QRect(x, y, 1, height);
}

int CodeEditArea::preeditCursorOffsetPx() const {
    return QFontMetrics(font()).horizontalAdvance(d->m_preedit.left(d->m_preeditCursor));
}

int CodeEditArea::preeditColumns() const {
    const int cw = d->m_viewportState.charWidth;
    if (cw <= 0 || d->m_preedit.isEmpty()) return 0;
    return (QFontMetrics(font()).horizontalAdvance(d->m_preedit) + cw - 1) / cw;
}

// ------------------------------------------------------------------------
// Document signal handlers
// ------------------------------------------------------------------------

void CodeEditArea::onDocumentReset() {
    clearAllExtraSelections();
    d->m_cursor = d->m_cursorCtrl->clamp(TextCursor{});
    d->m_anchor = d->m_cursor;
    d->m_undoStack->clear();
    d->m_knownLineCount = d->m_doc ? d->m_doc->lineCount() : 0;
    d->m_pendingRehighlightFrom = -1;
    rebuildHighlightCache();
    d->m_foldState.clear();
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit cursorPositionChanged(d->m_cursor);
    emit selectionChanged();
    emit textChanged();
}

void CodeEditArea::onLinesInserted(int startLine, int count) {
    clearAllExtraSelections();
    d->m_cursor = d->m_cursorCtrl->clamp(d->m_cursor);
    d->m_anchor = d->m_cursorCtrl->clamp(d->m_anchor);
    d->m_knownLineCount += count;
    const int rehighlightStart = takePendingRehighlight(startLine);
    if (d->m_highlighter) {
        // Lines inserted inside the highlighted prefix belong to it (they
        // are highlighted by rehighlightFrom below).
        // "<=": lines appended right after the prefix (e.g. at the end of a
        // fully highlighted document) can be highlighted now as well.
        if (startLine <= d->m_validLines) d->m_validLines += count;
        d->m_lineEndStates.insert(startLine, count, HighlightState{});
        d->m_lineSpans.insert(startLine, count, {});
        d->m_lineFolds.insert(startLine, count, {});
        if (d->m_batchDepth == 0) rehighlightFrom(rehighlightStart);
    }
    d->m_foldState.shiftLines(startLine, count);  // keep collapsed regions in place
    if (batched(rehighlightStart)) return;
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit textChanged();
}

void CodeEditArea::onLinesRemoved(int startLine, int count) {
    clearAllExtraSelections();
    d->m_cursor = d->m_cursorCtrl->clamp(d->m_cursor);
    d->m_anchor = d->m_cursorCtrl->clamp(d->m_anchor);
    d->m_knownLineCount -= count;
    const int rehighlightStart = takePendingRehighlight(startLine);
    if (d->m_highlighter) {
        if (startLine < d->m_validLines) d->m_validLines -= qMin(count, d->m_validLines - startLine);
        const int removable = qBound(0, int(d->m_lineEndStates.size()) - startLine, count);
        d->m_lineEndStates.remove(startLine, removable);
        d->m_lineSpans.remove(startLine, qBound(0, int(d->m_lineSpans.size()) - startLine, count));
        d->m_lineFolds.remove(startLine, qBound(0, int(d->m_lineFolds.size()) - startLine, count));
        if (d->m_batchDepth == 0) rehighlightFrom(rehighlightStart);
    }
    d->m_foldState.shiftLines(startLine, -count);
    if (batched(rehighlightStart)) return;
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit textChanged();
}

void CodeEditArea::onLinesChanged(int startLine, int) {
    clearAllExtraSelections();
    // A document may report the changed line before the lines it inserted or
    // removed (SimpleTextDocument does for multi-line inserts). Until that
    // signal arrives the per-line caches and fold regions cannot be lined up
    // with the text: remember the line and let onLinesInserted() /
    // onLinesRemoved() re-highlight from it and rebuild the folds.
    if (d->m_doc && d->m_doc->lineCount() != d->m_knownLineCount) {
        d->m_pendingRehighlightFrom = d->m_pendingRehighlightFrom < 0
            ? startLine : qMin(d->m_pendingRehighlightFrom, startLine);
        return;
    }
    if (batched(startLine)) return;
    if (d->m_highlighter) {
        rehighlightFrom(startLine);
    }
    rebuildFolds();
    rebuildWrapLayout();
    refreshViewportState();
    viewport()->update();
    emit textChanged();
}

namespace {
// Where a position goes when `removed` lines from `first` become `inserted`
// lines: below the run it moves with the line count; in the run it stays on
// the line at the same place, or after a removal goes to the line after it.
TextCursor mapThroughRun(TextCursor pos, int first, int removed, int inserted) {
    if (pos.line < first) return pos;
    if (pos.line >= first + removed) return {pos.line + inserted - removed, pos.column};
    if (inserted == 0) return {first, 0};
    return {first + qMin(pos.line - first, inserted - 1), pos.column};
}
}

void CodeEditArea::beginBatch() {
    ++d->m_batchDepth;
}

bool CodeEditArea::batched(int startLine) {
    if (d->m_batchDepth == 0) return false;
    d->m_batchRehighlightFrom = d->m_batchRehighlightFrom < 0 ? startLine : qMin(d->m_batchRehighlightFrom, startLine);
    return true;
}

// The end of a batch: what each change would have brought up to date, once.
void CodeEditArea::endBatch() {
    if (d->m_batchDepth == 0 || --d->m_batchDepth > 0 || d->m_batchRehighlightFrom < 0) return;
    const int from = takePendingRehighlight(d->m_batchRehighlightFrom);
    d->m_batchRehighlightFrom = -1;
    d->m_cursor = d->m_cursorCtrl->clamp(d->m_cursor);
    d->m_anchor = d->m_cursorCtrl->clamp(d->m_anchor);
    if (d->m_highlighter) rehighlightFrom(from);
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit textChanged();
}

void CodeEditArea::onTextReplacementStarted() {
    beginBatch();
    d->m_replacement = std::make_unique<Replacement>();
    d->m_replacement->cursorBefore = d->m_cursor;
    d->m_replacement->anchorBefore = d->m_anchor;
    d->m_replacement->cursor = d->m_cursor;
    d->m_replacement->anchor = d->m_anchor;
    d->m_replacement->topLine = d->m_viewportState.firstVisibleLine;
}

void CodeEditArea::onLinesReplaced(int first, const QStringList& removed, const QStringList& inserted) {
    if (!d->m_replacement) return;
    Replacement& r = *d->m_replacement;
    r.cursor = mapThroughRun(r.cursor, first, removed.size(), inserted.size());
    r.anchor = mapThroughRun(r.anchor, first, removed.size(), inserted.size());
    r.topLine = mapThroughRun({r.topLine, 0}, first, removed.size(), inserted.size()).line;
    r.runs.append({first, removed, inserted});
}

// The document has changed already; the command only records it for undo.
void CodeEditArea::onTextReplacementFinished() {
    endBatch();
    if (!d->m_replacement) return;
    const std::unique_ptr<Replacement> r = std::move(d->m_replacement);
    cancelPreedit(true);
    d->m_cursor = d->m_cursorCtrl->clamp(r->cursor);
    d->m_anchor = d->m_cursorCtrl->clamp(r->anchor);
    d->m_undoStack->push(new ReplaceLinesCommand(d->m_doc, r->runs, r->cursorBefore, r->anchorBefore,
                                              d->m_cursor, d->m_anchor, &d->m_cursor, &d->m_anchor,
                                              [this]() { beginBatch(); }, [this]() { endBatch(); }));
    // The same text stays at the top of the view.
    const int topLine = qBound(0, r->topLine, qMax(0, d->m_doc->lineCount() - 1));
    verticalScrollBar()->setValue(hasRows() ? d->m_wrapLayout->firstRowOf(topLine) : topLine);
    d->m_caretPainter->resetBlink();
    ensureCursorVisible(d->m_cursor);
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(d->m_cursor);
    emit selectionChanged();
}

int CodeEditArea::takePendingRehighlight(int startLine) {
    const int from = d->m_pendingRehighlightFrom < 0
        ? startLine : qMin(d->m_pendingRehighlightFrom, startLine);
    d->m_pendingRehighlightFrom = -1;
    return from;
}

// ------------------------------------------------------------------------
// Private — navigation helpers
// ------------------------------------------------------------------------

void CodeEditArea::refreshViewportState() {
    const QFontMetrics fm(font());
    const int lineHeight = LineRenderer::lineHeightFor(font());
    const int charWidth  = fm.horizontalAdvance(QLatin1Char('M'));
    const int vpW = viewport()->width();
    const int vpH = viewport()->height();

    ViewportState s;
    s.lineHeight     = lineHeight;
    s.charWidth      = charWidth;
    s.viewportWidth  = vpW;
    s.viewportHeight = vpH;
    s.contentOffsetY = 0;
    s.wordWrap       = d->m_wordWrap;

    // Rows with and without word wrap (without it a row per visible line);
    // lines only before the layout is built
    if (hasRows()) {
        s.contentOffsetX = (!d->m_wordWrap && charWidth > 0)
            ? horizontalScrollBar()->value() * charWidth : 0;
        const int totalRows = d->m_wrapLayout->totalRows();
        if (lineHeight > 0 && totalRows > 0) {
            const int firstRow = verticalScrollBar()->value();
            s.firstVisibleRow = qMin(firstRow, totalRows - 1);
            const int maxVisible = (vpH + lineHeight - 1) / lineHeight;
            s.lastVisibleRow = qMin(s.firstVisibleRow + maxVisible - 1, totalRows - 1);

            int prevLogical = -1;
            for (int r = s.firstVisibleRow; r <= s.lastVisibleRow; ++r) {
                const WrapLayout::Row& wr = d->m_wrapLayout->rowAt(r);
                ViewportState::RowInfo ri;
                ri.logicalLine = wr.logicalLine;
                ri.startCol    = wr.startCol;
                ri.endCol      = wr.endCol;
                ri.isFirstRow  = (wr.logicalLine != prevLogical);
                if (ri.isFirstRow) {
                    const int regIdx = d->m_foldState.regionStartingAt(wr.logicalLine);
                    if (regIdx >= 0 && d->m_foldState.isCollapsed(regIdx)) {
                        const FoldRegion& fr = d->m_foldState.regions()[regIdx];
                        ri.foldPlaceholder  = fr.placeholder;
                        ri.foldStartColumn  = fr.startColumn;
                    }
                }
                s.rows.push_back(ri);
                prevLogical = wr.logicalLine;
            }
            s.firstVisibleLine = s.rows.isEmpty() ? 0 : s.rows.first().logicalLine;
            s.lastVisibleLine  = s.rows.isEmpty() ? -1 : s.rows.last().logicalLine;
        } else {
            s.firstVisibleRow = 0;
            s.lastVisibleRow  = -1;
            s.firstVisibleLine = 0;
            s.lastVisibleLine  = -1;
        }
    } else {
        s.contentOffsetX = (charWidth > 0)
            ? horizontalScrollBar()->value() * charWidth : 0;
        const int lineCount = d->m_doc ? d->m_doc->lineCount() : 0;
        if (lineHeight > 0 && lineCount > 0) {
            const int scrollY = verticalScrollBar()->value();
            s.firstVisibleLine = qMin(scrollY, lineCount - 1);
            const int maxVisible = (vpH + lineHeight - 1) / lineHeight;
            s.lastVisibleLine = qMin(s.firstVisibleLine + maxVisible - 1, lineCount - 1);
        } else {
            s.firstVisibleLine = 0;
            s.lastVisibleLine  = -1;
        }
        s.firstVisibleRow = s.firstVisibleLine;
        s.lastVisibleRow  = s.lastVisibleLine;
    }

    d->m_viewportState = s;
    emit viewportChanged(d->m_viewportState);
}

void CodeEditArea::updateScrollBarRanges() {
    const QFontMetrics fm(font());
    const int lineHeight = LineRenderer::lineHeightFor(font());
    const int charWidth  = fm.horizontalAdvance(QLatin1Char('M'));
    const int vpH = viewport()->height();
    const int vpW = viewport()->width();
    const int visibleLines = (lineHeight > 0) ? (vpH / lineHeight) : 0;

    // Vertically rows (folded lines left out), lines before the layout is built
    const int rowCount = hasRows() ? d->m_wrapLayout->totalRows() : (d->m_doc ? d->m_doc->lineCount() : 0);
    verticalScrollBar()->setRange(0, qMax(0, rowCount - visibleLines));
    verticalScrollBar()->setPageStep(qMax(1, visibleLines));
    verticalScrollBar()->setSingleStep(1);
    if (d->m_wordWrap) {
        horizontalScrollBar()->setRange(0, 0);
    } else {

        const int visibleCols = (charWidth > 0) ? (vpW / charWidth) : 0;
        // Characters of the longest line; a line of tabs or wide characters
        // takes more cells, so at least up to the caret.
        const int maxCols = d->m_doc ? qMax(d->m_doc->maxLineLength(), caretCellOf(d->m_cursor) + 1) : 0;
        const int hMax = qMax(0, maxCols - visibleCols);
        horizontalScrollBar()->setRange(0, hMax);
        horizontalScrollBar()->setPageStep(qMax(1, visibleCols));
        horizontalScrollBar()->setSingleStep(1);
    }
}

void CodeEditArea::rebindDocumentSignals(ITextDocument* newDoc) {
    if (d->m_doc) {
        disconnect(d->m_doc, nullptr, this, nullptr);
    }
    if (newDoc) {
        connect(newDoc, &ITextDocument::documentReset,
                this, &CodeEditArea::onDocumentReset);
        connect(newDoc, &ITextDocument::linesInserted,
                this, &CodeEditArea::onLinesInserted);
        connect(newDoc, &ITextDocument::linesRemoved,
                this, &CodeEditArea::onLinesRemoved);
        connect(newDoc, &ITextDocument::linesChanged,
                this, &CodeEditArea::onLinesChanged);
        connect(newDoc, &ITextDocument::textReplacementStarted,
                this, &CodeEditArea::onTextReplacementStarted);
        connect(newDoc, &ITextDocument::linesReplaced,
                this, &CodeEditArea::onLinesReplaced);
        connect(newDoc, &ITextDocument::textReplacementFinished,
                this, &CodeEditArea::onTextReplacementFinished);
    }
}

void CodeEditArea::applyCursorMove(TextCursor newPos) {
    const TextCursor clamped = d->m_cursorCtrl->clamp(newPos);
    const bool posChanged = (clamped != d->m_cursor);
    const bool selWas = hasSelection();

    d->m_cursor = clamped;
    d->m_anchor = clamped;

    if (!posChanged && !selWas) {
        return;
    }
    d->m_caretPainter->resetBlink();
    ensureCursorVisible(d->m_cursor);
    viewport()->update();
    updateInputMethod();
    if (posChanged) emit cursorPositionChanged(d->m_cursor);
    if (selWas)     emit selectionChanged();
}

void CodeEditArea::applySelectionMove(TextCursor newPos) {
    const TextCursor clamped = d->m_cursorCtrl->clamp(newPos);
    if (clamped == d->m_cursor) {
        return;
    }
    d->m_cursor = clamped;
    d->m_caretPainter->resetBlink();
    ensureCursorVisible(d->m_cursor);
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(d->m_cursor);
    emit selectionChanged();
}

TextCursor CodeEditArea::cursorFromPoint(const QPoint& pt) const {
    const ViewportState& vp = d->m_viewportState;
    if (!d->m_doc || !vp.isValid()) return {};

    const int tw = tabWidth();

    if (!vp.rows.isEmpty()) {
        const int ri = qBound(0, pt.y() / vp.lineHeight, vp.rows.size() - 1);
        const ViewportState::RowInfo& row = vp.rows[ri];
        const int targetX = pt.x() - LineRenderer::kLeftPaddingPx + vp.contentOffsetX;
        const int col = LineRenderer::indexAtX(d->m_doc->lineAt(row.logicalLine), row.startCol, row.endCol,
                                               targetX, vp.charWidth, tw);
        return d->m_cursorCtrl->clamp({row.logicalLine, col});
    }

    const int lineNum = qBound(0,
        vp.firstVisibleLine + pt.y() / vp.lineHeight,
        d->m_doc->lineCount() - 1);
    const int targetX = pt.x() - LineRenderer::kLeftPaddingPx + vp.contentOffsetX;
    const QString& lineStr = d->m_doc->lineAt(lineNum);
    const int col = LineRenderer::indexAtX(lineStr, 0, int(lineStr.size()), targetX, vp.charWidth, tw);
    return d->m_cursorCtrl->clamp({lineNum, col});
}

void CodeEditArea::ensureVisible(TextCursor pos, int marginRows) {
    ensureCursorVisible(d->m_cursorCtrl->clamp(pos), 0, marginRows);
}

void CodeEditArea::ensureCursorVisible(TextCursor pos, int extraColumns, int marginRows) {
    QScrollBar* vBar = verticalScrollBar();

    // Only rows shown whole count: the last visible row of the viewport state
    // includes one cut off at the bottom (it is painted), and a cursor there
    // would stay half hidden, e.g. after Enter on the last row.
    const int page   = pageLineCount();
    // A margin of at most a third of the page, so that the row still fits
    const int margin = qBound(0, marginRows, (page - 1) / 3);
    auto scrollTo = [&](int row, int first) {
        const int last = first + page - 1;
        if (row < first) {
            vBar->setValue(qMax(0, row - margin));
        } else if (row > last) {
            vBar->setValue(qMax(0, row - page + 1 + margin));
        }
    };
    if (hasRows())
        scrollTo(visualRowOf(pos), d->m_viewportState.firstVisibleRow);
    else
        scrollTo(pos.line, d->m_viewportState.firstVisibleLine);
    if (d->m_wordWrap)
        return; // no horizontal scroll in wrap mode

    const int charWidth = d->m_viewportState.charWidth;
    if (charWidth <= 0) return;
    QScrollBar* hBar = horizontalScrollBar();
    const int firstCol    = hBar->value();
    const int visibleCols = d->m_viewportState.viewportWidth / charWidth;
    const int lastCol     = firstCol + visibleCols - 1;
    // The caret's cell on the screen: with tabs, wide characters or
    // right-to-left text not its column.
    const int caretCol    = caretCellOf(pos);
    const int rightCol    = caretCol + extraColumns;
    if (rightCol - visibleCols + 1 > hBar->maximum()) hBar->setMaximum(rightCol - visibleCols + 1);
    if (caretCol < firstCol) {
        hBar->setValue(caretCol);
    } else if (rightCol > lastCol) {
        // Keep the left end visible if the right end does not fit.
        hBar->setValue(qMin(caretCol, qMax(0, rightCol - visibleCols + 1)));
    }
}

int CodeEditArea::pageLineCount() const {
    const int lh = d->m_viewportState.lineHeight;
    return (lh <= 0) ? 1 : qMax(1, d->m_viewportState.viewportHeight / lh);
}

// --- Word-wrap helpers ---------------------------------------------------

void CodeEditArea::rebuildWrapLayout() {
    if (!d->m_doc) return;
    const QFontMetrics fm(font());
    const int cw = fm.horizontalAdvance(QLatin1Char('M'));
    if (cw <= 0) return;
    // Without word wrap a row per line: folded lines are skipped as with it
    const int availCols = d->m_wordWrap
        ? qMax(1, (viewport()->width() - LineRenderer::kLeftPaddingPx) / cw)
        : WrapLayout::kNoWrap;
    const FoldState* fs = (d->m_foldState.regions().isEmpty()) ? nullptr : &d->m_foldState;
    d->m_wrapLayout->rebuild(d->m_doc, availCols, tabWidth(), fs);
}

bool CodeEditArea::hasRows() const {
    return d->m_doc && d->m_wrapLayout->totalRows() > 0;
}

int CodeEditArea::visualRowOf(TextCursor pos) const {
    if (!hasRows()) return pos.line;
    return d->m_wrapLayout->rowForCursor(pos.line, pos.column);
}

int CodeEditArea::caretCellOf(TextCursor pos) const {
    if (!d->m_doc) return pos.column;
    const QString& line = d->m_doc->lineAt(pos.line);
    int rowStart = 0, rowEnd = int(line.size());
    if (hasRows()) {
        const WrapLayout::Row& row = d->m_wrapLayout->rowAt(visualRowOf(pos));
        if (row.logicalLine == pos.line) {  // not a line hidden in a fold
            rowStart = row.startCol;
            rowEnd = row.endCol;
        }
    }
    return LineRenderer::caretCell(line, rowStart, rowEnd, pos.column, tabWidth());
}

TextCursor CodeEditArea::moveVisualRows(TextCursor c, int delta) const {
    if (!hasRows()) {
        return delta < 0 ? d->m_cursorCtrl->moveUp(c, -delta) : d->m_cursorCtrl->moveDown(c, delta);
    }
    const int row = visualRowOf(c);
    const int target = qBound(0, row + delta, d->m_wrapLayout->totalRows() - 1);
    if (target == row) return c;  // first or last row: as moveUp() on line 0
    const int tw = tabWidth();
    // The same character cell in the target row, or its end.
    const int x = caretCellOf(c);
    const WrapLayout::Row& to = d->m_wrapLayout->rowAt(target);
    const QString& line = d->m_doc->lineAt(to.logicalLine);
    int col = LineRenderer::indexAtX(line, to.startCol, to.endCol, x, 1, tw);
    // The end of a row the line continues after is the start of the next
    // row; stay on the last character of this one.
    if (col >= to.endCol && to.endCol < line.size() && to.endCol > to.startCol) {
        col = codepoints::prevIndex(line, to.endCol);
    }
    return d->m_cursorCtrl->clamp({to.logicalLine, col});
}

// Home of a wrapped line: the start of the screen row; at the start of a row
// that is not the line's first, the start of the line
TextCursor CodeEditArea::moveToRowStart(TextCursor c) const {
    if (!d->m_wordWrap || !d->m_doc || d->m_wrapLayout->totalRows() == 0)
        return d->m_cursorCtrl->moveToLineStart(c);
    const WrapLayout::Row& row = d->m_wrapLayout->rowAt(visualRowOf(c));
    if (row.logicalLine != c.line || c.column == row.startCol)
        return d->m_cursorCtrl->moveToLineStart(c);
    return {c.line, row.startCol};
}

// End of a wrapped line: the end of the screen row (its last character: the
// position after it is the start of the next row); there already, or on the
// line's last row, the end of the line
TextCursor CodeEditArea::moveToRowEnd(TextCursor c) const {
    if (!d->m_wordWrap || !d->m_doc || d->m_wrapLayout->totalRows() == 0)
        return d->m_cursorCtrl->moveToLineEnd(c);
    const WrapLayout::Row& row = d->m_wrapLayout->rowAt(visualRowOf(c));
    const QString line = d->m_doc->lineAt(c.line);
    if (row.logicalLine != c.line || row.endCol >= line.size() || row.endCol <= row.startCol)
        return d->m_cursorCtrl->moveToLineEnd(c);
    const TextCursor rowEnd{c.line, codepoints::prevIndex(line, row.endCol)};
    return c == rowEnd ? d->m_cursorCtrl->moveToLineEnd(c) : rowEnd;
}

// --- Highlighting helpers ------------------------------------------------

void CodeEditArea::rebuildHighlightCache() {
    d->m_highlightTimer->stop();
    d->m_validLines = 0;
    d->m_lineEndStates.clear();
    d->m_lineSpans.clear();
    d->m_lineFolds.clear();
    if (!d->m_highlighter || !d->m_doc) return;
    const int n = d->m_doc->lineCount();
    d->m_lineEndStates.resize(n);
    d->m_lineSpans.resize(n);
    d->m_lineFolds.resize(n);
    // Highlight nothing yet: a new document, highlighter and palette often
    // arrive together, and each used to re-highlight the whole document.
    // paintEvent() highlights before anything is shown, the timer the rest,
    // a slice at a time, without blocking the event loop.
    d->m_highlightTimer->start();
}

int CodeEditArea::highlightedLineCount() const {
    finishShortHighlight();
    return d->m_highlighter ? d->m_validLines : (d->m_doc ? d->m_doc->lineCount() : 0);
}

bool CodeEditArea::highlightComplete() const {
    return !d->m_highlighter || !d->m_doc || d->m_validLines >= d->m_doc->lineCount();
}

QList<QPair<int, int>> CodeEditArea::spellCheckRanges(int line) {
    QList<QPair<int, int>> ranges;
    if (!d->m_doc || line < 0 || line >= d->m_doc->lineCount()) return ranges;
    const int length = int(d->m_doc->lineAt(line).size());
    if (!d->m_highlighter) {
        if (length > 0) ranges.append({0, length});
        return ranges;
    }
    if (line >= d->m_validLines) highlightUpTo(line + 1);
    if (line >= d->m_validLines || line >= d->m_lineSpans.size()) return ranges;
    // Text outside any span, or in a span without an attribute, is code.
    const QVector<TextAttribute>& attributes = d->m_highlighter->attributes();
    for (const StyleSpan& span : d->m_lineSpans[line]) {
        if (span.length <= 0 || span.attributeId < 0 || span.attributeId >= attributes.size()
                || !attributes[span.attributeId].spellCheck)
            continue;
        const int start = qBound(0, span.start, length);
        const int end = qBound(start, span.start + span.length, length);
        if (start == end) continue;
        if (!ranges.isEmpty() && ranges.last().second == start) ranges.last().second = end;
        else ranges.append({start, end});
    }
    return ranges;
}

void CodeEditArea::highlightUpTo(int lineCount) {
    if (!d->m_highlighter || !d->m_doc) return;
    const int end = qMin(lineCount, int(d->m_lineEndStates.size()));
    HighlightState stateIn = (d->m_validLines == 0)
        ? d->m_highlighter->initialState()
        : d->m_lineEndStates[d->m_validLines - 1];
    for (int i = d->m_validLines; i < end; ++i) {
        HighlightState stateOut;
        d->m_highlighter->highlightLineWithFolds(d->m_doc->lineAt(i), stateIn,
                                              d->m_lineSpans[i], stateOut, d->m_lineFolds[i]);
        d->m_lineEndStates[i] = stateOut;
        stateIn = stateOut;
    }
    d->m_validLines = qMax(d->m_validLines, end);
}

void CodeEditArea::highlightChunk() {
    if (!d->m_highlighter || !d->m_doc) {
        d->m_highlightTimer->stop();
        return;
    }
    // While the document reports a change in two signals the caches do not
    // match it yet; continue on the next tick.
    if (d->m_lineEndStates.size() != d->m_doc->lineCount()) return;

    QElapsedTimer budget;
    budget.start();
    while (!highlightComplete() && budget.elapsed() < 8) {
        highlightUpTo(d->m_validLines + 512);
    }
    if (!highlightComplete()) return;
    finishHighlighting();
}

void CodeEditArea::finishShortHighlight() const {
    if (!d->m_highlighter || !d->m_doc || d->m_doc->lineCount() > kSyncHighlightLines
            || d->m_lineEndStates.size() != d->m_doc->lineCount()) return;
    // Painting may have highlighted it all while folds still wait for the timer.
    if (highlightComplete() && !d->m_foldsPending) return;
    // Queries see a short document highlighted at once, as they always did;
    // only the work between them is deferred.
    auto* self = const_cast<CodeEditArea*>(this);
    self->highlightUpTo(d->m_doc->lineCount());
    self->finishHighlighting();
}

void CodeEditArea::finishHighlighting() {
    d->m_highlightTimer->stop();
    if (d->m_foldsPending) {
        rebuildFolds();
        rebuildWrapLayout();
        updateScrollBarRanges();
        refreshViewportState();
    }
    viewport()->update();
    emit highlightingCompleted();
}

void CodeEditArea::rehighlightFrom(int startLine) {
    if (!d->m_highlighter || !d->m_doc) return;
    const int n = d->m_doc->lineCount();
    if (startLine >= n) return;
    // Keep cache vectors in sync with line count.
    if (d->m_lineEndStates.size() != n) {
        d->m_lineEndStates.resize(n);
        d->m_lineSpans.resize(n);
    }
    if (d->m_lineFolds.size() != n) {
        d->m_lineFolds.resize(n);
    }
    // Lines past the highlighted prefix are done by highlightUpTo() later.
    d->m_validLines = qMin(d->m_validLines, n);
    if (startLine >= d->m_validLines) return;

    HighlightState stateIn = (startLine == 0)
        ? d->m_highlighter->initialState()
        : d->m_lineEndStates[startLine - 1];

    for (int i = startLine; i < d->m_validLines; ++i) {
        const HighlightState oldEndState = d->m_lineEndStates[i];
        HighlightState stateOut;
        d->m_highlighter->highlightLineWithFolds(d->m_doc->lineAt(i), stateIn,
                                              d->m_lineSpans[i], stateOut, d->m_lineFolds[i]);
        d->m_lineEndStates[i] = stateOut;
        // After the first mandatory re-highlight (startLine), stop as soon as
        // the end-of-line state matches what was cached — downstream lines
        // are still correct.
        if (i > startLine && stateOut == oldEndState) {
            break;
        }
        stateIn = stateOut;
    }
}

// --- Folding helpers -----------------------------------------------------

void CodeEditArea::rebuildFolds() {
    if (!d->m_foldingProvider || !d->m_doc) {
        d->m_foldsPending = false;
        d->m_foldState.setRegions({});
        return;
    }
    // Until the whole document is highlighted the markers are incomplete;
    // regions built from them would drop (and un-collapse) regions that end
    // further down. Keep the current regions and rebuild when it completes.
    if (!highlightComplete()) {
        d->m_foldsPending = true;
        if (!d->m_highlightTimer->isActive()) d->m_highlightTimer->start();
        return;
    }
    d->m_foldsPending = false;
    // Fast path: regions from the markers collected while highlighting.
    QVector<FoldRegion> regions;
    if (d->m_highlighter && d->m_lineFolds.size() == d->m_doc->lineCount()
            && d->m_foldingProvider->regionsFromLineMarkers(d->m_highlighter, d->m_lineFolds, regions)) {
        d->m_foldState.setRegions(std::move(regions));
        return;
    }
    d->m_foldState.setRegions(d->m_foldingProvider->computeRegions(d->m_doc));
}

// ------------------------------------------------------------------------
// Private — edit helpers
// ------------------------------------------------------------------------

void CodeEditArea::showReadOnlyHint() {
    int vRow = visualRowOf(d->m_cursor);
    int vCol = d->m_cursor.column;
    if (d->m_doc && d->m_viewportState.isValid()) vCol = caretCellOf(d->m_cursor);
    const int px = d->m_viewportState.isValid()
        ? LineRenderer::kLeftPaddingPx + vCol * d->m_viewportState.charWidth
              - d->m_viewportState.contentOffsetX
        : width() / 2;
    const int py = d->m_viewportState.isValid()
        ? d->m_viewportState.contentOffsetY
              + (vRow - d->m_viewportState.firstVisibleRow) * d->m_viewportState.lineHeight
        : height() / 3;
    QToolTip::showText(viewport()->mapToGlobal(QPoint(px, py)),
                       tr("This view is read-only"), viewport());
}

void CodeEditArea::executeInsert(const QString& text) {
    if (!d->m_doc || d->m_readOnly) {
        if (d->m_readOnly) showReadOnlyHint();
        return;
    }
    if (hasSelection()) {
        d->m_undoStack->beginMacro(QString());
        executeRemoveSelection();
        d->m_undoStack->push(new InsertCommand(
            d->m_doc, d->m_cursor, text, d->m_cursor, &d->m_cursor, &d->m_anchor));
        d->m_undoStack->endMacro();
    } else {
        d->m_undoStack->push(new InsertCommand(
            d->m_doc, d->m_cursor, text, d->m_cursor, &d->m_cursor, &d->m_anchor));
    }
    updateAfterEdit();
}

void CodeEditArea::executeRemove(TextCursor start, TextCursor end) {
    if (!d->m_doc || d->m_readOnly || start == end) {
        if (d->m_readOnly) showReadOnlyHint();
        return;
    }
    d->m_undoStack->push(new RemoveCommand(
        d->m_doc, start, end, d->m_cursor, &d->m_cursor, &d->m_anchor));
    updateAfterEdit();
}

void CodeEditArea::executeRemoveSelection() {
    if (!d->m_doc || d->m_readOnly || !hasSelection()) {
        if (d->m_readOnly) showReadOnlyHint();
        return;
    }
    d->m_undoStack->push(new RemoveCommand(
        d->m_doc, selectionStart(), selectionEnd(),
        d->m_cursor, &d->m_cursor, &d->m_anchor));
    updateAfterEdit();
}

void CodeEditArea::updateAfterEdit() {
    d->m_cursor = d->m_cursorCtrl->clamp(d->m_cursor);
    d->m_anchor = d->m_cursor;
    // Collapsed regions survive edits; never leave the cursor on a hidden line
    // (e.g. Enter at the end of a collapsed header).
    if (d->m_foldState.expandContaining(d->m_cursor.line)) {
        rebuildWrapLayout();
        refreshViewportState();
    }
    d->m_caretPainter->resetBlink();
    ensureCursorVisible(d->m_cursor);
    updateScrollBarRanges();
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(d->m_cursor);
    emit selectionChanged();
}

void CodeEditArea::insertTypedText(const QString& typed) {
    QString text = typed;
    if (d->m_insertFilter && !d->m_readOnly && (!d->m_insertFilter(text) || text.isEmpty())) return;
    if (d->m_overwrite && !hasSelection()
            && d->m_doc
            && d->m_cursor.column < d->m_doc->lineAt(d->m_cursor.line).size()) {
        // Replace character under cursor in one undo step.
        d->m_undoStack->beginMacro(QString());
        executeRemove(d->m_cursor, {d->m_cursor.line, d->m_cursor.column + 1});
        executeInsert(text);
        d->m_undoStack->endMacro();
    } else {
        executeInsert(text);
    }
}

bool CodeEditArea::isInsertableText(const QString& text) {
    if (text.isEmpty()) return false;
    // Iterate code points so surrogate pairs (emoji) count as one character.
    for (const char32_t c : text.toUcs4()) {
        if (QChar::isPrint(c)) continue;
        switch (QChar::category(c)) {
        case QChar::Mark_NonSpacing:
        case QChar::Mark_SpacingCombining:
        case QChar::Mark_Enclosing:
            continue;
        default:
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------------
// Private — painting helpers
// ------------------------------------------------------------------------

QRegion CodeEditArea::selectionRegion() const {
    if (!hasSelection() || !d->m_doc || !d->m_viewportState.isValid()) return {};
    const TextCursor s = selectionStart();
    const TextCursor e = selectionEnd();
    const ViewportState& vp = d->m_viewportState;
    const int tw = tabWidth();
    const auto band = LineRenderer::backgroundBand(font(), vp.lineHeight);
    QRegion region;
    // The selected characters of the row line[rowStart, rowEnd) (several
    // pieces when right-to-left text reverses part of it); when the
    // selection goes on past the line, also from the row's end to the cell
    // `wideEnd`.
    const auto addRow = [&](const QString& line, int lineNumber, int rowStart, int rowEnd, int topY, int wideEnd) {
        const int from = lineNumber == s.line ? s.column : 0;
        const int to = lineNumber == e.line ? e.column : int(line.size());
        const auto addCells = [&](int start, int end) {
            const int x = LineRenderer::kLeftPaddingPx + start * vp.charWidth - vp.contentOffsetX;
            const int w = (end - start) * vp.charWidth;
            if (w > 0) region += QRect(x, topY + band.offset, w, band.height);
        };
        for (const auto& [start, end] : LineRenderer::cellRanges(line, rowStart, rowEnd, from, to, tw))
            addCells(start, end);
        if (lineNumber != e.line) {
            const int rowCells = LineRenderer::visualColumn(line.mid(rowStart, rowEnd - rowStart), rowEnd - rowStart, tw);
            addCells(rowCells, qMax(rowCells, wideEnd));
        }
    };

    if (!vp.rows.isEmpty()) {
        for (int ri = 0; ri < vp.rows.size(); ++ri) {
            const ViewportState::RowInfo& row = vp.rows[ri];
            // Skip rows outside selection's logical line range.
            if (row.logicalLine < s.line || row.logicalLine > e.line) continue;
            if (row.logicalLine == s.line && row.endCol <= s.column)   continue;
            if (row.logicalLine == e.line && row.startCol >= e.column) continue;

            const int topY = vp.contentOffsetY + ri * vp.lineHeight;
            addRow(d->m_doc->lineAt(row.logicalLine), row.logicalLine, row.startCol, row.endCol, topY,
                   (vp.viewportWidth + vp.contentOffsetX) / vp.charWidth + 1);
        }
        return region;
    }

    const int first = qMax(s.line, vp.firstVisibleLine);
    const int last  = qMin(e.line, vp.lastVisibleLine);
    for (int i = first; i <= last; ++i) {
        const int topY = vp.contentOffsetY + (i - vp.firstVisibleLine) * vp.lineHeight;
        const QString& lineStr = d->m_doc->lineAt(i);
        addRow(lineStr, i, 0, int(lineStr.size()), topY, vp.viewportWidth / vp.charWidth + 1);
    }
    return region;
}

void CodeEditArea::paintLineBackgrounds(QPainter& painter) {
    if ((!d->m_lineBgProvider && !d->m_currentLineColor.isValid())
            || !d->m_doc || !d->m_viewportState.isValid()) return;
    const ViewportState& vp = d->m_viewportState;
    const int vpW = vp.viewportWidth;
    const auto band = LineRenderer::backgroundBand(font(), vp.lineHeight);
    auto fill = [&](int line, int topY) {
        const QColor bg = lineBackground(line);
        if (bg.isValid()) {
            painter.fillRect(0, topY + band.offset, vpW, band.height, bg);
        }
    };
    if (!vp.rows.isEmpty()) {
        for (int i = 0; i < vp.rows.size(); ++i) {
            fill(vp.rows[i].logicalLine,
                 vp.contentOffsetY + i * vp.lineHeight);
        }
    } else {
        for (int i = vp.firstVisibleLine; i <= vp.lastVisibleLine; ++i) {
            fill(i, vp.contentOffsetY + (i - vp.firstVisibleLine) * vp.lineHeight);
        }
    }
}

void CodeEditArea::paintPreedit(QPainter& painter) {
    if (d->m_preedit.isEmpty() || !d->m_doc || !d->m_viewportState.isValid()) return;
    const ViewportState& vp = d->m_viewportState;
    const int row = visualRowOf(d->m_cursor);
    if (row < vp.firstVisibleRow || row > vp.lastVisibleRow) return;

    const int x = caretRect(false).x();
    const int topY = vp.contentOffsetY + (row - vp.firstVisibleRow) * vp.lineHeight;
    const QFontMetrics fm(font());
    const int width = fm.horizontalAdvance(d->m_preedit);

    // Cover the rest of the row and repaint it shifted right of the pre-edit
    // (display only; the document is unchanged).
    painter.fillRect(QRect(x, topY, vp.viewportWidth - x, vp.lineHeight), palette().base());
    const QColor bg = lineBackground(d->m_cursor.line);
    if (bg.isValid()) {
        const auto band = LineRenderer::backgroundBand(font(), vp.lineHeight);
        painter.fillRect(x, topY + band.offset, vp.viewportWidth - x, band.height, bg);
    }
    const QString& line = d->m_doc->lineAt(d->m_cursor.line);
    const int rowEnd = hasRows() ? d->m_wrapLayout->rowAt(row).endCol : int(line.size());
    const QVector<StyleSpan>* spans =
        (d->m_highlighter && d->m_cursor.line < d->m_validLines && d->m_cursor.line < d->m_lineSpans.size())
            ? &d->m_lineSpans[d->m_cursor.line]
                                                              : nullptr;
    painter.setPen(palette().text().color());
    d->m_renderer->paintSegment(painter, line, d->m_cursor.column, rowEnd, x + width,
                             topY, vp.lineHeight, vp.charWidth, spans);

    // Backgrounds requested by the input method (e.g. the active clause).
    for (const QInputMethodEvent::Attribute& a : std::as_const(d->m_preeditAttributes)) {
        if (a.type != QInputMethodEvent::TextFormat) continue;
        const QTextCharFormat f = qvariant_cast<QTextFormat>(a.value).toCharFormat();
        if (f.background().style() == Qt::NoBrush) continue;
        const int ax = x + fm.horizontalAdvance(d->m_preedit.left(a.start));
        const int aw = fm.horizontalAdvance(d->m_preedit.mid(a.start, a.length));
        painter.fillRect(ax, topY, aw, vp.lineHeight, f.background());
    }

    painter.setFont(font());
    const int baselineY = topY + LineRenderer::backgroundBand(font(), vp.lineHeight).baseline;
    painter.drawText(x, baselineY, d->m_preedit);
    // Underline just below the baseline, but inside the (possibly tight) row.
    const int underlineY = qMin(baselineY + 2, topY + vp.lineHeight - 1);
    painter.drawLine(x, underlineY, x + width - 1, underlineY);
}

void CodeEditArea::paintSelection(QPainter& painter) {
    const QRegion region = selectionRegion();
    if (region.isEmpty()) {
        return;
    }
    for (const QRect& r : region) {
        painter.fillRect(r, d->m_selectionColor);
    }
}

} // namespace qce
