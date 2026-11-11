#include <qce/Rail.h>

#include <qce/CodeEditArea.h>
#include <qce/IMargin.h>

#include <QMouseEvent>
#include <QCursor>
#include <QEnterEvent>
#include <QPainter>
#include <QPaintEvent>

namespace qce {

namespace {

QColor mix(const QColor& from, const QColor& to, double t) {
    return QColor::fromRgbF(float(from.redF() + (to.redF() - from.redF()) * t),
                            float(from.greenF() + (to.greenF() - from.greenF()) * t),
                            float(from.blueF() + (to.blueF() - from.blueF()) * t));
}

} // namespace

struct Rail::Private {
    QList<IMargin*> m_margins;
    CodeEditArea* m_area = nullptr;
    GutterColors m_colors;
    Qt::Edge m_textEdge;
    ViewportState m_vp;
    IMargin* m_hoveredMargin = nullptr;
};

Rail::Rail(QWidget* parent, Qt::Edge textEdge)
    :
    QWidget(parent),
      d(std::make_unique<Private>()) {
    d->m_textEdge = textEdge;
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    setMouseTracking(true);
}

void Rail::setColors(const GutterColors& colors) {
    d->m_colors = colors;
    update();
}

GutterColors Rail::effectiveColors() const {
    const QPalette& pal = d->m_area ? d->m_area->palette() : palette();
    const QColor base = pal.base().color();
    const QColor text = pal.text().color();
    // Fractions picked so that a white/black palette gives Kate's Breeze
    // Light gutter: #f0f0f0 strip, #a0a0a0 numbers, #d5d5d5 separator.
    GutterColors c = d->m_colors;
    if (!c.background.isValid()) c.background = mix(base, text, 0.06);
    if (!c.foreground.isValid()) c.foreground = mix(base, text, 0.37);
    if (!c.separator.isValid())  c.separator  = mix(base, text, 0.165);
    return c;
}

void Rail::addMargin(IMargin* margin) {
    if (margin && !d->m_margins.contains(margin)) {
        d->m_margins.append(margin);
        updateGeometry();
        update();
    }
}

void Rail::removeMargin(IMargin* margin) {
    if (d->m_margins.removeOne(margin)) {
        if (d->m_hoveredMargin == margin) {
            margin->hoverChanged(false);
            d->m_hoveredMargin = nullptr;
        }
        updateGeometry();
        update();
    }
}

void Rail::connectToArea(CodeEditArea* area) {
    d->m_area = area;
    connect(area, &CodeEditArea::viewportChanged,
            this, &Rail::onViewportChanged);
    // Derived colours follow the area's palette (e.g. a theme change).
    area->installEventFilter(this);
}

bool Rail::eventFilter(QObject* watched, QEvent* e) {
    if (watched == d->m_area && e->type() == QEvent::PaletteChange) update();
    return QWidget::eventFilter(watched, e);
}

QSize Rail::sizeHint() const {
    return QSize(totalWidth(), 0);
}

void Rail::paintEvent(QPaintEvent*) {
    if (!d->m_vp.isValid()) {
        return;
    }
    const GutterColors colors = effectiveColors();
    QPainter painter(this);
    painter.fillRect(rect(), colors.background);

    int x = 0;
    for (IMargin* m : d->m_margins) {
        const int w = m->preferredWidth(d->m_vp);
        painter.setPen(colors.foreground);
        m->paint(painter, d->m_vp, QRect(x, 0, w, height()));
        x += w;
    }

    if (x > 0) {
        painter.setPen(colors.separator);
        const int sx = (d->m_textEdge == Qt::LeftEdge) ? 0 : width() - 1;
        painter.drawLine(sx, 0, sx, height() - 1);
    }
}

void Rail::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !d->m_vp.isValid()) {
        QWidget::mousePressEvent(e);
        return;
    }
    int x = 0;
    const QPoint pt = e->pos();
    for (IMargin* m : d->m_margins) {
        const int w = m->preferredWidth(d->m_vp);
        if (pt.x() >= x && pt.x() < x + w) {
            m->mousePressed(pt, d->m_vp, QRect(x, 0, w, height()));
            e->accept();
            return;
        }
        x += w;
    }
    QWidget::mousePressEvent(e);
}

void Rail::onViewportChanged(const ViewportState& vp) {
    d->m_vp = vp;
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
    if (d->m_vp.isValid() && rect().contains(pos)) {
        int x = 0;
        for (IMargin* margin : d->m_margins) {
            const int width = margin->preferredWidth(d->m_vp);
            if (pos.x() >= x && pos.x() < x + width) {
                hovered = margin;
                break;
            }
            x += width;
        }
    }
    if (hovered == d->m_hoveredMargin) return;
    if (d->m_hoveredMargin) d->m_hoveredMargin->hoverChanged(false);
    d->m_hoveredMargin = hovered;
    if (d->m_hoveredMargin) d->m_hoveredMargin->hoverChanged(true);
    update();
}

int Rail::totalWidth() const {
    if (!d->m_vp.isValid()) {
        return 0;
    }
    int w = 0;
    for (const IMargin* m : d->m_margins) {
        w += m->preferredWidth(d->m_vp);
    }
    return w;
}


Rail::~Rail() = default;
const GutterColors& Rail::colors() const { return d->m_colors; }

} // namespace qce
