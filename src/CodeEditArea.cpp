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

CodeEditArea::CodeEditArea(QWidget* parent)
    : QAbstractScrollArea(parent),
      m_renderer(std::make_unique<LineRenderer>()),
      m_wrapLayout(std::make_unique<WrapLayout>()),
      m_cursorCtrl(std::make_unique<CursorController>(nullptr)),
      m_caretPainter(std::make_unique<CaretPainter>(this)),
      m_undoStack(new QUndoStack(this)) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QStringList families = QFontDatabase::families();
    f.setStyleHint(QFont::TypeWriter);
    f = f.resolve(font());
    f.setPointSizeF(10);
    setFont(fitToCharGrid(f));
    m_renderer->setFont(font());

    viewport()->setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    // The area (not the viewport) has the focus, so it receives
    // QInputMethodEvent and answers inputMethodQuery().
    setAttribute(Qt::WA_InputMethodEnabled, true);

    connect(m_caretPainter.get(), &CaretPainter::blinkToggled,
            viewport(), QOverload<>::of(&QWidget::update));

    m_highlightTimer = new QTimer(this);
    m_highlightTimer->setInterval(0);
    connect(m_highlightTimer, &QTimer::timeout, this, &CodeEditArea::highlightChunk);

    m_renderer->setDecorationsProvider([this](int line) -> const QVector<ExtraSelection>* {
        auto it = m_extraSelectionsByLine.constFind(line);
        return it == m_extraSelectionsByLine.cend() ? nullptr : &it.value();
    });
    refreshViewportState();
}

CodeEditArea::~CodeEditArea() {
    emit aboutToBeDestroyed();
}

// ------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------

void CodeEditArea::setDocument(ITextDocument* doc) {
    if (m_doc == doc) {
        return;
    }
    cancelPreedit(true);
    clearAllExtraSelections();
    rebindDocumentSignals(doc);
    m_doc = doc;
    m_cursorCtrl->setDocument(doc);
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursor;
    m_undoStack->clear();

    m_knownLineCount = doc ? doc->lineCount() : 0;
    m_pendingRehighlightFrom = -1;
    rebuildHighlightCache();
    m_foldState.clear();
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
    return (m_anchor <= m_cursor) ? m_anchor : m_cursor;
}

TextCursor CodeEditArea::selectionEnd() const {
    return (m_anchor <= m_cursor) ? m_cursor : m_anchor;
}

QString CodeEditArea::selectedText() const {
    if (!hasSelection() || !m_doc) {
        return {};
    }
    const TextCursor s = selectionStart();
    const TextCursor e = selectionEnd();

    if (s.line == e.line) {
        return m_doc->lineAt(s.line).mid(s.column, e.column - s.column);
    }

    QString result = m_doc->lineAt(s.line).mid(s.column);
    for (int i = s.line + 1; i < e.line; ++i) {
        result += QLatin1Char('\n') + m_doc->lineAt(i);
    }
    result += QLatin1Char('\n') + m_doc->lineAt(e.line).left(e.column);
    return result;
}

void CodeEditArea::setSelection(TextCursor anchor, TextCursor cursor) {
    anchor = m_cursorCtrl->clamp(anchor);
    cursor = m_cursorCtrl->clamp(cursor);
    const bool cursorChanged = cursor != m_cursor;
    const bool selectionChangedValue = anchor != m_anchor || cursorChanged;
    m_anchor = anchor;
    m_cursor = cursor;
    m_caretPainter->resetBlink();
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
    if (m_doc && m_doc->lineCount() > 0) {
        for (auto selection : selections) {
            selection.start = m_cursorCtrl->clamp(selection.start);
            selection.end = m_cursorCtrl->clamp(selection.end);
            if (selection.end < selection.start) std::swap(selection.start, selection.end);
            if (selection.start == selection.end) continue;
            normalized.append(selection);
        }
    }
    if (normalized.isEmpty()) m_selectionLayers.remove(layer);
    else m_selectionLayers.insert(layer, normalized);
    rebuildDecorations();
}

void CodeEditArea::clearAllExtraSelections() {
    if (m_selectionLayers.isEmpty()) return;
    m_selectionLayers.clear();
    rebuildDecorations();
}

void CodeEditArea::rebuildDecorations() {
    m_extraSelectionsByLine.clear();
    if (!m_doc || m_doc->lineCount() == 0 || m_selectionLayers.isEmpty()) {
        viewport()->update();
        return;
    }
    // Each decoration as (layer, index); active ones are ordered by both, so
    // the last of a layer is that layer's top entry.
    struct Event { int column; int layer; int index; bool opening; };
    QHash<int, QVector<Event>> eventsByLine;
    for (auto layer = m_selectionLayers.cbegin(); layer != m_selectionLayers.cend(); ++layer) {
        for (int index = 0; index < layer->size(); ++index) {
            const ExtraSelection& selection = layer->at(index);
            for (int line = selection.start.line; line <= selection.end.line; ++line) {
                const int start = line == selection.start.line ? selection.start.column : 0;
                const int end = line == selection.end.line ? selection.end.column : m_doc->lineAt(line).size();
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
            const ExtraSelection& top = m_selectionLayers[it->first][it->second];
            if (top.background.isValid()) style.background = top.background;
            if (top.foreground.isValid()) style.foreground = top.foreground;
            if (top.underline.isValid()) style.underline = top.underline;
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
        auto& segments = m_extraSelectionsByLine[it.key()];
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
    if (!m_doc || m_doc->lineCount() == 0) {
        return;
    }
    m_anchor = {0, 0};
    const int last = m_doc->lineCount() - 1;
    m_cursor = m_cursorCtrl->clamp({last, INT_MAX});
    m_caretPainter->resetBlink();
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

void CodeEditArea::clearSelection() {
    if (!hasSelection()) {
        return;
    }
    m_anchor = m_cursor;
    viewport()->update();
    updateInputMethod();
    emit selectionChanged();
}

void CodeEditArea::setSelectionColor(const QColor& color) {
    m_selectionColor = color;
    if (hasSelection()) {
        viewport()->update();
    }
}

void CodeEditArea::setLineBackgroundProvider(LineBackgroundFn fn) {
    m_lineBgProvider = std::move(fn);
    viewport()->update();
}

void CodeEditArea::setCurrentLineColor(const QColor& color) {
    m_currentLineColor = color;
    viewport()->update();
}

QColor CodeEditArea::lineBackground(int line) const {
    if (m_lineBgProvider) {
        const QColor bg = m_lineBgProvider(line);
        if (bg.isValid()) return bg;
    }
    return line == m_cursor.line ? m_currentLineColor : QColor();
}

// --- Undo / redo --------------------------------------------------------

void CodeEditArea::undo() {
    if (m_readOnly) { showReadOnlyHint(); return; }
    if (!m_undoStack->canUndo()) {
        return;
    }
    m_undoStack->undo();
    updateAfterEdit();
}

void CodeEditArea::redo() {
    if (m_readOnly) { showReadOnlyHint(); return; }
    if (!m_undoStack->canRedo()) {
        return;
    }
    m_undoStack->redo();
    updateAfterEdit();
}

bool CodeEditArea::canUndo() const { return m_undoStack->canUndo(); }
bool CodeEditArea::canRedo() const { return m_undoStack->canRedo(); }

// --- Editing ------------------------------------------------------------

void CodeEditArea::insertText(const QString& text) {
    if (text.isEmpty() && !hasSelection()) return;
    executeInsert(text);
}

bool CodeEditArea::replaceRanges(const QList<TextReplacement>& replacements, const QString& undoText) {
    if (!m_doc || replacements.isEmpty()) return false;
    if (m_readOnly) {
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
    TextCursor cursorAfter = m_cursor;
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
            run.removed << m_doc->lineAt(line);
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
    const TextCursor cursorBefore = m_cursor, anchorBefore = m_anchor;
    beginBatch();
    for (const ReplaceLinesCommand::Run& run : std::as_const(runs))
        m_doc->replaceLines(run.first, int(run.removed.size()), run.inserted);
    endBatch();
    m_cursor = m_anchor = m_cursorCtrl->clamp(cursorAfter);
    auto* command = new ReplaceLinesCommand(m_doc, runs, cursorBefore, anchorBefore, m_cursor, m_anchor,
                                            &m_cursor, &m_anchor, [this]() { beginBatch(); },
                                            [this]() { endBatch(); });
    command->setText(undoText);
    m_undoStack->push(command);
    updateAfterEdit();
    return true;
}

void CodeEditArea::cut() {
    if (!hasSelection()) return;
    if (m_readOnly) { showReadOnlyHint(); return; }
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
    const bool accepted = m_readOnly || !m_insertFilter || m_insertFilter(text);
    if (accepted && !text.isEmpty()) {
        executeInsert(text);
    }
}

QMenu* CodeEditArea::createStandardContextMenu(QWidget* parent) {
    auto* menu = new QMenu(parent ? parent : this);
    const bool editable = m_doc && !m_readOnly;
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
        ->setEnabled(m_doc != nullptr);
    return menu;
}

// --- Configuration ------------------------------------------------------

void CodeEditArea::setTabWidth(int spaces) {
    m_renderer->setTabWidth(spaces);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

int CodeEditArea::tabWidth() const {
    return m_renderer->tabWidth();
}

void CodeEditArea::setPlaceholderText(const QString& text) {
    if (m_placeholderText == text) return;
    m_placeholderText = text;
    viewport()->update();
}

int CodeEditArea::visualRowCount() const {
    if (!m_doc) return 1;
    // The wrap layout is empty until the viewport has a width.
    if (hasRows()) return m_wrapLayout->totalRows();
    int rows = 0;
    for (int line = 0; line < m_doc->lineCount(); ++line)
        if (m_foldState.isLineVisible(line)) ++rows;
    return qMax(1, rows);
}

void CodeEditArea::setTabCaptured(bool captured) {
    m_tabCaptured = captured;
}

bool CodeEditArea::focusNextPrevChild(bool next) {
    // Tab and Shift+Tab (without Ctrl or Alt) then reach keyPressEvent().
    if (m_tabCaptured && !m_readOnly && m_doc) return false;
    return QAbstractScrollArea::focusNextPrevChild(next);
}

void CodeEditArea::setReadOnly(bool ro) {
    if (m_readOnly == ro) return;
    m_readOnly = ro;
    // Like QPlainTextEdit: a read-only area takes no input-method text.
    setAttribute(Qt::WA_InputMethodEnabled, !ro);
    if (ro) cancelPreedit(true);
    updateInputMethod(Qt::ImEnabled);
}

void CodeEditArea::setShowWhitespace(bool show) {
    m_renderer->setShowWhitespace(show);
    viewport()->update();
}

bool CodeEditArea::showWhitespace() const {
    return m_renderer->showWhitespace();
}

void CodeEditArea::setHighlighter(IHighlighter* hl) {
    m_highlighter = hl;
    if (hl) {
        m_renderer->setAttributePalette(&hl->attributes());
        m_renderer->setSpansProvider([this](int line) -> const QVector<StyleSpan>* {
            if (line < 0 || line >= m_validLines || line >= m_lineSpans.size()) return nullptr;
            return &m_lineSpans[line];
        });
    } else {
        m_renderer->setAttributePalette(nullptr);
        m_renderer->setSpansProvider({});
    }
    rebuildHighlightCache();
    viewport()->update();
}

void CodeEditArea::setFoldingProvider(IFoldingProvider* p) {
    m_foldingProvider = p;
    m_foldState.clear();
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::toggleFoldAt(int line) {
    const int idx = m_foldState.regionStartingAt(line);
    if (idx < 0) return;
    m_foldState.toggle(idx);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::foldAll() {
    m_foldState.foldAll();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::unfoldAll() {
    m_foldState.unfoldAll();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::setWordWrap(bool wrap) {
    if (m_wordWrap == wrap) return;
    m_wordWrap = wrap;
    horizontalScrollBar()->setVisible(!wrap);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
}

void CodeEditArea::setCaretBlinkInterval(int ms) {
    m_caretPainter->setBlinkInterval(ms);
}

int CodeEditArea::caretBlinkInterval() const {
    return m_caretPainter->blinkInterval();
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
    if (!m_placeholderText.isEmpty() && m_doc && m_doc->lineCount() <= 1
            && m_doc->lineAt(0).isEmpty() && m_preedit.isEmpty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        const int lineHeight = LineRenderer::lineHeightFor(font());
        p.drawText(QRect(LineRenderer::kLeftPaddingPx, 0,
                         viewport()->width() - LineRenderer::kLeftPaddingPx, lineHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, m_placeholderText);
    }
    p.setPen(palette().text().color());
    m_renderer->setSelectionRegion(selectionRegion());
    // Lazy highlighting: make sure every visible line is highlighted.
    if (m_highlighter && m_doc && !highlightComplete()
            && m_lineEndStates.size() == m_doc->lineCount()) {
        highlightUpTo(qMax(m_viewportState.lastVisibleLine, m_cursor.line) + 1);
    }
    m_renderer->paint(p, m_doc, m_viewportState);
    paintPreedit(p);
    if (!m_preedit.isEmpty() && !m_preeditCursorVisible) {
        return;  // the input method asked to hide the caret
    }
    int caretVisualCol, caretVisualRow;
    if (m_doc) {
        caretVisualRow = visualRowOf(m_cursor);
        const int rowStart = hasRows()
            ? m_wrapLayout->rowAt(caretVisualRow).startCol : 0;
        const QString seg = m_doc->lineAt(m_cursor.line).mid(rowStart);
        caretVisualCol = LineRenderer::visualColumn(seg, m_cursor.column - rowStart, tabWidth());
    } else {
        caretVisualRow = m_cursor.line;
        caretVisualCol = m_cursor.column;
    }
    m_caretPainter->paint(p, m_cursor, caretVisualCol, caretVisualRow, m_viewportState, font(),
                          m_preedit.isEmpty() ? 0 : preeditCursorOffsetPx());
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
    m_renderer->setFont(font());
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
    if (!m_doc) {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }

    const bool ctrl  = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool alt   = e->modifiers() & Qt::AltModifier;
    // Windows reports AltGr as Ctrl+Alt: Ctrl shortcuts must not swallow
    // AltGr characters (Polish ą = AltGr+A, ć = AltGr+C, ż = AltGr+Z, …).
    const bool cmd   = ctrl && !alt;
    const CursorController& cc = *m_cursorCtrl;
    const TextCursor c = m_cursor;

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
            const TextCursor from = cc.moveWordLeft(m_cursor);
            if (from != m_cursor) executeRemove(from, m_cursor);
        } else if (m_cursor.column > 0) {
            // One code point (a surrogate pair as a whole).
            const int from = codepoints::prevIndex(m_doc->lineAt(m_cursor.line), m_cursor.column);
            executeRemove({m_cursor.line, from}, m_cursor);
        } else if (m_cursor.line > 0) {
            const int prevLen = m_doc->lineAt(m_cursor.line - 1).size();
            executeRemove({m_cursor.line - 1, prevLen}, m_cursor);
        }
        break;

    case Qt::Key_Delete:
        if (hasSelection()) {
            executeRemoveSelection();
        } else if (ctrl) {
            // Delete to the next word stop (joins lines at the line end).
            const TextCursor to = cc.moveWordRight(m_cursor);
            if (to != m_cursor) executeRemove(m_cursor, to);
        } else {
            const QString lineText = m_doc->lineAt(m_cursor.line);
            if (m_cursor.column < lineText.size()) {
                // One code point (a surrogate pair as a whole).
                executeRemove(m_cursor, {m_cursor.line, codepoints::nextIndex(lineText, m_cursor.column)});
            } else if (m_cursor.line < m_doc->lineCount() - 1) {
                executeRemove(m_cursor, {m_cursor.line + 1, 0});
            }
        }
        break;

    case Qt::Key_Tab:
        // Ctrl+Tab always passes through (tab switching in host application).
        if (ctrl) {
            QAbstractScrollArea::keyPressEvent(e);
            return;
        }
        if (m_tabCaptured) {
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
        if (m_tabCaptured) {
            // Dedent: remove one leading tab, or up to tabWidth leading spaces.
            const QString line = m_doc->lineAt(m_cursor.line);
            if (!line.isEmpty() && line.at(0) == QLatin1Char('\t')) {
                executeRemove({m_cursor.line, 0}, {m_cursor.line, 1});
            } else {
                int spaces = 0;
                while (spaces < tabWidth() && spaces < line.size()
                       && line.at(spaces) == QLatin1Char(' ')) {
                    ++spaces;
                }
                if (spaces > 0) {
                    executeRemove({m_cursor.line, 0}, {m_cursor.line, spaces});
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
        m_overwrite = !m_overwrite;
        m_caretPainter->setOverwrite(m_overwrite);
        break;

    case Qt::Key_Minus:
        if (cmd && !shift) { toggleFoldAt(m_cursor.line); break; }
        goto handle_printable;

    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (cmd && !shift) { toggleFoldAt(m_cursor.line); break; }
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
        if (!m_preedit.isEmpty()) {
            // Like QPlainTextEdit: a click finishes the composition where it is.
            QGuiApplication::inputMethod()->commit();
            cancelPreedit(false);
        }
        m_mouseSelecting = false;
        // Placeholder hit-test: if the click falls on a collapsed region's
        // "{…}" box, unfold it instead of moving the cursor.
        if (m_viewportState.isValid() && !m_viewportState.rows.isEmpty()
                && m_viewportState.lineHeight > 0 && m_doc) {
            const int rowIdx = e->pos().y() / m_viewportState.lineHeight;
            if (rowIdx >= 0 && rowIdx < m_viewportState.rows.size()) {
                const auto& row = m_viewportState.rows[rowIdx];
                if (!row.foldPlaceholder.isEmpty()) {
                    const QString& line = m_doc->lineAt(row.logicalLine);
                    const int drawEnd = (row.foldStartColumn >= 0)
                        ? qMin((int)row.endCol, row.foldStartColumn)
                        : row.endCol;
                    const int visLen = LineRenderer::visualColumn(
                        line, drawEnd, tabWidth())
                        - LineRenderer::visualColumn(line, row.startCol, tabWidth());
                    const int phX = LineRenderer::kLeftPaddingPx
                                    + visLen * m_viewportState.charWidth;
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
        m_mouseSelecting = true;
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
    if (e->button() != Qt::LeftButton || !m_doc) {
        QAbstractScrollArea::mouseDoubleClickEvent(e);
        return;
    }
    m_mouseSelecting = false;
    const TextCursor pos = cursorFromPoint(e->pos());
    const QString& line = m_doc->lineAt(pos.line);
    int start = pos.column, end = pos.column;
    // The character clicked on (the cursor goes to the nearest boundary, so
    // a click on a character's right half gives the column after it); at the
    // end of the line the one before it.
    const int half = qMax(1, m_viewportState.charWidth / 2);
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
        if (m_mouseSelecting) {
            applySelectionMove(cursorFromPoint(e->pos()));
        }
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(e);
}

void CodeEditArea::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_mouseSelecting = false;
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void CodeEditArea::focusInEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusInEvent(e);
    m_caretPainter->setFocused(true);
}

void CodeEditArea::focusOutEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusOutEvent(e);
    m_caretPainter->setFocused(false);
    // Qt commits the composition before the focus moves; whatever is left
    // is stale.
    cancelPreedit(false);
}

// ------------------------------------------------------------------------
// Input methods
// ------------------------------------------------------------------------

void CodeEditArea::inputMethodEvent(QInputMethodEvent* e) {
    if (!m_doc || m_readOnly) {
        cancelPreedit(false);
        e->ignore();
        return;
    }

    // Replacement range: relative to the cursor, limited to the current line.
    const int line = m_cursor.line;
    const int lineLen = m_doc->lineAt(line).size();
    int repFrom = 0, repTo = 0;
    if (e->replacementLength() > 0) {
        repFrom = qBound(0, m_cursor.column + e->replacementStart(), lineLen);
        repTo   = qBound(repFrom,
                         m_cursor.column + e->replacementStart() + e->replacementLength(),
                         lineLen);
    }

    const QString commit = e->commitString();
    if (!commit.isEmpty() || repFrom < repTo) {
        // Replacement and commit form a single undo step.
        m_undoStack->beginMacro(QString());
        if (repFrom < repTo) executeRemove({line, repFrom}, {line, repTo});
        if (!commit.isEmpty()) insertTypedText(commit);
        m_undoStack->endMacro();
    }

    m_preedit = e->preeditString();
    m_preeditAttributes = e->attributes();
    m_preeditCursor = m_preedit.size();
    m_preeditCursorVisible = true;
    for (const QInputMethodEvent::Attribute& a : std::as_const(m_preeditAttributes)) {
        if (a.type == QInputMethodEvent::Cursor) {
            m_preeditCursor = qBound(0, a.start, int(m_preedit.size()));
            m_preeditCursorVisible = a.length != 0;
        }
    }
    if (m_preedit.isEmpty()) {
        m_preeditAttributes.clear();
    } else {
        ensureCursorVisible(m_cursor, preeditColumns());
    }

    m_caretPainter->resetBlink();
    viewport()->update();
    updateInputMethod(Qt::ImCursorRectangle);
    e->accept();
}

QVariant CodeEditArea::inputMethodQuery(Qt::InputMethodQuery query) const {
    const QString line = m_doc ? m_doc->lineAt(m_cursor.line) : QString();
    switch (query) {
    case Qt::ImEnabled:
        return !m_readOnly;
    case Qt::ImCursorRectangle:
        return caretRect(true).translated(viewport()->pos());
    case Qt::ImFont:
        return font();
    case Qt::ImCursorPosition:
        return m_cursor.column;
    case Qt::ImAnchorPosition:
        // Columns refer to the current line; an anchor on another line is
        // reported at the matching end of it.
        if (m_anchor.line == m_cursor.line) return m_anchor.column;
        return m_anchor.line < m_cursor.line ? 0 : int(line.size());
    case Qt::ImSurroundingText:
        return line;
    case Qt::ImCurrentSelection:
        return (hasSelection() && m_anchor.line == m_cursor.line) ? selectedText() : QString();
    case Qt::ImTextBeforeCursor:
        return line.left(m_cursor.column);
    case Qt::ImTextAfterCursor:
        return line.mid(m_cursor.column);
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
    if (m_preedit.isEmpty()) return;
    m_preedit.clear();
    m_preeditAttributes.clear();
    m_preeditCursor = 0;
    m_preeditCursorVisible = true;
    if (resetInputMethod && hasFocus()) {
        QGuiApplication::inputMethod()->reset();
    }
    viewport()->update();
}

QRect CodeEditArea::caretRect(bool withPreedit) const {
    const ViewportState& vp = m_viewportState;
    const QFontMetrics fm(font());
    const int height = fm.ascent() + fm.descent();
    if (!m_doc || !vp.isValid()) {
        return QRect(LineRenderer::kLeftPaddingPx, 0, 1, height);
    }
    const int row = visualRowOf(m_cursor);
    const int rowStart = hasRows() ? m_wrapLayout->rowAt(row).startCol : 0;
    const int visualCol = LineRenderer::visualColumn(
        m_doc->lineAt(m_cursor.line).mid(rowStart), m_cursor.column - rowStart, tabWidth());
    int x = LineRenderer::kLeftPaddingPx + visualCol * vp.charWidth - vp.contentOffsetX;
    if (withPreedit && !m_preedit.isEmpty()) {
        x += preeditCursorOffsetPx();
    }
    const int y = vp.contentOffsetY + (row - vp.firstVisibleRow) * vp.lineHeight;
    return QRect(x, y, 1, height);
}

int CodeEditArea::preeditCursorOffsetPx() const {
    return QFontMetrics(font()).horizontalAdvance(m_preedit.left(m_preeditCursor));
}

int CodeEditArea::preeditColumns() const {
    const int cw = m_viewportState.charWidth;
    if (cw <= 0 || m_preedit.isEmpty()) return 0;
    return (QFontMetrics(font()).horizontalAdvance(m_preedit) + cw - 1) / cw;
}

// ------------------------------------------------------------------------
// Document signal handlers
// ------------------------------------------------------------------------

void CodeEditArea::onDocumentReset() {
    clearAllExtraSelections();
    m_cursor = m_cursorCtrl->clamp(TextCursor{});
    m_anchor = m_cursor;
    m_undoStack->clear();
    m_knownLineCount = m_doc ? m_doc->lineCount() : 0;
    m_pendingRehighlightFrom = -1;
    rebuildHighlightCache();
    m_foldState.clear();
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
    emit textChanged();
}

void CodeEditArea::onLinesInserted(int startLine, int count) {
    clearAllExtraSelections();
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursorCtrl->clamp(m_anchor);
    m_knownLineCount += count;
    const int rehighlightStart = takePendingRehighlight(startLine);
    if (m_highlighter) {
        // Lines inserted inside the highlighted prefix belong to it (they
        // are highlighted by rehighlightFrom below).
        // "<=": lines appended right after the prefix (e.g. at the end of a
        // fully highlighted document) can be highlighted now as well.
        if (startLine <= m_validLines) m_validLines += count;
        m_lineEndStates.insert(startLine, count, HighlightState{});
        m_lineSpans.insert(startLine, count, {});
        m_lineFolds.insert(startLine, count, {});
        if (m_batchDepth == 0) rehighlightFrom(rehighlightStart);
    }
    m_foldState.shiftLines(startLine, count);  // keep collapsed regions in place
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
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursorCtrl->clamp(m_anchor);
    m_knownLineCount -= count;
    const int rehighlightStart = takePendingRehighlight(startLine);
    if (m_highlighter) {
        if (startLine < m_validLines) m_validLines -= qMin(count, m_validLines - startLine);
        const int removable = qBound(0, int(m_lineEndStates.size()) - startLine, count);
        m_lineEndStates.remove(startLine, removable);
        m_lineSpans.remove(startLine, qBound(0, int(m_lineSpans.size()) - startLine, count));
        m_lineFolds.remove(startLine, qBound(0, int(m_lineFolds.size()) - startLine, count));
        if (m_batchDepth == 0) rehighlightFrom(rehighlightStart);
    }
    m_foldState.shiftLines(startLine, -count);
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
    if (m_doc && m_doc->lineCount() != m_knownLineCount) {
        m_pendingRehighlightFrom = m_pendingRehighlightFrom < 0
            ? startLine : qMin(m_pendingRehighlightFrom, startLine);
        return;
    }
    if (batched(startLine)) return;
    if (m_highlighter) {
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
    ++m_batchDepth;
}

bool CodeEditArea::batched(int startLine) {
    if (m_batchDepth == 0) return false;
    m_batchRehighlightFrom = m_batchRehighlightFrom < 0 ? startLine : qMin(m_batchRehighlightFrom, startLine);
    return true;
}

// The end of a batch: what each change would have brought up to date, once.
void CodeEditArea::endBatch() {
    if (m_batchDepth == 0 || --m_batchDepth > 0 || m_batchRehighlightFrom < 0) return;
    const int from = takePendingRehighlight(m_batchRehighlightFrom);
    m_batchRehighlightFrom = -1;
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursorCtrl->clamp(m_anchor);
    if (m_highlighter) rehighlightFrom(from);
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit textChanged();
}

void CodeEditArea::onTextReplacementStarted() {
    beginBatch();
    m_replacement = std::make_unique<Replacement>();
    m_replacement->cursorBefore = m_cursor;
    m_replacement->anchorBefore = m_anchor;
    m_replacement->cursor = m_cursor;
    m_replacement->anchor = m_anchor;
    m_replacement->topLine = m_viewportState.firstVisibleLine;
}

void CodeEditArea::onLinesReplaced(int first, const QStringList& removed, const QStringList& inserted) {
    if (!m_replacement) return;
    Replacement& r = *m_replacement;
    r.cursor = mapThroughRun(r.cursor, first, removed.size(), inserted.size());
    r.anchor = mapThroughRun(r.anchor, first, removed.size(), inserted.size());
    r.topLine = mapThroughRun({r.topLine, 0}, first, removed.size(), inserted.size()).line;
    r.runs.append({first, removed, inserted});
}

// The document has changed already; the command only records it for undo.
void CodeEditArea::onTextReplacementFinished() {
    endBatch();
    if (!m_replacement) return;
    const std::unique_ptr<Replacement> r = std::move(m_replacement);
    cancelPreedit(true);
    m_cursor = m_cursorCtrl->clamp(r->cursor);
    m_anchor = m_cursorCtrl->clamp(r->anchor);
    m_undoStack->push(new ReplaceLinesCommand(m_doc, r->runs, r->cursorBefore, r->anchorBefore,
                                              m_cursor, m_anchor, &m_cursor, &m_anchor,
                                              [this]() { beginBatch(); }, [this]() { endBatch(); }));
    // The same text stays at the top of the view.
    const int topLine = qBound(0, r->topLine, qMax(0, m_doc->lineCount() - 1));
    verticalScrollBar()->setValue(hasRows() ? m_wrapLayout->firstRowOf(topLine) : topLine);
    m_caretPainter->resetBlink();
    ensureCursorVisible(m_cursor);
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

int CodeEditArea::takePendingRehighlight(int startLine) {
    const int from = m_pendingRehighlightFrom < 0
        ? startLine : qMin(m_pendingRehighlightFrom, startLine);
    m_pendingRehighlightFrom = -1;
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
    s.wordWrap       = m_wordWrap;

    // Rows with and without word wrap (without it a row per visible line);
    // lines only before the layout is built
    if (hasRows()) {
        s.contentOffsetX = (!m_wordWrap && charWidth > 0)
            ? horizontalScrollBar()->value() * charWidth : 0;
        const int totalRows = m_wrapLayout->totalRows();
        if (lineHeight > 0 && totalRows > 0) {
            const int firstRow = verticalScrollBar()->value();
            s.firstVisibleRow = qMin(firstRow, totalRows - 1);
            const int maxVisible = (vpH + lineHeight - 1) / lineHeight;
            s.lastVisibleRow = qMin(s.firstVisibleRow + maxVisible - 1, totalRows - 1);

            int prevLogical = -1;
            for (int r = s.firstVisibleRow; r <= s.lastVisibleRow; ++r) {
                const WrapLayout::Row& wr = m_wrapLayout->rowAt(r);
                ViewportState::RowInfo ri;
                ri.logicalLine = wr.logicalLine;
                ri.startCol    = wr.startCol;
                ri.endCol      = wr.endCol;
                ri.isFirstRow  = (wr.logicalLine != prevLogical);
                if (ri.isFirstRow) {
                    const int regIdx = m_foldState.regionStartingAt(wr.logicalLine);
                    if (regIdx >= 0 && m_foldState.isCollapsed(regIdx)) {
                        const FoldRegion& fr = m_foldState.regions()[regIdx];
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
        const int lineCount = m_doc ? m_doc->lineCount() : 0;
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

    m_viewportState = s;
    emit viewportChanged(m_viewportState);
}

void CodeEditArea::updateScrollBarRanges() {
    const QFontMetrics fm(font());
    const int lineHeight = LineRenderer::lineHeightFor(font());
    const int charWidth  = fm.horizontalAdvance(QLatin1Char('M'));
    const int vpH = viewport()->height();
    const int vpW = viewport()->width();
    const int visibleLines = (lineHeight > 0) ? (vpH / lineHeight) : 0;

    // Vertically rows (folded lines left out), lines before the layout is built
    const int rowCount = hasRows() ? m_wrapLayout->totalRows() : (m_doc ? m_doc->lineCount() : 0);
    verticalScrollBar()->setRange(0, qMax(0, rowCount - visibleLines));
    verticalScrollBar()->setPageStep(qMax(1, visibleLines));
    verticalScrollBar()->setSingleStep(1);
    if (m_wordWrap) {
        horizontalScrollBar()->setRange(0, 0);
    } else {

        const int visibleCols = (charWidth > 0) ? (vpW / charWidth) : 0;
        const int maxCols = m_doc ? m_doc->maxLineLength() : 0;
        const int hMax = qMax(0, maxCols - visibleCols);
        horizontalScrollBar()->setRange(0, hMax);
        horizontalScrollBar()->setPageStep(qMax(1, visibleCols));
        horizontalScrollBar()->setSingleStep(1);
    }
}

void CodeEditArea::rebindDocumentSignals(ITextDocument* newDoc) {
    if (m_doc) {
        disconnect(m_doc, nullptr, this, nullptr);
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
    const TextCursor clamped = m_cursorCtrl->clamp(newPos);
    const bool posChanged = (clamped != m_cursor);
    const bool selWas = hasSelection();

    m_cursor = clamped;
    m_anchor = clamped;

    if (!posChanged && !selWas) {
        return;
    }
    m_caretPainter->resetBlink();
    ensureCursorVisible(m_cursor);
    viewport()->update();
    updateInputMethod();
    if (posChanged) emit cursorPositionChanged(m_cursor);
    if (selWas)     emit selectionChanged();
}

void CodeEditArea::applySelectionMove(TextCursor newPos) {
    const TextCursor clamped = m_cursorCtrl->clamp(newPos);
    if (clamped == m_cursor) {
        return;
    }
    m_cursor = clamped;
    m_caretPainter->resetBlink();
    ensureCursorVisible(m_cursor);
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

TextCursor CodeEditArea::cursorFromPoint(const QPoint& pt) const {
    const ViewportState& vp = m_viewportState;
    if (!m_doc || !vp.isValid()) return {};

    const int tw = tabWidth();

    if (!vp.rows.isEmpty()) {
        const int ri = qBound(0, pt.y() / vp.lineHeight, vp.rows.size() - 1);
        const ViewportState::RowInfo& row = vp.rows[ri];
        const int targetX = pt.x() - LineRenderer::kLeftPaddingPx + vp.contentOffsetX;
        const QString seg = m_doc->lineAt(row.logicalLine)
                                .mid(row.startCol, row.endCol - row.startCol);
        int visual = 0, segCol = 0;
        while (segCol < seg.size()) {
            const int cw = (seg.at(segCol) == QLatin1Char('\t'))
                ? (tw - (visual % tw)) : codepoints::cellsAt(seg, segCol);
            if (targetX * 2 < (2 * visual + cw) * vp.charWidth) break;
            visual += cw;
            segCol = codepoints::nextIndex(seg, segCol);  // a surrogate pair is one cell
        }
        return m_cursorCtrl->clamp({row.logicalLine, row.startCol + segCol});
    }

    const int lineNum = qBound(0,
        vp.firstVisibleLine + pt.y() / vp.lineHeight,
        m_doc->lineCount() - 1);
    const int targetX = pt.x() - LineRenderer::kLeftPaddingPx + vp.contentOffsetX;
    const QString lineStr = m_doc->lineAt(lineNum);
    int visual = 0, col = 0;
    while (col < lineStr.size()) {
        const int cw = (lineStr.at(col) == QLatin1Char('\t'))
            ? (tw - (visual % tw)) : codepoints::cellsAt(lineStr, col);
        if (targetX * 2 < (2 * visual + cw) * vp.charWidth) break;
        visual += cw;
        col = codepoints::nextIndex(lineStr, col);  // a surrogate pair is one cell
    }
    return m_cursorCtrl->clamp({lineNum, col});
}

void CodeEditArea::ensureVisible(TextCursor pos, int marginRows) {
    ensureCursorVisible(m_cursorCtrl->clamp(pos), 0, marginRows);
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
        scrollTo(visualRowOf(pos), m_viewportState.firstVisibleRow);
    else
        scrollTo(pos.line, m_viewportState.firstVisibleLine);
    if (m_wordWrap)
        return; // no horizontal scroll in wrap mode

    const int charWidth = m_viewportState.charWidth;
    if (charWidth <= 0) return;
    QScrollBar* hBar = horizontalScrollBar();
    const int firstCol    = hBar->value();
    const int visibleCols = m_viewportState.viewportWidth / charWidth;
    const int lastCol     = firstCol + visibleCols - 1;
    const int rightCol    = pos.column + extraColumns;
    if (pos.column < firstCol) {
        hBar->setValue(pos.column);
    } else if (rightCol > lastCol) {
        // Keep the left end visible if the right end does not fit.
        hBar->setValue(qMin(pos.column, qMax(0, rightCol - visibleCols + 1)));
    }
}

int CodeEditArea::pageLineCount() const {
    const int lh = m_viewportState.lineHeight;
    return (lh <= 0) ? 1 : qMax(1, m_viewportState.viewportHeight / lh);
}

// --- Word-wrap helpers ---------------------------------------------------

void CodeEditArea::rebuildWrapLayout() {
    if (!m_doc) return;
    const QFontMetrics fm(font());
    const int cw = fm.horizontalAdvance(QLatin1Char('M'));
    if (cw <= 0) return;
    // Without word wrap a row per line: folded lines are skipped as with it
    const int availCols = m_wordWrap
        ? qMax(1, (viewport()->width() - LineRenderer::kLeftPaddingPx) / cw)
        : WrapLayout::kNoWrap;
    const FoldState* fs = (m_foldState.regions().isEmpty()) ? nullptr : &m_foldState;
    m_wrapLayout->rebuild(m_doc, availCols, tabWidth(), fs);
}

bool CodeEditArea::hasRows() const {
    return m_doc && m_wrapLayout->totalRows() > 0;
}

int CodeEditArea::visualRowOf(TextCursor pos) const {
    if (!hasRows()) return pos.line;
    return m_wrapLayout->rowForCursor(pos.line, pos.column);
}

TextCursor CodeEditArea::moveVisualRows(TextCursor c, int delta) const {
    if (!hasRows()) {
        return delta < 0 ? m_cursorCtrl->moveUp(c, -delta) : m_cursorCtrl->moveDown(c, delta);
    }
    const int row = visualRowOf(c);
    const int target = qBound(0, row + delta, m_wrapLayout->totalRows() - 1);
    if (target == row) return c;  // first or last row: as moveUp() on line 0
    const int tw = tabWidth();
    // The same character cell in the target row, or its end.
    const WrapLayout::Row& from = m_wrapLayout->rowAt(row);
    const int x = LineRenderer::visualColumn(m_doc->lineAt(from.logicalLine).mid(from.startCol),
                                             c.column - from.startCol, tw);
    const WrapLayout::Row& to = m_wrapLayout->rowAt(target);
    const QString line = m_doc->lineAt(to.logicalLine);
    const QString seg = line.mid(to.startCol, to.endCol - to.startCol);
    int visual = 0, segCol = 0;
    while (segCol < seg.size()) {
        const int cw = (seg.at(segCol) == QLatin1Char('\t')) ? (tw - (visual % tw)) : codepoints::cellsAt(seg, segCol);
        if (2 * x < 2 * visual + cw) break;
        visual += cw;
        segCol = codepoints::nextIndex(seg, segCol);
    }
    // The end of a row the line continues after is the start of the next
    // row; stay on the last character of this one.
    if (segCol >= seg.size() && to.endCol < line.size() && !seg.isEmpty()) {
        segCol = codepoints::prevIndex(seg, int(seg.size()));
    }
    return m_cursorCtrl->clamp({to.logicalLine, to.startCol + segCol});
}

// Home of a wrapped line: the start of the screen row; at the start of a row
// that is not the line's first, the start of the line
TextCursor CodeEditArea::moveToRowStart(TextCursor c) const {
    if (!m_wordWrap || !m_doc || m_wrapLayout->totalRows() == 0)
        return m_cursorCtrl->moveToLineStart(c);
    const WrapLayout::Row& row = m_wrapLayout->rowAt(visualRowOf(c));
    if (row.logicalLine != c.line || c.column == row.startCol)
        return m_cursorCtrl->moveToLineStart(c);
    return {c.line, row.startCol};
}

// End of a wrapped line: the end of the screen row (its last character: the
// position after it is the start of the next row); there already, or on the
// line's last row, the end of the line
TextCursor CodeEditArea::moveToRowEnd(TextCursor c) const {
    if (!m_wordWrap || !m_doc || m_wrapLayout->totalRows() == 0)
        return m_cursorCtrl->moveToLineEnd(c);
    const WrapLayout::Row& row = m_wrapLayout->rowAt(visualRowOf(c));
    const QString line = m_doc->lineAt(c.line);
    if (row.logicalLine != c.line || row.endCol >= line.size() || row.endCol <= row.startCol)
        return m_cursorCtrl->moveToLineEnd(c);
    const TextCursor rowEnd{c.line, codepoints::prevIndex(line, row.endCol)};
    return c == rowEnd ? m_cursorCtrl->moveToLineEnd(c) : rowEnd;
}

// --- Highlighting helpers ------------------------------------------------

void CodeEditArea::rebuildHighlightCache() {
    m_highlightTimer->stop();
    m_validLines = 0;
    m_lineEndStates.clear();
    m_lineSpans.clear();
    m_lineFolds.clear();
    if (!m_highlighter || !m_doc) return;
    const int n = m_doc->lineCount();
    m_lineEndStates.resize(n);
    m_lineSpans.resize(n);
    m_lineFolds.resize(n);
    if (n <= kSyncHighlightLines) {
        highlightUpTo(n);
    } else {
        // Large document: paintEvent() highlights what is visible, the timer
        // the rest, a slice at a time, without blocking the event loop.
        m_highlightTimer->start();
    }
}

int CodeEditArea::highlightedLineCount() const {
    return m_highlighter ? m_validLines : (m_doc ? m_doc->lineCount() : 0);
}

bool CodeEditArea::highlightComplete() const {
    return !m_highlighter || !m_doc || m_validLines >= m_doc->lineCount();
}

QList<QPair<int, int>> CodeEditArea::spellCheckRanges(int line) {
    QList<QPair<int, int>> ranges;
    if (!m_doc || line < 0 || line >= m_doc->lineCount()) return ranges;
    const int length = int(m_doc->lineAt(line).size());
    if (!m_highlighter) {
        if (length > 0) ranges.append({0, length});
        return ranges;
    }
    if (line >= m_validLines) highlightUpTo(line + 1);
    if (line >= m_validLines || line >= m_lineSpans.size()) return ranges;
    // Text outside any span, or in a span without an attribute, is code.
    const QVector<TextAttribute>& attributes = m_highlighter->attributes();
    for (const StyleSpan& span : m_lineSpans[line]) {
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
    if (!m_highlighter || !m_doc) return;
    const int end = qMin(lineCount, int(m_lineEndStates.size()));
    HighlightState stateIn = (m_validLines == 0)
        ? m_highlighter->initialState()
        : m_lineEndStates[m_validLines - 1];
    for (int i = m_validLines; i < end; ++i) {
        HighlightState stateOut;
        m_highlighter->highlightLineWithFolds(m_doc->lineAt(i), stateIn,
                                              m_lineSpans[i], stateOut, m_lineFolds[i]);
        m_lineEndStates[i] = stateOut;
        stateIn = stateOut;
    }
    m_validLines = qMax(m_validLines, end);
}

void CodeEditArea::highlightChunk() {
    if (!m_highlighter || !m_doc) {
        m_highlightTimer->stop();
        return;
    }
    // While the document reports a change in two signals the caches do not
    // match it yet; continue on the next tick.
    if (m_lineEndStates.size() != m_doc->lineCount()) return;

    QElapsedTimer budget;
    budget.start();
    while (!highlightComplete() && budget.elapsed() < 8) {
        highlightUpTo(m_validLines + 512);
    }
    if (!highlightComplete()) return;

    m_highlightTimer->stop();
    if (m_foldsPending) {
        rebuildFolds();
        rebuildWrapLayout();
        updateScrollBarRanges();
        refreshViewportState();
    }
    viewport()->update();
    emit highlightingCompleted();
}

void CodeEditArea::rehighlightFrom(int startLine) {
    if (!m_highlighter || !m_doc) return;
    const int n = m_doc->lineCount();
    if (startLine >= n) return;
    // Keep cache vectors in sync with line count.
    if (m_lineEndStates.size() != n) {
        m_lineEndStates.resize(n);
        m_lineSpans.resize(n);
    }
    if (m_lineFolds.size() != n) {
        m_lineFolds.resize(n);
    }
    // Lines past the highlighted prefix are done by highlightUpTo() later.
    m_validLines = qMin(m_validLines, n);
    if (startLine >= m_validLines) return;

    HighlightState stateIn = (startLine == 0)
        ? m_highlighter->initialState()
        : m_lineEndStates[startLine - 1];

    for (int i = startLine; i < m_validLines; ++i) {
        const HighlightState oldEndState = m_lineEndStates[i];
        HighlightState stateOut;
        m_highlighter->highlightLineWithFolds(m_doc->lineAt(i), stateIn,
                                              m_lineSpans[i], stateOut, m_lineFolds[i]);
        m_lineEndStates[i] = stateOut;
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
    if (!m_foldingProvider || !m_doc) {
        m_foldsPending = false;
        m_foldState.setRegions({});
        return;
    }
    // Until the whole document is highlighted the markers are incomplete;
    // regions built from them would drop (and un-collapse) regions that end
    // further down. Keep the current regions and rebuild when it completes.
    if (!highlightComplete()) {
        m_foldsPending = true;
        if (!m_highlightTimer->isActive()) m_highlightTimer->start();
        return;
    }
    m_foldsPending = false;
    // Fast path: regions from the markers collected while highlighting.
    QVector<FoldRegion> regions;
    if (m_highlighter && m_lineFolds.size() == m_doc->lineCount()
            && m_foldingProvider->regionsFromLineMarkers(m_highlighter, m_lineFolds, regions)) {
        m_foldState.setRegions(std::move(regions));
        return;
    }
    m_foldState.setRegions(m_foldingProvider->computeRegions(m_doc));
}

// ------------------------------------------------------------------------
// Private — edit helpers
// ------------------------------------------------------------------------

void CodeEditArea::showReadOnlyHint() {
    int vRow = visualRowOf(m_cursor);
    int vCol = m_cursor.column;
    if (m_doc && m_viewportState.isValid()) {
        const int rowStart = hasRows() ? m_wrapLayout->rowAt(vRow).startCol : 0;
        vCol = LineRenderer::visualColumn(
            m_doc->lineAt(m_cursor.line).mid(rowStart),
            m_cursor.column - rowStart, tabWidth());
    }
    const int px = m_viewportState.isValid()
        ? LineRenderer::kLeftPaddingPx + vCol * m_viewportState.charWidth
              - m_viewportState.contentOffsetX
        : width() / 2;
    const int py = m_viewportState.isValid()
        ? m_viewportState.contentOffsetY
              + (vRow - m_viewportState.firstVisibleRow) * m_viewportState.lineHeight
        : height() / 3;
    QToolTip::showText(viewport()->mapToGlobal(QPoint(px, py)),
                       tr("This view is read-only"), viewport());
}

void CodeEditArea::executeInsert(const QString& text) {
    if (!m_doc || m_readOnly) {
        if (m_readOnly) showReadOnlyHint();
        return;
    }
    if (hasSelection()) {
        m_undoStack->beginMacro(QString());
        executeRemoveSelection();
        m_undoStack->push(new InsertCommand(
            m_doc, m_cursor, text, m_cursor, &m_cursor, &m_anchor));
        m_undoStack->endMacro();
    } else {
        m_undoStack->push(new InsertCommand(
            m_doc, m_cursor, text, m_cursor, &m_cursor, &m_anchor));
    }
    updateAfterEdit();
}

void CodeEditArea::executeRemove(TextCursor start, TextCursor end) {
    if (!m_doc || m_readOnly || start == end) {
        if (m_readOnly) showReadOnlyHint();
        return;
    }
    m_undoStack->push(new RemoveCommand(
        m_doc, start, end, m_cursor, &m_cursor, &m_anchor));
    updateAfterEdit();
}

void CodeEditArea::executeRemoveSelection() {
    if (!m_doc || m_readOnly || !hasSelection()) {
        if (m_readOnly) showReadOnlyHint();
        return;
    }
    m_undoStack->push(new RemoveCommand(
        m_doc, selectionStart(), selectionEnd(),
        m_cursor, &m_cursor, &m_anchor));
    updateAfterEdit();
}

void CodeEditArea::updateAfterEdit() {
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursor;
    // Collapsed regions survive edits; never leave the cursor on a hidden line
    // (e.g. Enter at the end of a collapsed header).
    if (m_foldState.expandContaining(m_cursor.line)) {
        rebuildWrapLayout();
        refreshViewportState();
    }
    m_caretPainter->resetBlink();
    ensureCursorVisible(m_cursor);
    updateScrollBarRanges();
    viewport()->update();
    updateInputMethod();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

void CodeEditArea::insertTypedText(const QString& typed) {
    QString text = typed;
    if (m_insertFilter && !m_readOnly && (!m_insertFilter(text) || text.isEmpty())) return;
    if (m_overwrite && !hasSelection()
            && m_doc
            && m_cursor.column < m_doc->lineAt(m_cursor.line).size()) {
        // Replace character under cursor in one undo step.
        m_undoStack->beginMacro(QString());
        executeRemove(m_cursor, {m_cursor.line, m_cursor.column + 1});
        executeInsert(text);
        m_undoStack->endMacro();
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
    if (!hasSelection() || !m_doc || !m_viewportState.isValid()) return {};
    const TextCursor s = selectionStart();
    const TextCursor e = selectionEnd();
    const ViewportState& vp = m_viewportState;
    const int tw = tabWidth();
    const auto band = LineRenderer::backgroundBand(font(), vp.lineHeight);
    QRegion region;

    if (!vp.rows.isEmpty()) {
        for (int ri = 0; ri < vp.rows.size(); ++ri) {
            const ViewportState::RowInfo& row = vp.rows[ri];
            // Skip rows outside selection's logical line range.
            if (row.logicalLine < s.line || row.logicalLine > e.line) continue;
            if (row.logicalLine == s.line && row.endCol <= s.column)   continue;
            if (row.logicalLine == e.line && row.startCol >= e.column) continue;

            const QString line = m_doc->lineAt(row.logicalLine);
            const QString seg  = line.mid(row.startCol, row.endCol - row.startCol);

            const int selStartInSeg = (row.logicalLine == s.line)
                ? qMax(s.column - row.startCol, 0) : 0;
            const int selEndInSeg = (row.logicalLine == e.line)
                ? qMin(e.column - row.startCol, (int)seg.size()) : (int)seg.size();

            const int vcStart = LineRenderer::visualColumn(seg, selStartInSeg, tw);
            int vcEnd;
            if (row.logicalLine == e.line) {
                vcEnd = LineRenderer::visualColumn(seg, selEndInSeg, tw);
            } else {
                vcEnd = qMax(LineRenderer::visualColumn(seg, seg.size(), tw),
                             (vp.viewportWidth + vp.contentOffsetX) / vp.charWidth + 1);
            }

            const int topY = vp.contentOffsetY + ri * vp.lineHeight;
            const int x = LineRenderer::kLeftPaddingPx + vcStart * vp.charWidth - vp.contentOffsetX;
            const int w = (vcEnd - vcStart) * vp.charWidth;
            if (w > 0) region += QRect(x, topY + band.offset, w, band.height);
        }
        return region;
    }

    const int first = qMax(s.line, vp.firstVisibleLine);
    const int last  = qMin(e.line, vp.lastVisibleLine);
    for (int i = first; i <= last; ++i) {
        const int topY = vp.contentOffsetY + (i - vp.firstVisibleLine) * vp.lineHeight;
        const QString lineStr = m_doc->lineAt(i);
        const int startCol = (i == s.line)
            ? LineRenderer::visualColumn(lineStr, s.column, tw) : 0;
        int endCol;
        if (i == e.line) {
            endCol = LineRenderer::visualColumn(lineStr, e.column, tw);
        } else {
            endCol = qMax(LineRenderer::visualColumn(lineStr, lineStr.size(), tw),
                          vp.viewportWidth / vp.charWidth + 1);
        }
        const int x = LineRenderer::kLeftPaddingPx + startCol * vp.charWidth - vp.contentOffsetX;
        const int w = (endCol - startCol) * vp.charWidth;
        if (w > 0) region += QRect(x, topY + band.offset, w, band.height);
    }
    return region;
}

void CodeEditArea::paintLineBackgrounds(QPainter& painter) {
    if ((!m_lineBgProvider && !m_currentLineColor.isValid())
            || !m_doc || !m_viewportState.isValid()) return;
    const ViewportState& vp = m_viewportState;
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
    if (m_preedit.isEmpty() || !m_doc || !m_viewportState.isValid()) return;
    const ViewportState& vp = m_viewportState;
    const int row = visualRowOf(m_cursor);
    if (row < vp.firstVisibleRow || row > vp.lastVisibleRow) return;

    const int x = caretRect(false).x();
    const int topY = vp.contentOffsetY + (row - vp.firstVisibleRow) * vp.lineHeight;
    const QFontMetrics fm(font());
    const int width = fm.horizontalAdvance(m_preedit);

    // Cover the rest of the row and repaint it shifted right of the pre-edit
    // (display only; the document is unchanged).
    painter.fillRect(QRect(x, topY, vp.viewportWidth - x, vp.lineHeight), palette().base());
    const QColor bg = lineBackground(m_cursor.line);
    if (bg.isValid()) {
        const auto band = LineRenderer::backgroundBand(font(), vp.lineHeight);
        painter.fillRect(x, topY + band.offset, vp.viewportWidth - x, band.height, bg);
    }
    const QString& line = m_doc->lineAt(m_cursor.line);
    const int rowEnd = hasRows() ? m_wrapLayout->rowAt(row).endCol : int(line.size());
    const QVector<StyleSpan>* spans =
        (m_highlighter && m_cursor.line < m_validLines && m_cursor.line < m_lineSpans.size())
            ? &m_lineSpans[m_cursor.line]
                                                              : nullptr;
    painter.setPen(palette().text().color());
    m_renderer->paintSegment(painter, line, m_cursor.column, rowEnd, x + width,
                             topY, vp.lineHeight, vp.charWidth, spans);

    // Backgrounds requested by the input method (e.g. the active clause).
    for (const QInputMethodEvent::Attribute& a : std::as_const(m_preeditAttributes)) {
        if (a.type != QInputMethodEvent::TextFormat) continue;
        const QTextCharFormat f = qvariant_cast<QTextFormat>(a.value).toCharFormat();
        if (f.background().style() == Qt::NoBrush) continue;
        const int ax = x + fm.horizontalAdvance(m_preedit.left(a.start));
        const int aw = fm.horizontalAdvance(m_preedit.mid(a.start, a.length));
        painter.fillRect(ax, topY, aw, vp.lineHeight, f.background());
    }

    painter.setFont(font());
    const int baselineY = topY + LineRenderer::backgroundBand(font(), vp.lineHeight).baseline;
    painter.drawText(x, baselineY, m_preedit);
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
        painter.fillRect(r, m_selectionColor);
    }
}

} // namespace qce
