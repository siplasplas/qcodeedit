#include "LineRenderer.h"

#include "Bidi.h"

#include <qce/CodePoints.h>
#include <qce/ITextDocument.h>
#include <qce/ViewportState.h>

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace qce {

int LineRenderer::lineHeightFor(const QFont& font) {
    // Ascent + descent plus 15% leading. backgroundBand() keeps the glyphs
    // inside the row for any height >= ascent + descent.
    return qRound(QFontMetrics(font).height() * 1.15);
}

LineRenderer::BackgroundBand LineRenderer::backgroundBand(const QFont& font, int lineHeight) {
    const QFontMetrics fm(font);
    const QRect bounds = fm.tightBoundingRect(QStringLiteral("L"));
    const int padding = qMax(0, (lineHeight - bounds.height()) / 2);
    // Center the baseline on capital ink, but keep the whole text box
    // (ascent above, descent below the baseline) inside the row so that
    // descenders and accents are never covered by the next/previous row's
    // background. With spare height this changes nothing; in a tight row
    // (lineHeight == ascent + descent) the baseline lands on the ascent.
    int baseline = padding - bounds.top();
    const int lowest = lineHeight - fm.descent();
    baseline = (lowest >= fm.ascent()) ? qBound(fm.ascent(), baseline, lowest) : fm.ascent();
    return {0, bounds.height() + 2 * padding, baseline};
}

void LineRenderer::setFont(const QFont& font) {
    m_font = font;
    QStringList families = font.families();
    if (families.isEmpty()) families << font.family();
    for (const QString& fallback : {QStringLiteral("Liberation Mono"), QStringLiteral("Cousine")}) {
        if (!families.contains(fallback, Qt::CaseInsensitive)) families << fallback;
    }
    m_font.setFamilies(families);
}

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

    const int lineHeight = vp.lineHeight;
    const int baseline = backgroundBand(m_font, lineHeight).baseline;

    // X position of the first character after padding, then shifted left by
    // the horizontal scroll offset. Lines that are shorter than the offset
    // will draw off-screen and simply be clipped by the painter.
    const int baseX = kLeftPaddingPx - vp.contentOffsetX;

    // Rows (with word wrap, and without it a row per visible line: folded
    // lines are left out); lines only before the area has its row layout
    if (!vp.rows.isEmpty()) {
        for (int ri = 0; ri < vp.rows.size(); ++ri) {
            const auto& row = vp.rows[ri];
            const int topY = vp.contentOffsetY + ri * lineHeight;
            const int baselineY = topY + baseline;
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
                                 baseX, baselineY, vp.charWidth, spans,
                                 topY, lineHeight, m_decorationsProvider ? m_decorationsProvider(row.logicalLine) : nullptr);
            if (m_showWhitespace) {
                if (bidi::hasRightToLeft(line)) {
                    paintBidiWhitespaceMarkers(painter, line, row.startCol, drawEnd, baseX, baselineY,
                                               vp.charWidth);
                } else {
                    const QString seg = line.mid(row.startCol, drawEnd - row.startCol);
                    paintWhitespaceMarkers(painter, seg, baseX, baselineY,
                                           vp.charWidth);
                }
            }
            if (!row.foldPlaceholder.isEmpty()) {
                const QString drawnSeg = line.mid(row.startCol, drawEnd - row.startCol);
                const int segVisualLen = cellCount(expandTabsAt(drawnSeg, 0));
                const int segEndX = baseX + segVisualLen * vp.charWidth;
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
        const int baselineY = topY + baseline;
        const QString& line = doc->lineAt(i);
        const QVector<StyleSpan>* spans = m_spansProvider ? m_spansProvider(i) : nullptr;
        drawSegmentWithSpans(painter, line, 0, line.size(),
                             baseX, baselineY, vp.charWidth, spans,
                             topY, lineHeight, m_decorationsProvider ? m_decorationsProvider(i) : nullptr);
        if (m_showWhitespace) {
            if (bidi::hasRightToLeft(line))
                paintBidiWhitespaceMarkers(painter, line, 0, int(line.size()), baseX, baselineY, vp.charWidth);
            else
                paintWhitespaceMarkers(painter, line, baseX, baselineY, vp.charWidth);
        }
    }
}

void LineRenderer::paintSegment(QPainter& painter, const QString& line,
                                int segStart, int segEnd, int drawX,
                                int topY, int lineHeight, int charWidth,
                                const QVector<StyleSpan>* spans) const {
    if (segStart >= segEnd) return;
    painter.setFont(m_font);
    const int baselineY = topY + backgroundBand(m_font, lineHeight).baseline;
    drawSegmentWithSpans(painter, line, segStart, segEnd, drawX, baselineY,
                         charWidth, spans, topY, lineHeight);
}

void LineRenderer::drawFoldPlaceholder(QPainter& painter, const QString& text,
                                        int x, int topY, int lineHeight) const {
    const QFontMetrics fm(m_font);
    const int pad = 3;
    const int textW = fm.horizontalAdvance(text);
    const int w = textW + 2 * pad;
    // The box fills the row; backgroundBand() keeps the glyphs inside it.
    // adjusted(): a 1-px pen draws one pixel right of and below the rect.
    const QRect r = QRect(x, topY, w, lineHeight).adjusted(0, 0, -1, -1);

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
    painter.drawText(x + pad, topY + backgroundBand(m_font, lineHeight).baseline, text);
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
    for (int i = 0; i < seg.size(); ++i) {
        const QChar ch = seg.at(i);
        if (ch == QLatin1Char(' ')) {
            painter.drawText(baseX + visual * charWidth, baselineY, kDot);
            ++visual;
        } else if (ch == QLatin1Char('\t')) {
            const int tabSpaces = m_tabWidth - (visual % m_tabWidth);
            painter.drawText(baseX + visual * charWidth, baselineY, kArrow);
            visual += tabSpaces;
        } else {
            visual += codepoints::cellsAt(seg, i);
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
            visual += codepoints::cellsAt(line, i);  // a surrogate pair is one character, a wide one two cells
        }
    }
    return visual;
}

int LineRenderer::cellCount(const QString& text) {
    int cells = 0;
    for (int i = 0; i < text.size(); ++i) cells += codepoints::cellsAt(text, i);
    return cells;
}

void LineRenderer::drawWavyUnderline(QPainter& painter, int x1, int x2, int baselineY,
                                     const QColor& color) const {
    // Half the descent below the baseline, so the wave stays under the text
    // and inside the line; two pixels high, a period of four.
    const qreal y = baselineY + qMax(1, QFontMetrics(m_font).descent() / 2) + 0.5;
    const qreal amplitude = 1.0;
    QPainterPath path(QPointF(x1, y));
    bool up = true;
    for (qreal x = x1; x < x2; x += 2) {
        path.lineTo(QPointF(qMin<qreal>(x + 2, x2), up ? y - amplitude : y + amplitude));
        up = !up;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

void LineRenderer::drawCells(QPainter& painter, int x, int baselineY, int charWidth,
                             const QString& text) {
    int runStart = 0;   // first index of the run of one-cell BMP characters not drawn yet
    int runCell  = 0;   // its cell
    int cell     = 0;
    for (int i = 0; i < text.size();) {
        const bool pair = codepoints::isPairAt(text, i);
        const int cells = codepoints::cellsAt(text, i);
        if (!pair && cells == 1) {
            ++i;
            ++cell;
            continue;
        }
        // A character outside the BMP or a wide one, drawn alone at its
        // cell: its glyph's advance need not be a whole number of cells.
        if (runStart < i) {
            painter.drawText(x + runCell * charWidth, baselineY, text.mid(runStart, i - runStart));
        }
        const int length = pair ? 2 : 1;
        painter.drawText(x + cell * charWidth, baselineY, text.mid(i, length));
        i += length;
        cell += cells;
        runStart = i;
        runCell  = cell;
    }
    if (runStart < text.size()) {
        painter.drawText(x + runCell * charWidth, baselineY, text.mid(runStart));
    }
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
                                         int topY, int lineHeight,
                                         const QVector<ExtraSelection>* decorations) const {
    if (segStart >= segEnd) return;
    if (bidi::hasRightToLeft(line)) {
        drawBidiSegment(painter, line, segStart, segEnd, drawX, baselineY, charWidth, spans, topY, lineHeight,
                        decorations);
        return;
    }

    // Fast path: no highlighting → single drawText.
    if ((!spans || spans->isEmpty() || !m_palette || m_palette->isEmpty())
        && (!decorations || decorations->isEmpty())) {
        const QString seg = line.mid(segStart, segEnd - segStart);
        const QString text = expandTabs(seg);
        if (!text.isEmpty()) {
            drawCells(painter, drawX, baselineY, charWidth, text);
        }
        return;
    }

    const auto band = backgroundBand(m_font, lineHeight);
    const QPen  defaultPen  = painter.pen();
    const QFont defaultFont = painter.font();

    int rawCol = segStart;
    int visual = 0;                  // relative to segStart (=segment-local visual col)

    qsizetype spanIndex = spans ? std::lower_bound(spans->cbegin(), spans->cend(), segStart,
        [](const StyleSpan& span, int column) { return span.start + span.length <= column; }) - spans->cbegin() : 0;
    qsizetype decorationIndex = decorations ? std::lower_bound(decorations->cbegin(), decorations->cend(), segStart,
        [](const ExtraSelection& range, int column) { return range.end.column <= column; }) - decorations->cbegin() : 0;
    while (rawCol < segEnd) {
        // Find span that covers rawCol, or the next span after it.
        int attrId   = -1;
        int chunkEnd = segEnd;
        if (spans && m_palette) {
            while (spanIndex < spans->size()
                   && (*spans)[spanIndex].start + (*spans)[spanIndex].length <= rawCol) ++spanIndex;
            if (spanIndex < spans->size()) {
                const auto& span = (*spans)[spanIndex];
                if (span.start > rawCol) chunkEnd = qMin(chunkEnd, span.start);
                else {
                    attrId = span.attributeId;
                    chunkEnd = qMin(chunkEnd, span.start + span.length);
                }
            }
        }
        const ExtraSelection* decoration = nullptr;
        if (decorations) {
            while (decorationIndex < decorations->size()
                   && (*decorations)[decorationIndex].end.column <= rawCol) ++decorationIndex;
            if (decorationIndex < decorations->size()) {
                const auto& range = (*decorations)[decorationIndex];
                if (range.start.column > rawCol) chunkEnd = qMin(chunkEnd, range.start.column);
                else {
                    decoration = &range;
                    chunkEnd = qMin(chunkEnd, range.end.column);
                }
            }
        }

        // Apply attribute (foreground / bold / italic / underline / background).
        QPen  pen = defaultPen;
        QFont f   = defaultFont;
        const QString chunk    = line.mid(rawCol, chunkEnd - rawCol);
        const QString expanded = expandTabsAt(chunk, visual);
        if (m_palette && attrId >= 0 && attrId < m_palette->size()) {
            const TextAttribute& a = (*m_palette)[attrId];
            if (a.background.isValid() && lineHeight > 0) {
                const QRect rect(drawX + visual * charWidth, topY + band.offset,
                                 cellCount(expanded) * charWidth, band.height);
                for (const QRect& piece : QRegion(rect).subtracted(m_selectionRegion)) {
                    painter.fillRect(piece, a.background);
                }
            }
            if (a.foreground.isValid()) pen.setColor(a.foreground);
            if (a.bold)      f.setBold(true);
            if (a.italic)    f.setItalic(true);
            if (a.underline) f.setUnderline(true);
        }
        const QRect cell(drawX + visual * charWidth, topY + band.offset,
                         cellCount(expanded) * charWidth, band.height);
        if (decoration) {
            if (decoration->background.isValid()) {
                for (const QRect& piece : QRegion(cell).subtracted(m_selectionRegion)) {
                    painter.fillRect(piece, decoration->background);
                }
            }
            if (decoration->foreground.isValid()) pen.setColor(decoration->foreground);
        }
        painter.setPen(pen);
        painter.setFont(f);

        drawCells(painter, drawX + visual * charWidth, baselineY, charWidth, expanded);
        if (decoration && decoration->underline.isValid() && cell.width() > 0)
            drawWavyUnderline(painter, cell.left(), cell.right() + 1, baselineY, decoration->underline);

        // Advance visual column past this chunk.
        for (int i = 0; i < chunk.size(); ++i) {
            if (chunk.at(i) == QLatin1Char('\t')) {
                visual = (visual / m_tabWidth + 1) * m_tabWidth;
            } else {
                visual += codepoints::cellsAt(chunk, i);
            }
        }
        rawCol = chunkEnd;
    }

    painter.setPen(defaultPen);
    painter.setFont(defaultFont);
}

namespace {

// Cells of line[from, to) in logical order, tabs reaching the tab stops
// counted from `from`.
int cellsBetween(const QString& line, int from, int to, int tabWidth) {
    int visual = 0;
    to = qMin(to, int(line.size()));
    for (int i = from; i < to; ++i) {
        if (line.at(i) == QLatin1Char('\t')) visual = (visual / tabWidth + 1) * tabWidth;
        else visual += codepoints::cellsAt(line, i);
    }
    return visual;
}

} // namespace

int LineRenderer::caretCell(const QString& line, int rowStart, int rowEnd, int index, int tabWidth) {
    if (!bidi::hasRightToLeft(line)) return cellsBetween(line, rowStart, index, tabWidth);
    const QVector<BidiCell> row = bidiRow(line, rowStart, rowEnd, tabWidth);
    if (index > rowStart) {
        const int before = codepoints::prevIndex(line, index);
        for (const BidiCell& c : row)
            if (c.index == before) return c.level % 2 ? c.cell : c.cell + c.width;
    }
    for (const BidiCell& c : row)
        if (c.index == index) return c.level % 2 ? c.cell + c.width : c.cell;
    return row.isEmpty() ? 0 : row.back().cell + row.back().width;
}

int LineRenderer::indexAtX(const QString& line, int rowStart, int rowEnd, int x, int charWidth, int tabWidth) {
    rowEnd = qMin(rowEnd, int(line.size()));
    if (!bidi::hasRightToLeft(line)) {
        int visual = 0, i = rowStart;
        while (i < rowEnd) {
            const int cw = line.at(i) == QLatin1Char('\t') ? tabWidth - visual % tabWidth : codepoints::cellsAt(line, i);
            if (x * 2 < (2 * visual + cw) * charWidth) break;
            visual += cw;
            i = codepoints::nextIndex(line, i);  // a surrogate pair is one cell
        }
        return i;
    }
    const QVector<BidiCell> row = bidiRow(line, rowStart, rowEnd, tabWidth);
    if (row.isEmpty()) return rowStart;
    for (int k = 0; k < row.size(); ++k) {
        const BidiCell& c = row[k];
        if (x >= (c.cell + c.width) * charWidth && k + 1 < row.size()) continue;
        if (x >= (c.cell + c.width) * charWidth) break;  // right of the text
        const bool leftHalf = x * 2 < (2 * c.cell + c.width) * charWidth;
        const bool before = (c.level % 2) ? !leftHalf : leftHalf;
        return before ? c.index : codepoints::nextIndex(line, c.index);
    }
    const BidiCell& last = row.back();
    return last.level % 2 ? last.index : codepoints::nextIndex(line, last.index);
}

QVector<std::pair<int, int>> LineRenderer::cellRanges(const QString& line, int rowStart, int rowEnd,
                                                      int from, int to, int tabWidth) {
    from = qMax(from, rowStart);
    to = qMin(to, rowEnd);
    if (from >= to) return {};
    if (!bidi::hasRightToLeft(line))
        return {{cellsBetween(line, rowStart, from, tabWidth), cellsBetween(line, rowStart, to, tabWidth)}};
    QVector<std::pair<int, int>> ranges;
    for (const BidiCell& c : bidiRow(line, rowStart, rowEnd, tabWidth)) {
        if (c.index < from || c.index >= to) continue;
        if (!ranges.isEmpty() && ranges.back().second == c.cell) ranges.back().second = c.cell + c.width;
        else ranges.append({c.cell, c.cell + c.width});
    }
    return ranges;
}

QVector<LineRenderer::BidiCell> LineRenderer::bidiRow(const QString& line, int segStart, int segEnd,
                                                      int tabWidth) {
    // Cells in logical order first: a tab reaches the next tab stop.
    segEnd = qMin(segEnd, int(line.size()));
    if (segStart >= segEnd) return {};
    QVector<int> width(segEnd - segStart, 0);
    int visual = 0;
    for (int i = segStart; i < segEnd; i = codepoints::nextIndex(line, i)) {
        const int w = line.at(i) == QLatin1Char('\t') ? tabWidth - visual % tabWidth
                                                       : codepoints::cellsAt(line, i);
        width[i - segStart] = w;
        visual += w;
    }
    const bidi::Levels levels = bidi::levels(line);
    QVector<BidiCell> row;
    int cell = 0;
    for (const int i : bidi::visualOrder(line, levels, segStart, segEnd)) {
        row.append({i, cell, width[i - segStart], levels.levels[i]});
        cell += width[i - segStart];
    }
    return row;
}

void LineRenderer::drawBidiSegment(QPainter& painter, const QString& line, int segStart, int segEnd,
                                   int drawX, int baselineY, int charWidth, const QVector<StyleSpan>* spans,
                                   int topY, int lineHeight, const QVector<ExtraSelection>* decorations) const {
    const auto band = backgroundBand(m_font, lineHeight);
    const QPen  defaultPen  = painter.pen();
    const QFont defaultFont = painter.font();
    const bool styled = spans && m_palette && !m_palette->isEmpty();
    const auto attributeAt = [&](int column) {
        if (!styled) return -1;
        const auto it = std::lower_bound(spans->cbegin(), spans->cend(), column,
            [](const StyleSpan& span, int c) { return span.start + span.length <= c; });
        return it != spans->cend() && it->start <= column ? it->attributeId : -1;
    };
    const auto decorationAt = [&](int column) -> const ExtraSelection* {
        if (!decorations) return nullptr;
        const auto it = std::lower_bound(decorations->cbegin(), decorations->cend(), column,
            [](const ExtraSelection& range, int c) { return range.end.column <= c; });
        return it != decorations->cend() && it->start.column <= column ? &*it : nullptr;
    };

    const QVector<BidiCell> row = bidiRow(line, segStart, segEnd, m_tabWidth);
    for (int k = 0; k < row.size();) {
        // A run: one level, one style, consecutive in the line (in reverse
        // for right to left).
        const int level = row[k].level;
        const bool rightToLeft = level % 2;
        const int attrId = attributeAt(row[k].index);
        const ExtraSelection* decoration = decorationAt(row[k].index);
        int end = k + 1;
        while (end < row.size() && row[end].level == level
               && (rightToLeft ? codepoints::nextIndex(line, row[end].index) == row[end - 1].index
                               : codepoints::nextIndex(line, row[end - 1].index) == row[end].index)
               && attributeAt(row[end].index) == attrId && decorationAt(row[end].index) == decoration)
            ++end;
        // The run's text in logical order, tabs as spaces.
        QString text;
        for (int j = k; j < end; ++j) {
            const BidiCell& c = row[rightToLeft ? end - 1 - (j - k) : j];
            if (line.at(c.index) == QLatin1Char('\t')) text += QString(c.width, QLatin1Char(' '));
            else text += line.mid(c.index, codepoints::nextIndex(line, c.index) - c.index);
        }
        const int cells = row[end - 1].cell + row[end - 1].width - row[k].cell;
        const QRect rect(drawX + row[k].cell * charWidth, topY + band.offset, cells * charWidth, band.height);

        QPen  pen = defaultPen;
        QFont f   = defaultFont;
        if (attrId >= 0 && attrId < m_palette->size()) {
            const TextAttribute& a = (*m_palette)[attrId];
            if (a.background.isValid() && lineHeight > 0) {
                for (const QRect& piece : QRegion(rect).subtracted(m_selectionRegion)) painter.fillRect(piece, a.background);
            }
            if (a.foreground.isValid()) pen.setColor(a.foreground);
            if (a.bold)      f.setBold(true);
            if (a.italic)    f.setItalic(true);
            if (a.underline) f.setUnderline(true);
        }
        if (decoration) {
            if (decoration->background.isValid()) {
                for (const QRect& piece : QRegion(rect).subtracted(m_selectionRegion))
                    painter.fillRect(piece, decoration->background);
            }
            if (decoration->foreground.isValid()) pen.setColor(decoration->foreground);
        }
        painter.setPen(pen);
        painter.setFont(f);
        if (rightToLeft) {
            // Right-to-left marks around it: Qt lays the whole run out right
            // to left, neutrals at its ends too, and mirrors brackets.
            static const QString kRlm(QChar(0x200F));
            painter.drawText(rect.left(), baselineY, kRlm + text + kRlm);
        } else {
            drawCells(painter, rect.left(), baselineY, charWidth, text);
        }
        if (decoration && decoration->underline.isValid() && rect.width() > 0)
            drawWavyUnderline(painter, rect.left(), rect.right() + 1, baselineY, decoration->underline);
        k = end;
    }
    painter.setPen(defaultPen);
    painter.setFont(defaultFont);
}

void LineRenderer::paintBidiWhitespaceMarkers(QPainter& painter, const QString& line, int segStart, int segEnd,
                                              int baseX, int baselineY, int charWidth) const {
    static const QString kDot(QChar(0x00B7));  // · middle dot
    static const QString kArrow(QChar(0x2192)); // → rightwards arrow
    painter.save();
    QColor wsColor = painter.pen().color();
    wsColor.setAlphaF(0.35);
    painter.setPen(wsColor);
    for (const BidiCell& c : bidiRow(line, segStart, segEnd, m_tabWidth)) {
        if (line.at(c.index) == QLatin1Char(' ')) painter.drawText(baseX + c.cell * charWidth, baselineY, kDot);
        else if (line.at(c.index) == QLatin1Char('\t')) painter.drawText(baseX + c.cell * charWidth, baselineY, kArrow);
    }
    painter.restore();
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
