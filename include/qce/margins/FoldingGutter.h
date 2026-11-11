#pragma once

#include <memory>

#include <qce/IMargin.h>

#include <functional>

namespace qce {

class FoldState;

/// Draws a narrow column of light gray open chevrons (right when collapsed,
/// down when expanded) on the first visual row of each foldable region.
/// Expanded chevrons appear only while the pointer is over this margin's strip;
/// collapsed chevrons are always visible. Clicking a row containing a
/// region invokes the supplied toggle callback (typically wired to
/// CodeEditArea::toggleFoldAt).
///
/// Non-owning references: the gutter reads FoldState but does not own it,
/// and the callback is user-provided.
class FoldingGutter : public IMargin {
public:
    ~FoldingGutter();
    FoldingGutter(const FoldingGutter& other);
    FoldingGutter& operator=(const FoldingGutter& other);
    FoldingGutter(FoldingGutter&& other) noexcept;
    FoldingGutter& operator=(FoldingGutter&& other) noexcept;
    using ToggleCallback = std::function<void(int logicalLine)>;

    FoldingGutter(const FoldState* state, ToggleCallback toggle);

    int  preferredWidth(const ViewportState& vp) const override;
    void paint(QPainter& painter,
               const ViewportState& vp,
               const QRect& marginRect) override;
    void mousePressed(const QPoint& local,
                      const ViewportState& vp,
                      const QRect& marginRect) override;
    void hoverChanged(bool hovered) override;

private:
    // The state, in FoldingGutter.cpp: fields added there keep the
    // binary interface (the size of this class does not change).
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace qce
