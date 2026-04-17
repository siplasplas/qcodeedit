#include <qce/margins/LineNumberGutter.h>

#include <qce/ITextDocument.h>
#include <qce/ViewportState.h>

#include <QFontMetrics>
#include <QPainter>
#include <QRect>

namespace qce {

static constexpr int kHorizontalPadding = 4; // px on each side of the digits

LineNumberGutter::LineNumberGutter(const ITextDocument* doc)
    : m_doc(doc) {}

void LineNumberGutter::setDocument(const ITextDocument* doc) {
    m_doc = doc;
}

void LineNumberGutter::setFont(const QFont& font) {
    m_font = font;
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
    const int lines = m_doc ? m_doc->lineCount() : 0;
    const int digits = digitCount(lines);
    // Use charWidth from the viewport (same mono-font assumption as editor).
    return digits * vp.charWidth + 2 * kHorizontalPadding;
}

void LineNumberGutter::paint(QPainter& painter,
                              const ViewportState& vp,
                              const QRect& marginRect) {
    if (!m_doc || !vp.isValid()) {
        return;
    }

    painter.save();
    painter.setFont(m_font);
    painter.setClipRect(marginRect);

    const QFontMetrics fm(m_font);
    const int ascent = fm.ascent();
    const int rightEdge = marginRect.right() - kHorizontalPadding;

    const int first = vp.firstVisibleLine;
    const int last  = vp.lastVisibleLine;
    const int lineCount = m_doc->lineCount();

    for (int i = first; i <= last && i < lineCount; ++i) {
        const int topY = marginRect.top()
                         + vp.contentOffsetY
                         + (i - first) * vp.lineHeight;
        const int baselineY = topY + ascent;

        const QString label = QString::number(i + 1); // 1-based
        const int textWidth = fm.horizontalAdvance(label);
        painter.drawText(rightEdge - textWidth, baselineY, label);
    }

    painter.restore();
}

} // namespace qce
