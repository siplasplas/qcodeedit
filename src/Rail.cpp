#include <qce/Rail.h>

#include <qce/CodeEditArea.h>
#include <qce/IMargin.h>

#include <QMouseEvent>
#include <QCursor>
#include <QEnterEvent>
#include <QPainter>
#include <QPaintEvent>

namespace qce {

Rail::Rail(QWidget* parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    setMouseTracking(true);
}

void Rail::addMargin(IMargin* margin) {
    if (margin && !m_margins.contains(margin)) {
        m_margins.append(margin);
        updateGeometry();
        update();
    }
}

void Rail::removeMargin(IMargin* margin) {
    if (m_margins.removeOne(margin)) {
        if (m_hoveredMargin == margin) {
            margin->hoverChanged(false);
            m_hoveredMargin = nullptr;
        }
        updateGeometry();
        update();
    }
}

void Rail::connectToArea(CodeEditArea* area) {
    connect(area, &CodeEditArea::viewportChanged,
            this, &Rail::onViewportChanged);
}

QSize Rail::sizeHint() const {
    return QSize(totalWidth(), 0);
}

void Rail::paintEvent(QPaintEvent*) {
    if (!m_vp.isValid()) {
        return;
    }
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setPen(palette().text().color());

    int x = 0;
    for (IMargin* m : m_margins) {
        const int w = m->preferredWidth(m_vp);
        m->paint(painter, m_vp, QRect(x, 0, w, height()));
        x += w;
    }
}

void Rail::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_vp.isValid()) {
        QWidget::mousePressEvent(e);
        return;
    }
    int x = 0;
    const QPoint pt = e->pos();
    for (IMargin* m : m_margins) {
        const int w = m->preferredWidth(m_vp);
        if (pt.x() >= x && pt.x() < x + w) {
            m->mousePressed(pt, m_vp, QRect(x, 0, w, height()));
            e->accept();
            return;
        }
        x += w;
    }
    QWidget::mousePressEvent(e);
}

void Rail::onViewportChanged(const ViewportState& vp) {
    m_vp = vp;
    if (underMouse()) updateHoveredMargin(mapFromGlobal(QCursor::pos()));
    updateGeometry();
    update();
}

void Rail::mouseMoveEvent(QMouseEvent* e) {
    updateHoveredMargin(e->pos());
    QWidget::mouseMoveEvent(e);
}

void Rail::enterEvent(QEnterEvent* e) {
    updateHoveredMargin(e->position().toPoint());
    QWidget::enterEvent(e);
}

void Rail::leaveEvent(QEvent* e) {
    updateHoveredMargin(QPoint(-1, -1));
    QWidget::leaveEvent(e);
}

void Rail::updateHoveredMargin(const QPoint& pos) {
    IMargin* hovered = nullptr;
    if (m_vp.isValid() && rect().contains(pos)) {
        int x = 0;
        for (IMargin* margin : m_margins) {
            const int width = margin->preferredWidth(m_vp);
            if (pos.x() >= x && pos.x() < x + width) {
                hovered = margin;
                break;
            }
            x += width;
        }
    }
    if (hovered == m_hoveredMargin) return;
    if (m_hoveredMargin) m_hoveredMargin->hoverChanged(false);
    m_hoveredMargin = hovered;
    if (m_hoveredMargin) m_hoveredMargin->hoverChanged(true);
    update();
}

int Rail::totalWidth() const {
    if (!m_vp.isValid()) {
        return 0;
    }
    int w = 0;
    for (const IMargin* m : m_margins) {
        w += m->preferredWidth(m_vp);
    }
    return w;
}

} // namespace qce
