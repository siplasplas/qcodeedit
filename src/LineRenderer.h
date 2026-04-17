#pragma once

#include <QFont>
#include <QString>

class QPainter;

namespace qce {

class ITextDocument;
struct ViewportState;

/// Internal text renderer. Paints the visible lines of a document into a
/// QPainter, given the current viewport state.
///
/// Lives in src/ (not public API) because it is an implementation detail of
/// CodeEditArea. Keeping it separate from CodeEditArea prevents paintEvent
/// from growing into a mega-function and makes the drawing logic
/// independently testable (given a QImage + QPainter in a later test).
///
/// v0.2: plain QPainter::drawText per line, tabs expanded to spaces, no
/// styling. Designed so richer features (syntax highlighting, per-line
/// backgrounds) can be layered on without rewriting the core loop.
class LineRenderer {
public:
    /// Left padding inside the viewport, in pixels. Leaves a little gap
    /// between the left edge and the first character.
    static constexpr int kLeftPaddingPx = 4;

    LineRenderer() = default;

    /// Sets the font used for drawing. Caller is responsible for setting the
    /// same font on CodeEditArea so that QFontMetrics agrees.
    void setFont(const QFont& font) { m_font = font; }
    const QFont& font() const { return m_font; }

    /// Number of space characters a tab expands to. v0.2 simplification;
    /// will be replaced by proper tab-stop columns later.
    void setTabWidth(int spaces) { m_tabWidth = spaces > 0 ? spaces : 4; }
    int tabWidth() const { return m_tabWidth; }

    /// Paints the visible region of the document.
    void paint(QPainter& painter,
               const ITextDocument* doc,
               const ViewportState& vp) const;

    /// Visual column (number of displayed characters) at logical char index
    /// `charIndex` in `line`, given `tabWidth`. Tabs advance to the next tab
    /// stop. Used by CodeEditArea to position the caret and selection rects.
    static int visualColumn(const QString& line, int charIndex, int tabWidth);

private:
    QFont m_font;
    int m_tabWidth = 4;

    /// Column-aware tab expansion: each '\t' advances to the next tab stop.
    QString expandTabs(const QString& line) const;
};

} // namespace qce
