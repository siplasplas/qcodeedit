#pragma once

#include <qce/StyleSpan.h>
#include <qce/TextAttribute.h>

#include <QFont>
#include <QString>
#include <QVector>

#include <functional>

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

    void setShowWhitespace(bool show) { m_showWhitespace = show; }
    bool showWhitespace() const { return m_showWhitespace; }

    using SpansForLineFn = std::function<const QVector<StyleSpan>*(int line)>;

    /// Set the attribute palette (non-owning). Passing nullptr disables
    /// span-based coloring and reverts to default foreground rendering.
    void setAttributePalette(const QVector<TextAttribute>* palette) { m_palette = palette; }

    /// Provider that returns spans for a given logical line, or nullptr if
    /// none. Called once per rendered line/row. Callback may be null.
    void setSpansProvider(SpansForLineFn fn) { m_spansProvider = std::move(fn); }

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
    int  m_tabWidth        = 4;
    bool m_showWhitespace  = false;

    const QVector<TextAttribute>* m_palette = nullptr;
    SpansForLineFn                m_spansProvider;

    /// Column-aware tab expansion starting at visual column 0.
    QString expandTabs(const QString& line) const;

    /// Column-aware tab expansion starting at `startVisual` column. Used when
    /// rendering mid-line chunks (span segments).
    QString expandTabsAt(const QString& chunk, int startVisual) const;

    /// Draw a segment of a line with span-based syntax coloring. If no spans
    /// / no palette are set, falls back to a single plain drawText.
    /// `segStart`..`segEnd` = raw QChar range within `line`. Text is drawn at
    /// pixel x = `drawX`, with visual column 0 corresponding to `segStart`.
    void drawSegmentWithSpans(QPainter& painter,
                              const QString& line,
                              int segStart, int segEnd,
                              int drawX, int baselineY,
                              int charWidth,
                              const QVector<StyleSpan>* spans,
                              int topY = 0, int lineHeight = 0) const;

    /// Draw the fold placeholder text in a faint rounded rectangle; used on
    /// lines that are the header of a collapsed region.
    void drawFoldPlaceholder(QPainter& painter, const QString& text,
                              int x, int topY, int lineHeight) const;

    /// Second-pass: draw · for spaces and → for tabs in a muted color.
    /// seg is the raw (unexpanded) text for one visual row; visualStart is
    /// the visual column offset at the beginning of seg (0 for wrap rows and
    /// for the first row of a non-wrapped line).
    void paintWhitespaceMarkers(QPainter& painter,
                                const QString& seg,
                                int baseX,
                                int baselineY,
                                int charWidth,
                                int visualStart = 0) const;
};

} // namespace qce
