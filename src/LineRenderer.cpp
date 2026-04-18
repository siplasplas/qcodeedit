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

    // fm.ascent() includes room for accents above capitals (Á, É, ...); most
    // code is plain ASCII and renders ~(ascent - capHeight) pixels below the
    // cell top, while caret and selection span the full cell. Shift the
    // baseline up by that gap so glyphs sit at the cell top like the caret.
    const int topShift = qMax(0, ascent - fm.capHeight());

    // X position of the first character after padding, then shifted left by
    // the horizontal scroll offset. Lines that are shorter than the offset
    // will draw off-screen and simply be clipped by the painter.
    const int baseX = kLeftPaddingPx - vp.contentOffsetX;

    if (vp.wordWrap && !vp.rows.isEmpty()) {
        for (int ri = 0; ri < vp.rows.size(); ++ri) {
            const auto& row = vp.rows[ri];
            const int topY = vp.contentOffsetY + ri * lineHeight;
            const int baselineY = topY + ascent - topShift;
            if (row.isFiller) {
                if (row.fillerColor.isValid()) {
                    painter.fillRect(0, topY, vp.viewportWidth, lineHeight,
                                     row.fillerColor);
                }
                if (!row.fillerLabel.isEmpty()) {
                    painter.save();
                    QColor c = painter.pen().color();
                    c.setAlphaF(0.55);
                    painter.setPen(c);
                    const int labX = (vp.viewportWidth
                        - fm.horizontalAdvance(row.fillerLabel)) / 2;
                    painter.drawText(labX, baselineY, row.fillerLabel);
                    painter.restore();
                }
                continue;
            }
            const QString& line = doc->lineAt(row.logicalLine);
            const QVector<StyleSpan>* spans = m_spansProvider
                ? m_spansProvider(row.logicalLine) : nullptr;

            // If this row is the header of a collapsed fold, cut off the
            // visible text at the fold's start column — the placeholder then
            // stands in for everything from that column onward (including
            // the opening '{' / '/*' which belongs to the hidden region).
            int drawEnd = row.endCol;
            if (!row.foldPlaceholder.isEmpty() && row.foldStartColumn >= 0) {
                drawEnd = qMin(drawEnd, row.foldStartColumn);
            }

            drawSegmentWithSpans(painter, line, row.startCol, drawEnd,
                                 kLeftPaddingPx, baselineY, vp.charWidth, spans,
                                 topY, lineHeight);
            if (m_showWhitespace) {
                const QString seg = line.mid(row.startCol, drawEnd - row.startCol);
                paintWhitespaceMarkers(painter, seg, kLeftPaddingPx, baselineY,
                                       vp.charWidth);
            }
            if (!row.foldPlaceholder.isEmpty()) {
                const QString drawnSeg = line.mid(row.startCol, drawEnd - row.startCol);
                const int segVisualLen = expandTabsAt(drawnSeg, 0).size();
                const int segEndX = kLeftPaddingPx + segVisualLen * vp.charWidth;
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
        const int baselineY = topY + ascent - topShift;
        const QString& line = doc->lineAt(i);
        const QVector<StyleSpan>* spans = m_spansProvider ? m_spansProvider(i) : nullptr;
        drawSegmentWithSpans(painter, line, 0, line.size(),
                             baseX, baselineY, vp.charWidth, spans,
                             topY, lineHeight);
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
    // Some glyphs ('{', 'y', 'g') extend a couple of pixels below what
    // fm.height() reports; pad the box a bit so the border doesn't clip
    // them. A small overshoot into the next line's space is acceptable
    // (placeholders are rare and transient).
    const int extraBot = 2;
    const QRect r(x, topY, w, lineHeight + extraBot);

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
                                         const QVector<StyleSpan>* spans,
                                         int topY, int lineHeight) const {
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

        // Apply attribute (foreground / bold / italic / underline / background).
        QPen  pen = defaultPen;
        QFont f   = defaultFont;
        const QString chunk    = line.mid(rawCol, chunkEnd - rawCol);
        const QString expanded = expandTabsAt(chunk, visual);
        if (attrId >= 0 && attrId < m_palette->size()) {
            const TextAttribute& a = (*m_palette)[attrId];
            if (a.background.isValid() && lineHeight > 0) {
                painter.fillRect(drawX + visual * charWidth, topY,
                                 expanded.length() * charWidth, lineHeight,
                                 a.background);
            }
            if (a.foreground.isValid()) pen.setColor(a.foreground);
            if (a.bold)      f.setBold(true);
            if (a.italic)    f.setItalic(true);
            if (a.underline) f.setUnderline(true);
        }
        painter.setPen(pen);
        painter.setFont(f);

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
