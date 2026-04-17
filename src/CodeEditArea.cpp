#include "qce/CodeEditArea.h"

#include "qce/IFoldingProvider.h"
#include "qce/IHighlighter.h"
#include "qce/ITextDocument.h"
#include "CaretPainter.h"
#include "CursorController.h"
#include "EditCommands.h"
#include "LineRenderer.h"
#include "WrapLayout.h"

#include <QClipboard>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QUndoStack>

namespace qce {

// ------------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------------

CodeEditArea::CodeEditArea(QWidget* parent)
    : QAbstractScrollArea(parent),
      m_renderer(std::make_unique<LineRenderer>()),
      m_cursorCtrl(std::make_unique<CursorController>(nullptr)),
      m_caretPainter(std::make_unique<CaretPainter>(this)),
      m_undoStack(new QUndoStack(this)),
      m_wrapLayout(std::make_unique<WrapLayout>()) {
    QFont f(QStringLiteral("Monospace"));
    f.setStyleHint(QFont::TypeWriter);
    setFont(f);
    m_renderer->setFont(f);

    viewport()->setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    connect(m_caretPainter.get(), &CaretPainter::blinkToggled,
            viewport(), QOverload<>::of(&QWidget::update));

    refreshViewportState();
}

CodeEditArea::~CodeEditArea() = default;

// ------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------

void CodeEditArea::setDocument(ITextDocument* doc) {
    if (m_doc == doc) {
        return;
    }
    rebindDocumentSignals(doc);
    m_doc = doc;
    m_cursorCtrl->setDocument(doc);
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursor;
    m_undoStack->clear();

    rebuildHighlightCache();
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::setCursorPosition(TextCursor pos) {
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

void CodeEditArea::selectAll() {
    if (!m_doc || m_doc->lineCount() == 0) {
        return;
    }
    m_anchor = {0, 0};
    const int last = m_doc->lineCount() - 1;
    m_cursor = m_cursorCtrl->clamp({last, INT_MAX});
    m_caretPainter->resetBlink();
    viewport()->update();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

void CodeEditArea::clearSelection() {
    if (!hasSelection()) {
        return;
    }
    m_anchor = m_cursor;
    viewport()->update();
    emit selectionChanged();
}

void CodeEditArea::setSelectionColor(const QColor& color) {
    m_selectionColor = color;
    if (hasSelection()) {
        viewport()->update();
    }
}

// --- Undo / redo --------------------------------------------------------

void CodeEditArea::undo() {
    if (!m_undoStack->canUndo()) {
        return;
    }
    m_undoStack->undo();
    updateAfterEdit();
}

void CodeEditArea::redo() {
    if (!m_undoStack->canRedo()) {
        return;
    }
    m_undoStack->redo();
    updateAfterEdit();
}

bool CodeEditArea::canUndo() const { return m_undoStack->canUndo(); }
bool CodeEditArea::canRedo() const { return m_undoStack->canRedo(); }

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

void CodeEditArea::setTabCaptured(bool captured) {
    m_tabCaptured = captured;
}

void CodeEditArea::setReadOnly(bool ro) {
    m_readOnly = ro;
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
            if (line < 0 || line >= m_lineSpans.size()) return nullptr;
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
}

void CodeEditArea::foldAll() {
    m_foldState.foldAll();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::unfoldAll() {
    m_foldState.unfoldAll();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::setWordWrap(bool wrap) {
    if (m_wordWrap == wrap) return;
    m_wordWrap = wrap;
    horizontalScrollBar()->setVisible(!wrap);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
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
    paintSelection(p);
    p.setPen(palette().text().color());
    m_renderer->paint(p, m_doc, m_viewportState);
    int caretVisualCol, caretVisualRow;
    if (m_doc) {
        caretVisualRow = visualRowOf(m_cursor);
        const int rowStart = m_wordWrap
            ? m_wrapLayout->rowAt(caretVisualRow).startCol : 0;
        const QString seg = m_doc->lineAt(m_cursor.line).mid(rowStart);
        caretVisualCol = LineRenderer::visualColumn(seg, m_cursor.column - rowStart, tabWidth());
    } else {
        caretVisualRow = m_cursor.line;
        caretVisualCol = m_cursor.column;
    }
    m_caretPainter->paint(p, m_cursor, caretVisualCol, caretVisualRow, m_viewportState, font());
}

void CodeEditArea::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
}

void CodeEditArea::scrollContentsBy(int dx, int dy) {
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    refreshViewportState();
    viewport()->update();
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
    const CursorController& cc = *m_cursorCtrl;
    const TextCursor c = m_cursor;

    auto move = [&](TextCursor next) {
        if (shift) applySelectionMove(next);
        else       applyCursorMove(next);
    };

    switch (e->key()) {
    // --- Navigation ---
    case Qt::Key_Up:       move(cc.moveUp(c));                              break;
    case Qt::Key_Down:     move(cc.moveDown(c));                            break;
    case Qt::Key_Left:
        move(ctrl ? cc.moveWordLeft(c) : cc.moveLeft(c));
        break;
    case Qt::Key_Right:
        move(ctrl ? cc.moveWordRight(c) : cc.moveRight(c));
        break;
    case Qt::Key_Home:     move(ctrl ? cc.moveToDocumentStart(c)
                                     : cc.moveToLineStart(c));              break;
    case Qt::Key_End:      move(ctrl ? cc.moveToDocumentEnd(c)
                                     : cc.moveToLineEnd(c));                break;
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
        } else if (m_cursor.column > 0) {
            executeRemove({m_cursor.line, m_cursor.column - 1}, m_cursor);
        } else if (m_cursor.line > 0) {
            const int prevLen = m_doc->lineAt(m_cursor.line - 1).size();
            executeRemove({m_cursor.line - 1, prevLen}, m_cursor);
        }
        break;

    case Qt::Key_Delete:
        if (hasSelection()) {
            executeRemoveSelection();
        } else {
            const int lineLen = m_doc->lineAt(m_cursor.line).size();
            if (m_cursor.column < lineLen) {
                executeRemove(m_cursor, {m_cursor.line, m_cursor.column + 1});
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
        if (ctrl) { selectAll(); break; }
        goto handle_printable;

    case Qt::Key_C:
        if (ctrl && hasSelection()) {
            QGuiApplication::clipboard()->setText(selectedText());
            break;
        }
        goto handle_printable;

    case Qt::Key_X:
        if (ctrl && hasSelection()) {
            QGuiApplication::clipboard()->setText(selectedText());
            executeRemoveSelection();
            break;
        }
        goto handle_printable;

    case Qt::Key_V:
        if (ctrl) {
            const QString text = QGuiApplication::clipboard()->text();
            if (!text.isEmpty()) {
                executeInsert(text);
            }
            break;
        }
        goto handle_printable;

    case Qt::Key_Insert:
        m_overwrite = !m_overwrite;
        m_caretPainter->setOverwrite(m_overwrite);
        break;

    case Qt::Key_Minus:
        if (ctrl && !shift) { toggleFoldAt(m_cursor.line); break; }
        goto handle_printable;

    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (ctrl && !shift) { toggleFoldAt(m_cursor.line); break; }
        goto handle_printable;

    case Qt::Key_Z:
        if (ctrl && !shift) { undo(); break; }
        if (ctrl &&  shift) { redo(); break; }
        goto handle_printable;

    case Qt::Key_Y:
        if (ctrl) { redo(); break; }
        goto handle_printable;

    default:
    handle_printable: {
        const QString text = e->text();
        if (!text.isEmpty() && !ctrl && !alt && text.at(0).isPrint()) {
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

void CodeEditArea::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        applySelectionMove(cursorFromPoint(e->pos()));
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(e);
}

void CodeEditArea::mouseReleaseEvent(QMouseEvent* e) {
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void CodeEditArea::focusInEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusInEvent(e);
    m_caretPainter->setFocused(true);
}

void CodeEditArea::focusOutEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusOutEvent(e);
    m_caretPainter->setFocused(false);
}

// ------------------------------------------------------------------------
// Document signal handlers
// ------------------------------------------------------------------------

void CodeEditArea::onDocumentReset() {
    m_cursor = m_cursorCtrl->clamp(TextCursor{});
    m_anchor = m_cursor;
    m_undoStack->clear();
    rebuildHighlightCache();
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

void CodeEditArea::onLinesInserted(int startLine, int count) {
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursorCtrl->clamp(m_anchor);
    if (m_highlighter) {
        for (int i = 0; i < count; ++i) {
            m_lineEndStates.insert(startLine, HighlightState{});
            m_lineSpans.insert(startLine, {});
        }
        rehighlightFrom(startLine);
    }
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::onLinesRemoved(int startLine, int count) {
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursorCtrl->clamp(m_anchor);
    if (m_highlighter) {
        for (int i = 0; i < count && startLine < m_lineEndStates.size(); ++i) {
            m_lineEndStates.removeAt(startLine);
            m_lineSpans.removeAt(startLine);
        }
        rehighlightFrom(startLine);
    }
    rebuildFolds();
    rebuildWrapLayout();
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::onLinesChanged(int startLine, int) {
    if (m_highlighter) {
        rehighlightFrom(startLine);
    }
    rebuildFolds();
    rebuildWrapLayout();
    refreshViewportState();
    viewport()->update();
}

// ------------------------------------------------------------------------
// Private — navigation helpers
// ------------------------------------------------------------------------

void CodeEditArea::refreshViewportState() {
    const QFontMetrics fm(font());
    const int lineHeight = fm.height();
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

    if (m_wordWrap) {
        s.contentOffsetX = 0;
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
    const int lineHeight = fm.height();
    const int charWidth  = fm.horizontalAdvance(QLatin1Char('M'));
    const int vpH = viewport()->height();
    const int vpW = viewport()->width();
    const int visibleLines = (lineHeight > 0) ? (vpH / lineHeight) : 0;

    if (m_wordWrap) {
        const int totalRows = m_wrapLayout->totalRows();
        const int vMax = qMax(0, totalRows - visibleLines);
        verticalScrollBar()->setRange(0, vMax);
        verticalScrollBar()->setPageStep(qMax(1, visibleLines));
        verticalScrollBar()->setSingleStep(1);
        horizontalScrollBar()->setRange(0, 0);
    } else {
        const int lineCount = m_doc ? m_doc->lineCount() : 0;
        const int vMax = qMax(0, lineCount - visibleLines);
        verticalScrollBar()->setRange(0, vMax);
        verticalScrollBar()->setPageStep(qMax(1, visibleLines));
        verticalScrollBar()->setSingleStep(1);

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
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

TextCursor CodeEditArea::cursorFromPoint(const QPoint& pt) const {
    const ViewportState& vp = m_viewportState;
    if (!m_doc || !vp.isValid()) return {};

    const int tw = tabWidth();

    if (m_wordWrap && !vp.rows.isEmpty()) {
        const int ri = qBound(0, pt.y() / vp.lineHeight, vp.rows.size() - 1);
        const ViewportState::RowInfo& row = vp.rows[ri];
        const int targetX = pt.x() - LineRenderer::kLeftPaddingPx;
        const QString seg = m_doc->lineAt(row.logicalLine)
                                .mid(row.startCol, row.endCol - row.startCol);
        int visual = 0, segCol = 0;
        for (; segCol < seg.size(); ++segCol) {
            const int cw = (seg.at(segCol) == QLatin1Char('\t'))
                ? (tw - (visual % tw)) : 1;
            if (targetX * 2 < (2 * visual + cw) * vp.charWidth) break;
            visual += cw;
        }
        return m_cursorCtrl->clamp({row.logicalLine, row.startCol + segCol});
    }

    const int lineNum = qBound(0,
        vp.firstVisibleLine + pt.y() / vp.lineHeight,
        m_doc->lineCount() - 1);
    const int targetX = pt.x() - LineRenderer::kLeftPaddingPx + vp.contentOffsetX;
    const QString lineStr = m_doc->lineAt(lineNum);
    int visual = 0, col = 0;
    for (; col < lineStr.size(); ++col) {
        const int cw = (lineStr.at(col) == QLatin1Char('\t'))
            ? (tw - (visual % tw)) : 1;
        if (targetX * 2 < (2 * visual + cw) * vp.charWidth) break;
        visual += cw;
    }
    return m_cursorCtrl->clamp({lineNum, col});
}

void CodeEditArea::ensureCursorVisible(TextCursor pos) {
    QScrollBar* vBar = verticalScrollBar();

    if (m_wordWrap) {
        const int row   = visualRowOf(pos);
        const int first = m_viewportState.firstVisibleRow;
        const int last  = m_viewportState.lastVisibleRow;
        if (row < first) {
            vBar->setValue(row);
        } else if (row > last) {
            vBar->setValue(qMax(0, row - pageLineCount() + 1));
        }
        return; // no horizontal scroll in wrap mode
    }

    const int first = m_viewportState.firstVisibleLine;
    const int last  = m_viewportState.lastVisibleLine;
    if (pos.line < first) {
        vBar->setValue(pos.line);
    } else if (pos.line > last) {
        vBar->setValue(qMax(0, pos.line - pageLineCount() + 1));
    }

    const int charWidth = m_viewportState.charWidth;
    if (charWidth <= 0) return;
    QScrollBar* hBar = horizontalScrollBar();
    const int firstCol    = hBar->value();
    const int visibleCols = m_viewportState.viewportWidth / charWidth;
    const int lastCol     = firstCol + visibleCols - 1;
    if (pos.column < firstCol) {
        hBar->setValue(pos.column);
    } else if (pos.column > lastCol) {
        hBar->setValue(qMax(0, pos.column - visibleCols + 1));
    }
}

int CodeEditArea::pageLineCount() const {
    const int lh = m_viewportState.lineHeight;
    return (lh <= 0) ? 1 : qMax(1, m_viewportState.viewportHeight / lh);
}

// --- Word-wrap helpers ---------------------------------------------------

void CodeEditArea::rebuildWrapLayout() {
    if (!m_wordWrap || !m_doc) return;
    const QFontMetrics fm(font());
    const int cw = fm.horizontalAdvance(QLatin1Char('M'));
    if (cw <= 0) return;
    const int availCols = qMax(1, (viewport()->width() - LineRenderer::kLeftPaddingPx) / cw);
    const FoldState* fs = (m_foldState.regions().isEmpty()) ? nullptr : &m_foldState;
    m_wrapLayout->rebuild(m_doc, availCols, tabWidth(), fs);
}

int CodeEditArea::visualRowOf(TextCursor pos) const {
    if (!m_wordWrap) return pos.line;
    return m_wrapLayout->rowForCursor(pos.line, pos.column);
}

// --- Highlighting helpers ------------------------------------------------

void CodeEditArea::rebuildHighlightCache() {
    m_lineEndStates.clear();
    m_lineSpans.clear();
    if (!m_highlighter || !m_doc) return;
    const int n = m_doc->lineCount();
    m_lineEndStates.resize(n);
    m_lineSpans.resize(n);
    // Default-constructed HighlightState has an empty stack; rehighlightFrom's
    // stability check cannot stop early while the cached value is still empty.
    rehighlightFrom(0);
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

    HighlightState stateIn = (startLine == 0)
        ? m_highlighter->initialState()
        : m_lineEndStates[startLine - 1];

    for (int i = startLine; i < n; ++i) {
        const HighlightState oldEndState = m_lineEndStates[i];
        HighlightState stateOut;
        m_highlighter->highlightLine(m_doc->lineAt(i), stateIn,
                                      m_lineSpans[i], stateOut);
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
        m_foldState.setRegions({});
        return;
    }
    m_foldState.setRegions(m_foldingProvider->computeRegions(m_doc));
}

// ------------------------------------------------------------------------
// Private — edit helpers
// ------------------------------------------------------------------------

void CodeEditArea::executeInsert(const QString& text) {
    if (!m_doc || m_readOnly) {
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
        return;
    }
    m_undoStack->push(new RemoveCommand(
        m_doc, start, end, m_cursor, &m_cursor, &m_anchor));
    updateAfterEdit();
}

void CodeEditArea::executeRemoveSelection() {
    if (!m_doc || m_readOnly || !hasSelection()) {
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
    m_caretPainter->resetBlink();
    ensureCursorVisible(m_cursor);
    updateScrollBarRanges();
    viewport()->update();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
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
    QRegion region;

    if (m_wordWrap && !vp.rows.isEmpty()) {
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
                             vp.viewportWidth / vp.charWidth + 1);
            }

            const int topY = vp.contentOffsetY + ri * vp.lineHeight;
            const int x = LineRenderer::kLeftPaddingPx + vcStart * vp.charWidth;
            const int w = (vcEnd - vcStart) * vp.charWidth;
            if (w > 0) region += QRect(x, topY, w, vp.lineHeight);
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
        if (w > 0) region += QRect(x, topY, w, vp.lineHeight);
    }
    return region;
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
