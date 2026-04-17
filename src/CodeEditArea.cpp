#include "qce/CodeEditArea.h"

#include "qce/ITextDocument.h"
#include "CaretPainter.h"
#include "CursorController.h"
#include "LineRenderer.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>

namespace qce {

// ------------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------------

CodeEditArea::CodeEditArea(QWidget* parent)
    : QAbstractScrollArea(parent),
      m_renderer(std::make_unique<LineRenderer>()),
      m_cursorCtrl(std::make_unique<CursorController>(nullptr)),
      m_caretPainter(std::make_unique<CaretPainter>(this)) {
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

    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::setCursorPosition(TextCursor pos) {
    applyCursorMove(pos);
}

// --- Selection ---

TextCursor CodeEditArea::selectionStart() const {
    if (m_anchor.line < m_cursor.line ||
        (m_anchor.line == m_cursor.line && m_anchor.column <= m_cursor.column)) {
        return m_anchor;
    }
    return m_cursor;
}

TextCursor CodeEditArea::selectionEnd() const {
    if (m_anchor.line < m_cursor.line ||
        (m_anchor.line == m_cursor.line && m_anchor.column <= m_cursor.column)) {
        return m_cursor;
    }
    return m_anchor;
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

// --- Configuration ---

void CodeEditArea::setTabWidth(int spaces) {
    m_renderer->setTabWidth(spaces);
    viewport()->update();
}

int CodeEditArea::tabWidth() const {
    return m_renderer->tabWidth();
}

void CodeEditArea::setSelectionColor(const QColor& color) {
    m_selectionColor = color;
    if (hasSelection()) {
        viewport()->update();
    }
}

void CodeEditArea::setInvertSelection(bool invert) {
    m_invertSelection = invert;
    if (hasSelection()) {
        viewport()->update();
    }
}

void CodeEditArea::setSelectionForeground(const QColor& color) {
    m_selectionForeground = color;
    if (hasSelection() && m_invertSelection) {
        viewport()->update();
    }
}

void CodeEditArea::setCaretBlinkInterval(int ms) {
    m_caretPainter->setBlinkInterval(ms);
}

int CodeEditArea::caretBlinkInterval() const {
    return m_caretPainter->blinkInterval();
}

// ------------------------------------------------------------------------
// QWidget / QAbstractScrollArea overrides
// ------------------------------------------------------------------------

void CodeEditArea::paintEvent(QPaintEvent* e) {
    QPainter p(viewport());
    p.fillRect(e->rect(), palette().base());
    paintSelection(p);
    p.setPen(palette().text().color());
    m_renderer->paint(p, m_doc, m_viewportState);
    if (m_invertSelection && hasSelection()) {
        QFont boldFont = m_renderer->font();
        boldFont.setBold(true);
        m_renderer->setFont(boldFont);

        p.save();
        p.setClipRegion(selectionRegion());
        p.setPen(m_selectionForeground);
        m_renderer->paint(p, m_doc, m_viewportState);
        p.restore();

        m_renderer->setFont(font()); // restore original
    }
    m_caretPainter->paint(p, m_cursor, m_viewportState, font());
}

void CodeEditArea::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBarRanges();
    refreshViewportState();
}

void CodeEditArea::scrollContentsBy(int dx, int dy) {
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::keyPressEvent(QKeyEvent* e) {
    if (!m_doc) {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    const bool ctrl  = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const CursorController& cc = *m_cursorCtrl;
    const TextCursor c = m_cursor;

    auto move = [&](TextCursor next) {
        if (shift) applySelectionMove(next);
        else       applyCursorMove(next);
    };

    switch (e->key()) {
    case Qt::Key_Up:       move(cc.moveUp(c));                              break;
    case Qt::Key_Down:     move(cc.moveDown(c));                            break;
    case Qt::Key_Left:     move(cc.moveLeft(c));                            break;
    case Qt::Key_Right:    move(cc.moveRight(c));                           break;
    case Qt::Key_Home:     move(ctrl ? cc.moveToDocumentStart(c)
                                     : cc.moveToLineStart(c));              break;
    case Qt::Key_End:      move(ctrl ? cc.moveToDocumentEnd(c)
                                     : cc.moveToLineEnd(c));                break;
    case Qt::Key_PageUp:   move(cc.movePageUp(c, pageLineCount()));         break;
    case Qt::Key_PageDown: move(cc.movePageDown(c, pageLineCount()));       break;
    case Qt::Key_A:
        if (ctrl) { selectAll(); break; }
        QAbstractScrollArea::keyPressEvent(e);
        return;
    case Qt::Key_C:
        if (ctrl && hasSelection()) {
            QGuiApplication::clipboard()->setText(selectedText());
            break;
        }
        QAbstractScrollArea::keyPressEvent(e);
        return;
    default:
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    e->accept();
}

void CodeEditArea::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
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
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit cursorPositionChanged(m_cursor);
    emit selectionChanged();
}

void CodeEditArea::onLinesInserted(int, int) {
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursorCtrl->clamp(m_anchor);
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::onLinesRemoved(int, int) {
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    m_anchor = m_cursorCtrl->clamp(m_anchor);
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::onLinesChanged(int, int) {
    refreshViewportState();
    viewport()->update();
}

// ------------------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------------------

void CodeEditArea::refreshViewportState() {
    const QFontMetrics fm(font());
    const int lineHeight = fm.height();
    const int charWidth = fm.horizontalAdvance(QLatin1Char('M'));
    const int vpW = viewport()->width();
    const int vpH = viewport()->height();
    const int lineCount = m_doc ? m_doc->lineCount() : 0;

    ViewportState s;
    s.lineHeight = lineHeight;
    s.charWidth = charWidth;
    s.viewportWidth = vpW;
    s.viewportHeight = vpH;
    s.contentOffsetX = (charWidth > 0)
        ? horizontalScrollBar()->value() * charWidth
        : 0;

    if (lineHeight > 0 && lineCount > 0) {
        const int scrollY = verticalScrollBar()->value();
        s.firstVisibleLine = qMin(scrollY, lineCount - 1);
        s.contentOffsetY = 0;
        const int maxVisible = (vpH + lineHeight - 1) / lineHeight;
        s.lastVisibleLine = qMin(s.firstVisibleLine + maxVisible - 1,
                                 lineCount - 1);
    } else {
        s.firstVisibleLine = 0;
        s.lastVisibleLine = -1;
        s.contentOffsetY = 0;
    }

    m_viewportState = s;
    emit viewportChanged(m_viewportState);
}

void CodeEditArea::updateScrollBarRanges() {
    const int lineCount = m_doc ? m_doc->lineCount() : 0;
    const QFontMetrics fm(font());
    const int lineHeight = fm.height();
    const int charWidth = fm.horizontalAdvance(QLatin1Char('M'));
    const int vpH = viewport()->height();
    const int vpW = viewport()->width();
    const int visibleLines = (lineHeight > 0) ? (vpH / lineHeight) : 0;
    const int visibleCols  = (charWidth  > 0) ? (vpW / charWidth)  : 0;

    const int vMax = qMax(0, lineCount - visibleLines);
    verticalScrollBar()->setRange(0, vMax);
    verticalScrollBar()->setPageStep(qMax(1, visibleLines));
    verticalScrollBar()->setSingleStep(1);

    const int maxCols = m_doc ? m_doc->maxLineLength() : 0;
    const int hMax = qMax(0, maxCols - visibleCols);
    horizontalScrollBar()->setRange(0, hMax);
    horizontalScrollBar()->setPageStep(qMax(1, visibleCols));
    horizontalScrollBar()->setSingleStep(1);
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
    if (!m_doc || !vp.isValid()) {
        return {};
    }
    const int line = vp.firstVisibleLine + pt.y() / vp.lineHeight;
    // Round to nearest column by adding half a charWidth before dividing.
    const int col = qMax(0, (pt.x() - LineRenderer::kLeftPaddingPx
                              + vp.contentOffsetX + vp.charWidth / 2)
                             / vp.charWidth);
    return m_cursorCtrl->clamp({line, col});
}

QRegion CodeEditArea::selectionRegion() const {
    if (!hasSelection() || !m_doc || !m_viewportState.isValid()) {
        return {};
    }
    const TextCursor s = selectionStart();
    const TextCursor e = selectionEnd();
    const ViewportState& vp = m_viewportState;

    const int first = qMax(s.line, vp.firstVisibleLine);
    const int last  = qMin(e.line, vp.lastVisibleLine);

    QRegion region;
    for (int i = first; i <= last; ++i) {
        const int topY = vp.contentOffsetY + (i - vp.firstVisibleLine) * vp.lineHeight;

        int startCol = (i == s.line) ? s.column : 0;
        int endCol;
        if (i == e.line) {
            endCol = e.column;
        } else {
            const int lineLen = m_doc->lineAt(i).length();
            endCol = qMax(lineLen, vp.viewportWidth / vp.charWidth + 1);
        }

        const int x = LineRenderer::kLeftPaddingPx
                      + startCol * vp.charWidth
                      - vp.contentOffsetX;
        const int w = (endCol - startCol) * vp.charWidth;
        if (w > 0) {
            region += QRect(x, topY, w, vp.lineHeight);
        }
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

void CodeEditArea::ensureCursorVisible(TextCursor pos) {
    const int first = m_viewportState.firstVisibleLine;
    const int last  = m_viewportState.lastVisibleLine;
    QScrollBar* vBar = verticalScrollBar();

    if (pos.line < first) {
        vBar->setValue(pos.line);
    } else if (pos.line > last) {
        vBar->setValue(qMax(0, pos.line - pageLineCount() + 1));
    }

    const int charWidth = m_viewportState.charWidth;
    if (charWidth <= 0) {
        return;
    }
    QScrollBar* hBar = horizontalScrollBar();
    const int firstCol   = hBar->value();
    const int visibleCols = m_viewportState.viewportWidth / charWidth;
    const int lastCol    = firstCol + visibleCols - 1;

    if (pos.column < firstCol) {
        hBar->setValue(pos.column);
    } else if (pos.column > lastCol) {
        hBar->setValue(qMax(0, pos.column - visibleCols + 1));
    }
}

int CodeEditArea::pageLineCount() const {
    const int lh = m_viewportState.lineHeight;
    if (lh <= 0) {
        return 1;
    }
    return qMax(1, m_viewportState.viewportHeight / lh);
}

} // namespace qce
