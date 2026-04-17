#include <qce/Rail.h>

#include <qce/CodeEditArea.h>
#include <qce/IMargin.h>

#include <QPainter>
#include <QPaintEvent>

namespace qce {

Rail::Rail(QWidget* parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(false);
}

void Rail::addMargin(IMargin* margin) {
    if (margin && !m_margins.contains(margin)) {
        m_margins.append(margin);
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

void Rail::onViewportChanged(const ViewportState& vp) {
    m_vp = vp;
    updateGeometry();
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
