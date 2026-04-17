#include "LineRenderer.h"

#include <qce/ITextDocument.h>
#include <qce/ViewportState.h>

#include <QFontMetrics>
#include <QPainter>

namespace qce {

void LineRenderer::paint(QPainter& painter,
                         const ITextDocument* doc,
                         const ViewportState& vp) const {
    if (!doc || !vp.isValid()) {
        return;
    }
    const int lineCount = doc->lineCount();
    if (lineCount == 0 || vp.lastVisibleLine < vp.firstVisibleLine) {
        return;
    }

    painter.setFont(m_font);

    const QFontMetrics fm(m_font);
    const int ascent = fm.ascent();
    const int lineHeight = vp.lineHeight;

    // X position of the first character after padding, then shifted left by
    // the horizontal scroll offset. Lines that are shorter than the offset
    // will draw off-screen and simply be clipped by the painter.
    const int baseX = kLeftPaddingPx - vp.contentOffsetX;

    const int first = vp.firstVisibleLine;
    const int last = vp.lastVisibleLine;

    for (int i = first; i <= last && i < lineCount; ++i) {
        // Baseline Y: top of line + offsetY + ascent.
        // offsetY is <= 0 when the first line is partially scrolled off;
        // for subsequent lines we add (i - first) * lineHeight.
        const int topY = vp.contentOffsetY + (i - first) * lineHeight;
        const int baselineY = topY + ascent;

        const QString text = expandTabs(doc->lineAt(i));
        if (text.isEmpty()) {
            continue;
        }
        painter.drawText(baseX, baselineY, text);
    }
}

QString LineRenderer::expandTabs(const QString& line) const {
    if (!line.contains(QLatin1Char('\t'))) {
        return line;
    }
    const QString spaces(m_tabWidth, QLatin1Char(' '));
    QString out = line;
    out.replace(QLatin1Char('\t'), spaces);
    return out;
}

} // namespace qce
