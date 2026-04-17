#pragma once

#include <QWidget>

namespace qce {

class CodeEditArea;
class ITextDocument;
class IMargin;
class LeftRail;
class RightRail;

/// Top-level code editor widget.
///
/// Compositional container: [ LeftRail | CodeEditArea | RightRail ].
/// Each rail is always present in the layout; it takes zero width when
/// no margins have been added to it.
///
/// CodeEdit does not render text itself — text rendering is delegated to
/// CodeEditArea. Margin rendering is delegated to Rail/IMargin.
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

    /// Direct access to the rendering area, e.g. for font queries or
    /// additional signal connections.
    CodeEditArea* area() const { return m_area; }

    /// Adds a margin to the left rail. Non-owning; caller manages lifetime.
    void addLeftMargin(IMargin* margin);

    /// Adds a margin to the right rail. Non-owning; caller manages lifetime.
    void addRightMargin(IMargin* margin);

    /// Switches the vertical scroll bar to the left or right side.
    /// Default is Right. Full implementation comes in section 6.5.
    void setScrollBarSide(ScrollBarSide side);
    ScrollBarSide scrollBarSide() const { return m_scrollBarSide; }

private:
    CodeEditArea* m_area      = nullptr;
    LeftRail*     m_leftRail  = nullptr;
    RightRail*    m_rightRail = nullptr;
    ScrollBarSide m_scrollBarSide = ScrollBarSide::Right;
};

} // namespace qce
