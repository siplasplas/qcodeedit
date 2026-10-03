#include <qce/margins/FoldingGutter.h>

#include <qce/FoldState.h>
#include <qce/ViewportState.h>

#include <QPainter>
#include <QPolygonF>
#include <QRect>

namespace qce {

FoldingGutter::FoldingGutter(const FoldState* state, ToggleCallback toggle)
    : m_state(state), m_toggle(std::move(toggle)) {}

int FoldingGutter::preferredWidth(const ViewportState& vp) const {
    if (vp.charWidth <= 0) return 14;
    return vp.charWidth + 4;
}

void FoldingGutter::paint(QPainter& painter,
                           const ViewportState& vp,
                           const QRect& marginRect) {
    if (!m_state || !vp.isValid() || vp.rows.isEmpty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#A8ADB8"), 1.2, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));

    const qreal size = qMax(0.0, qMin(9.0, qMin(marginRect.width() - 4.0,
                                              vp.lineHeight * 0.45)));
    const qreal centerX = marginRect.left() + marginRect.width() / 2.0;

    for (int i = 0; i < vp.rows.size(); ++i) {
        const auto& row = vp.rows[i];
        if (!row.isFirstRow) continue;
        const int regIdx = m_state->regionStartingAt(row.logicalLine);
        if (regIdx < 0) continue;

        const int topY = marginRect.top() + vp.contentOffsetY + i * vp.lineHeight;
        const bool collapsed = m_state->isCollapsed(regIdx);
        if (!collapsed && !m_hovered) continue;
        const qreal centerY = topY + vp.lineHeight / 2.0 - size /2;
        const QPolygonF chevron = collapsed
            ? QPolygonF{{centerX - size / 4, centerY - size / 2},
                        {centerX + size / 4, centerY},
                        {centerX - size / 4, centerY + size / 2}}
            : QPolygonF{{centerX - size / 2, centerY - size / 4},
                        {centerX, centerY + size / 4},
                        {centerX + size / 2, centerY - size / 4}};
        painter.drawPolyline(chevron);
    }
    painter.restore();
}

void FoldingGutter::mousePressed(const QPoint& local,
                                  const ViewportState& vp,
                                  const QRect& marginRect) {
    if (!m_state || !vp.isValid() || vp.rows.isEmpty() || !m_toggle) return;
    if (vp.lineHeight <= 0) return;

    const int yInside = local.y() - marginRect.top() - vp.contentOffsetY;
    if (yInside < 0) return;
    const int rowIdx = yInside / vp.lineHeight;
    if (rowIdx < 0 || rowIdx >= vp.rows.size()) return;

    const auto& row = vp.rows[rowIdx];
    if (!row.isFirstRow) return;
    if (m_state->regionStartingAt(row.logicalLine) < 0) return;
    m_toggle(row.logicalLine);
}

} // namespace qce
