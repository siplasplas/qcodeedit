#include "CaretPainter.h"

#include "LineRenderer.h"
#include <qce/TextCursor.h>
#include <qce/ViewportState.h>

#include <QFontMetrics>
#include <QPainter>
#include <QTimer>

namespace qce {

CaretPainter::CaretPainter(QObject* parent)
    : QObject(parent), m_timer(new QTimer(this)) {
    m_timer->setInterval(m_blinkInterval);
    connect(m_timer, &QTimer::timeout, this, &CaretPainter::onTimerTick);
}

void CaretPainter::setBlinkInterval(int ms) {
    m_blinkInterval = ms > 0 ? ms : 500;
    m_timer->setInterval(m_blinkInterval);
}

void CaretPainter::setFocused(bool focused) {
    m_focused = focused;
    if (focused) {
        m_shown = true;
        m_timer->start();
    } else {
        m_timer->stop();
        m_shown = false;
    }
    emit blinkToggled();
}

void CaretPainter::resetBlink() {
    if (!m_focused) {
        return;
    }
    m_shown = true;
    m_timer->start(); // restarts the interval from zero
    emit blinkToggled();
}

void CaretPainter::onTimerTick() {
    m_shown = !m_shown;
    emit blinkToggled();
}

void CaretPainter::paint(QPainter& painter,
                          const TextCursor& cursor,
                          const ViewportState& vp,
                          const QFont& font) const {
    if (!m_focused || !m_shown || !vp.isValid()) {
        return;
    }
    if (cursor.line < vp.firstVisibleLine || cursor.line > vp.lastVisibleLine) {
        return;
    }

    const int x = LineRenderer::kLeftPaddingPx
                  + cursor.column * vp.charWidth
                  - vp.contentOffsetX;
    const int topY = vp.contentOffsetY
                     + (cursor.line - vp.firstVisibleLine) * vp.lineHeight;

    const QFontMetrics fm(font);
    const int caretHeight = fm.ascent() + fm.descent();

    painter.save();
    painter.setPen(painter.pen().color());
    painter.drawLine(x, topY, x, topY + caretHeight - 1);
    painter.restore();
}

} // namespace qce
