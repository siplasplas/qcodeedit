#include <qce/margins/LineNumberGutter.h>
#include "LineRenderer.h"

#include <qce/ITextDocument.h>
#include <qce/ViewportState.h>

#include <QFontMetrics>
#include <QPainter>
#include <QRect>

namespace qce {

static constexpr int kHorizontalPadding = 4; // px on each side of the digits

struct LineNumberGutter::Private {
    const ITextDocument* m_doc = nullptr;
    QFont m_font;
};

LineNumberGutter::LineNumberGutter(const ITextDocument* doc)
    :
    d(std::make_unique<Private>()) {
    d->m_doc = doc;}

void LineNumberGutter::setDocument(const ITextDocument* doc) {
    d->m_doc = doc;
}

void LineNumberGutter::setFont(const QFont& font) {
    d->m_font = font;
}

int LineNumberGutter::digitCount(int lineCount) {
    if (lineCount <= 0) {
        return 1;
    }
    int digits = 0;
    while (lineCount > 0) {
        lineCount /= 10;
        ++digits;
    }
    return digits;
}

int LineNumberGutter::preferredWidth(const ViewportState& vp) const {
    const int lines = d->m_doc ? d->m_doc->lineCount() : 0;
    const int digits = digitCount(lines);
    // Use charWidth from the viewport (same mono-font assumption as editor).
    return digits * vp.charWidth + 2 * kHorizontalPadding;
}

void LineNumberGutter::paintWrapArrow(QPainter& painter, int right, int centerY,
                                      int charWidth) {
    // "↳" drawn with lines: a short stem down, then right to an arrow head.
    const int w = qMax(5, charWidth * 4 / 5);
    const int h = qMax(3, w / 2);
    const int head = qMax(2, h / 2);
    const int x0 = right - w;
    const int bottom = centerY + h / 2;
    painter.drawLine(x0, bottom - h, x0, bottom);
    painter.drawLine(x0, bottom, right, bottom);
    painter.drawLine(right - head, bottom - head, right, bottom);
    painter.drawLine(right - head, bottom + head, right, bottom);
}

void LineNumberGutter::paint(QPainter& painter,
                              const ViewportState& vp,
                              const QRect& marginRect) {
    if (!d->m_doc || !vp.isValid()) {
        return;
    }

    painter.save();
    painter.setFont(d->m_font);
    painter.setClipRect(marginRect);

    const QFontMetrics fm(d->m_font);
    const int baseline = LineRenderer::backgroundBand(d->m_font, vp.lineHeight).baseline;
    const int rightEdge = marginRect.right() - kHorizontalPadding;

    const int first = vp.firstVisibleLine;
    const int last  = vp.lastVisibleLine;
    const int lineCount = d->m_doc->lineCount();

    if (!vp.rows.isEmpty()) { // (also without word wrap: folded lines left out)
        for (int ri = 0; ri < vp.rows.size(); ++ri) {
            const auto& row = vp.rows[ri];
            if (row.logicalLine >= lineCount) continue;
            const int topY = marginRect.top() + vp.contentOffsetY + ri * vp.lineHeight;
            if (!row.isFirstRow) {
                // Continuation row of a wrapped line: a short hooked arrow.
                paintWrapArrow(painter, rightEdge, topY + baseline - fm.xHeight() / 2,
                               vp.charWidth);
                continue;
            }
            const int baselineY = topY + baseline;
            const QString label = QString::number(row.logicalLine + 1);
            const int textWidth = fm.horizontalAdvance(label);
            painter.drawText(rightEdge - textWidth, baselineY, label);
        }
    } else {
        for (int i = first; i <= last && i < lineCount; ++i) {
            const int topY = marginRect.top()
                             + vp.contentOffsetY
                             + (i - first) * vp.lineHeight;
            const int baselineY = topY + baseline;
            const QString label = QString::number(i + 1);
            const int textWidth = fm.horizontalAdvance(label);
            painter.drawText(rightEdge - textWidth, baselineY, label);
        }
    }

    painter.restore();
}


LineNumberGutter::~LineNumberGutter() = default;
LineNumberGutter::LineNumberGutter(const LineNumberGutter& other) : d(std::make_unique<Private>(*other.d)) {}
LineNumberGutter& LineNumberGutter::operator=(const LineNumberGutter& other) { if (this != &other) *d = *other.d; return *this; }
// The source stays usable, with an empty state.
LineNumberGutter::LineNumberGutter(LineNumberGutter&& other) noexcept : d(std::make_unique<Private>()) { d.swap(other.d); }
LineNumberGutter& LineNumberGutter::operator=(LineNumberGutter&& other) noexcept { d.swap(other.d); return *this; }
const ITextDocument* LineNumberGutter::document() const { return d->m_doc; }
const QFont& LineNumberGutter::font() const { return d->m_font; }

} // namespace qce
