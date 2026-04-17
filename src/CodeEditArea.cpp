#include "qce/CodeEditArea.h"

#include "qce/ITextDocument.h"
#include "CaretPainter.h"
#include "CursorController.h"
#include "LineRenderer.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
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
    // Mono-font default.
    QFont f(QStringLiteral("Monospace"));
    f.setStyleHint(QFont::TypeWriter);
    setFont(f);
    m_renderer->setFont(f);

    // We paint the full background ourselves in paintEvent.
    viewport()->setAutoFillBackground(false);

    // Accept focus so we receive keyPressEvent.
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

    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::setCursorPosition(TextCursor pos) {
    applyCursorMove(pos);
}

void CodeEditArea::setTabWidth(int spaces) {
    m_renderer->setTabWidth(spaces);
    viewport()->update();
}

int CodeEditArea::tabWidth() const {
    return m_renderer->tabWidth();
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
    p.setPen(palette().text().color());
    m_renderer->paint(p, m_doc, m_viewportState);
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
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const CursorController& cc = *m_cursorCtrl;
    TextCursor c = m_cursor;

    switch (e->key()) {
    case Qt::Key_Up:
        applyCursorMove(cc.moveUp(c));
        break;
    case Qt::Key_Down:
        applyCursorMove(cc.moveDown(c));
        break;
    case Qt::Key_Left:
        applyCursorMove(cc.moveLeft(c));
        break;
    case Qt::Key_Right:
        applyCursorMove(cc.moveRight(c));
        break;
    case Qt::Key_Home:
        applyCursorMove(ctrl ? cc.moveToDocumentStart(c)
                             : cc.moveToLineStart(c));
        break;
    case Qt::Key_End:
        applyCursorMove(ctrl ? cc.moveToDocumentEnd(c)
                             : cc.moveToLineEnd(c));
        break;
    case Qt::Key_PageUp:
        applyCursorMove(cc.movePageUp(c, pageLineCount()));
        break;
    case Qt::Key_PageDown:
        applyCursorMove(cc.movePageDown(c, pageLineCount()));
        break;
    default:
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    e->accept();
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
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
    emit cursorPositionChanged(m_cursor);
}

void CodeEditArea::onLinesInserted(int, int) {
    m_cursor = m_cursorCtrl->clamp(m_cursor);
    updateScrollBarRanges();
    refreshViewportState();
    viewport()->update();
}

void CodeEditArea::onLinesRemoved(int, int) {
    m_cursor = m_cursorCtrl->clamp(m_cursor);
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
        s.contentOffsetY = 0; // line-granularity scroll in v0.2
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
    const int visibleCols = (charWidth > 0) ? (vpW / charWidth) : 0;

    // Vertical: range measured in lines (step == 1 line in v0.2).
    const int vMax = qMax(0, lineCount - visibleLines);
    verticalScrollBar()->setRange(0, vMax);
    verticalScrollBar()->setPageStep(qMax(1, visibleLines));
    verticalScrollBar()->setSingleStep(1);

    // Horizontal: range measured in character widths.
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
    if (clamped == m_cursor) {
        return;
    }
    m_cursor = clamped;
    m_caretPainter->resetBlink();
    ensureCursorVisible(m_cursor);
    viewport()->update();
    emit cursorPositionChanged(m_cursor);
}

void CodeEditArea::ensureCursorVisible(TextCursor pos) {
    // Vertical: scroll if the line is outside [first, last] visible range.
    const int first = m_viewportState.firstVisibleLine;
    const int last = m_viewportState.lastVisibleLine;
    QScrollBar* vBar = verticalScrollBar();

    if (pos.line < first) {
        vBar->setValue(pos.line);
    } else if (pos.line > last) {
        const int pageLines = pageLineCount();
        vBar->setValue(qMax(0, pos.line - pageLines + 1));
    }

    // Horizontal: scroll if the column is outside the visible horizontal
    // range. "Column" is a QChar index here; approximates pixel extent by
    // multiplying by charWidth (mono-font).
    const int charWidth = m_viewportState.charWidth;
    if (charWidth <= 0) {
        return;
    }
    QScrollBar* hBar = horizontalScrollBar();
    const int firstCol = hBar->value();
    const int visibleCols = m_viewportState.viewportWidth / charWidth;
    const int lastCol = firstCol + visibleCols - 1;

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
