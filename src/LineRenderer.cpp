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
        for (int ri = 0; ri < vp.rows.size(); ++ri) {
            const auto& row = vp.rows[ri];
            const int topY = vp.contentOffsetY + ri * lineHeight;
            const int baselineY = topY + ascent;
            const QString& line = doc->lineAt(row.logicalLine);
            const QVector<StyleSpan>* spans = m_spansProvider
                ? m_spansProvider(row.logicalLine) : nullptr;
            drawSegmentWithSpans(painter, line, row.startCol, row.endCol,
                                 kLeftPaddingPx, baselineY, vp.charWidth, spans);
            if (m_showWhitespace) {
                const QString seg = line.mid(row.startCol, row.endCol - row.startCol);
                paintWhitespaceMarkers(painter, seg, kLeftPaddingPx, baselineY,
                                       vp.charWidth);
            }
            if (!row.foldPlaceholder.isEmpty()) {
                // Compute visual end of the already-drawn segment.
                const QString seg = line.mid(row.startCol, row.endCol - row.startCol);
                const int segVisualLen = expandTabsAt(seg, 0).size();
                const int segEndX = kLeftPaddingPx + segVisualLen * vp.charWidth
                                    + vp.charWidth; // one-char gap
                drawFoldPlaceholder(painter, row.foldPlaceholder,
                                    segEndX, topY, lineHeight);
            }
        }
        return;
    }

    const int first = vp.firstVisibleLine;
    const int last  = vp.lastVisibleLine;
    for (int i = first; i <= last && i < lineCount; ++i) {
        const int topY = vp.contentOffsetY + (i - first) * lineHeight;
        const int baselineY = topY + ascent;
        const QString& line = doc->lineAt(i);
        const QVector<StyleSpan>* spans = m_spansProvider ? m_spansProvider(i) : nullptr;
        drawSegmentWithSpans(painter, line, 0, line.size(),
                             baseX, baselineY, vp.charWidth, spans);
        if (m_showWhitespace) {
            paintWhitespaceMarkers(painter, line, baseX, baselineY, vp.charWidth);
        }
    }
}

void LineRenderer::drawFoldPlaceholder(QPainter& painter, const QString& text,
                                        int x, int topY, int lineHeight) const {
    const QFontMetrics fm(m_font);
    const int pad = 3;
    const int textW = fm.horizontalAdvance(text);
    const int w = textW + 2 * pad;
    // Full line height — ascent+descent of the glyphs must fit (descenders on
    // '{', 'y' etc. otherwise clip the rounded border's bottom edge).
    const QRect r(x, topY, w, lineHeight);

    painter.save();
    QColor bg = painter.pen().color();
    bg.setAlphaF(0.08);
    painter.fillRect(r, bg);
    QColor border = painter.pen().color();
    border.setAlphaF(0.35);
    painter.setPen(border);
    painter.drawRoundedRect(r, 3, 3);
    QColor fg = painter.pen().color();
    fg.setAlphaF(0.75);
    painter.setPen(fg);
    painter.setFont(m_font);
    painter.drawText(x + pad, topY + fm.ascent(), text);
    painter.restore();
}

void LineRenderer::paintWhitespaceMarkers(QPainter& painter,
                                           const QString& seg,
                                           int baseX,
                                           int baselineY,
                                           int charWidth,
                                           int visualStart) const {
    static const QString kDot(QChar(0x00B7));  // · middle dot
    static const QString kArrow(QChar(0x2192)); // → rightwards arrow

    painter.save();
    QColor wsColor = painter.pen().color();
    wsColor.setAlphaF(0.35);
    painter.setPen(wsColor);

    int visual = visualStart;
    for (QChar ch : seg) {
        if (ch == QLatin1Char(' ')) {
            painter.drawText(baseX + visual * charWidth, baselineY, kDot);
            ++visual;
        } else if (ch == QLatin1Char('\t')) {
            const int tabSpaces = m_tabWidth - (visual % m_tabWidth);
            painter.drawText(baseX + visual * charWidth, baselineY, kArrow);
            visual += tabSpaces;
        } else {
            ++visual;
        }
    }
    painter.restore();
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

QString LineRenderer::expandTabsAt(const QString& chunk, int startVisual) const {
    if (!chunk.contains(QLatin1Char('\t'))) {
        return chunk;
    }
    QString out;
    out.reserve(chunk.size() + 8);
    int visual = startVisual;
    for (QChar ch : chunk) {
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

void LineRenderer::drawSegmentWithSpans(QPainter& painter,
                                         const QString& line,
                                         int segStart, int segEnd,
                                         int drawX, int baselineY,
                                         int charWidth,
                                         const QVector<StyleSpan>* spans) const {
    if (segStart >= segEnd) return;

    // Fast path: no highlighting → single drawText.
    if (!spans || spans->isEmpty() || !m_palette || m_palette->isEmpty()) {
        const QString seg = line.mid(segStart, segEnd - segStart);
        const QString text = expandTabs(seg);
        if (!text.isEmpty()) {
            painter.drawText(drawX, baselineY, text);
        }
        return;
    }

    const QPen  defaultPen  = painter.pen();
    const QFont defaultFont = painter.font();

    int rawCol = segStart;
    int visual = 0;                  // relative to segStart (=segment-local visual col)

    while (rawCol < segEnd) {
        // Find span that covers rawCol, or the next span after it.
        int attrId   = -1;
        int chunkEnd = segEnd;
        for (const StyleSpan& s : *spans) {
            const int sEnd = s.start + s.length;
            if (sEnd <= rawCol) continue;        // span ends before rawCol
            if (s.start > rawCol) {
                chunkEnd = qMin(chunkEnd, s.start);
                break;
            }
            attrId   = s.attributeId;
            chunkEnd = qMin(chunkEnd, sEnd);
            break;
        }

        // Apply attribute (foreground / bold / italic / underline).
        QPen  pen = defaultPen;
        QFont f   = defaultFont;
        if (attrId >= 0 && attrId < m_palette->size()) {
            const TextAttribute& a = (*m_palette)[attrId];
            if (a.foreground.isValid()) pen.setColor(a.foreground);
            if (a.bold)      f.setBold(true);
            if (a.italic)    f.setItalic(true);
            if (a.underline) f.setUnderline(true);
        }
        painter.setPen(pen);
        painter.setFont(f);

        const QString chunk    = line.mid(rawCol, chunkEnd - rawCol);
        const QString expanded = expandTabsAt(chunk, visual);
        painter.drawText(drawX + visual * charWidth, baselineY, expanded);

        // Advance visual column past this chunk.
        for (QChar ch : chunk) {
            if (ch == QLatin1Char('\t')) {
                visual = (visual / m_tabWidth + 1) * m_tabWidth;
            } else {
                ++visual;
            }
        }
        rawCol = chunkEnd;
    }

    painter.setPen(defaultPen);
    painter.setFont(defaultFont);
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
