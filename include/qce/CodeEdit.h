#pragma once

#include <QWidget>

namespace qce {

class CodeEditArea;
class ITextDocument;

/// Top-level code editor widget.
///
/// Compositional container that holds the central CodeEditArea and arranges
/// optional margins (gutters, side bars, minimaps) on its left and right
/// sides. Also owns the decision of which side the vertical scroll bar lives
/// on — this is configurable, which is the primary motivation for building
/// this widget instead of using QPlainTextEdit.
///
/// CodeEdit does not render text itself; it only arranges child widgets.
/// Text rendering is entirely delegated to CodeEditArea.
///
/// v1: minimal container. No margins yet, no scrollbar-side switch yet
/// (comes in the next milestone). This is just a stub that places the
/// CodeEditArea in the center.
class CodeEdit : public QWidget {
    Q_OBJECT
public:
    enum class ScrollBarSide {
        Right, ///< Default, matches QPlainTextEdit.
        Left   ///< Used e.g. for the left pane of a side-by-side diff view.
    };

    explicit CodeEdit(QWidget* parent = nullptr);
    ~CodeEdit() override;

    /// Attaches a document to the underlying area. Non-owning.
    void setDocument(ITextDocument* doc);
    ITextDocument* document() const;

    /// Access to the underlying area, e.g. for direct viewport-state queries
    /// or signal connection from margins.
    CodeEditArea* area() const { return m_area; }

    /// Switches the vertical scroll bar to the left or right side. Default
    /// is Right. v1: stub; actual switching will be implemented alongside
    /// the left rail.
    void setScrollBarSide(ScrollBarSide side);
    ScrollBarSide scrollBarSide() const { return m_scrollBarSide; }

private:
    CodeEditArea* m_area = nullptr;
    ScrollBarSide m_scrollBarSide = ScrollBarSide::Right;
};

} // namespace qce
