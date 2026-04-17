#pragma once

#include <QWidget>

class QScrollBar;

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

    /// Removes a margin from the left rail. No-op if not present.
    void removeLeftMargin(IMargin* margin);

    /// Adds a margin to the right rail. Non-owning; caller manages lifetime.
    void addRightMargin(IMargin* margin);

    /// Removes a margin from the right rail. No-op if not present.
    void removeRightMargin(IMargin* margin);

    /// Moves the vertical scroll bar to the left or right side of the editor.
    /// Default is Right. On Left, the area's built-in scroll bar is hidden
    /// and a standalone QScrollBar is placed at the far left of the layout,
    /// kept in sync with the area's scroll position.
    void setScrollBarSide(ScrollBarSide side);
    ScrollBarSide scrollBarSide() const { return m_scrollBarSide; }

private:
    CodeEditArea* m_area        = nullptr;
    LeftRail*     m_leftRail    = nullptr;
    RightRail*    m_rightRail   = nullptr;
    QScrollBar*   m_leftVScroll = nullptr; // non-null only in Left mode
    ScrollBarSide m_scrollBarSide = ScrollBarSide::Right;

    void applyScrollBarSide(ScrollBarSide side);
};

} // namespace qce
