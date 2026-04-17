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

    if (vp.wordWrap && !vp.rows.isEmpty()) {
        // Word-wrap mode: each entry in vp.rows is one visual row.
        for (int ri = 0; ri < vp.rows.size(); ++ri) {
            const auto& row = vp.rows[ri];
            const int topY = vp.contentOffsetY + ri * lineHeight;
            const int baselineY = topY + ascent;

            const QString& line = doc->lineAt(row.logicalLine);
            const QString seg = line.mid(row.startCol, row.endCol - row.startCol);
            const QString text = expandTabs(seg);
            if (!text.isEmpty()) {
                painter.drawText(kLeftPaddingPx, baselineY, text);
            }
        }
        return;
    }

    const int first = vp.firstVisibleLine;
    const int last = vp.lastVisibleLine;

    for (int i = first; i <= last && i < lineCount; ++i) {
        const int topY = vp.contentOffsetY + (i - first) * lineHeight;
        const int baselineY = topY + ascent;

        const QString text = expandTabs(doc->lineAt(i));
        if (text.isEmpty()) {
            continue;
        }
        painter.drawText(baseX, baselineY, text);
    }
}

int LineRenderer::visualColumn(const QString& line, int charIndex, int tabWidth) {
    int visual = 0;
    const int limit = qMin(charIndex, (int)line.size());
    for (int i = 0; i < limit; ++i) {
        if (line.at(i) == QLatin1Char('\t')) {
            visual = (visual / tabWidth + 1) * tabWidth;
        } else {
            ++visual;
        }
    }
    return visual;
}

QString LineRenderer::expandTabs(const QString& line) const {
    if (!line.contains(QLatin1Char('\t'))) {
        return line;
    }
    QString out;
    out.reserve(line.size() + 16);
    int visual = 0;
    for (QChar ch : line) {
        if (ch == QLatin1Char('\t')) {
            const int spaces = m_tabWidth - (visual % m_tabWidth);
            out.append(QString(spaces, QLatin1Char(' ')));
            visual += spaces;
        } else {
            out.append(ch);
            ++visual;
        }
    }
    return out;
}

} // namespace qce
