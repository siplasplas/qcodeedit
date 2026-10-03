#include <qce/margins/FoldingGutter.h>

#include <qce/FoldState.h>
#include <qce/ViewportState.h>

#include <QFontMetrics>
#include <QPainter>
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

    const QFontMetrics fm = painter.fontMetrics();
    QColor c = painter.pen().color();
    c.setAlphaF(0.55);
    painter.save();
    painter.setPen(c);

    for (int i = 0; i < vp.rows.size(); ++i) {
        const auto& row = vp.rows[i];
        if (!row.isFirstRow) continue;
        const int regIdx = m_state->regionStartingAt(row.logicalLine);
        if (regIdx < 0) continue;

        const int topY = marginRect.top() + vp.contentOffsetY + i * vp.lineHeight;
        const bool collapsed = m_state->isCollapsed(regIdx);
        const QString glyph = collapsed
            ? QStringLiteral("\u25B8")   // ▸
            : QStringLiteral("\u25BE");  // ▾
        const int glyphW = fm.horizontalAdvance(glyph);
        const int x = marginRect.left() + (marginRect.width() - glyphW) / 2;
        painter.drawText(x, topY + fm.ascent(), glyph);
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
