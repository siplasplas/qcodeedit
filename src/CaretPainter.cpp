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

void CaretPainter::setOverwrite(bool overwrite) {
    m_overwrite = overwrite;
    emit blinkToggled();
}

void CaretPainter::onTimerTick() {
    m_shown = !m_shown;
    emit blinkToggled();
}

void CaretPainter::paint(QPainter& painter,
                          const TextCursor& cursor,
                          int visualCol,
                          int visualRow,
                          const ViewportState& vp,
                          const QFont& font) const {
    if (!m_focused || !m_shown || !vp.isValid()) {
        return;
    }
    if (visualRow < vp.firstVisibleRow || visualRow > vp.lastVisibleRow) {
        return;
    }

    const int x = LineRenderer::kLeftPaddingPx
                  + visualCol * vp.charWidth
                  - vp.contentOffsetX;
    const int topY = vp.contentOffsetY
                     + (visualRow - vp.firstVisibleRow) * vp.lineHeight;

    const QFontMetrics fm(font);
    const int caretHeight = fm.ascent() + fm.descent();

    painter.save();
    if (m_overwrite) {
        painter.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
        painter.fillRect(x, topY, vp.charWidth, caretHeight, Qt::white);
    } else {
        QPen pen(painter.pen().color());
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawLine(x, topY, x, topY + caretHeight - 1);
    }
    painter.restore();
}

} // namespace qce
