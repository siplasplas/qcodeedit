#pragma once

#include <qce/IMargin.h>

#include <functional>

namespace qce {

class FoldState;

/// Draws a narrow column of fold arrows (▸ collapsed / ▾ expanded) on the
/// first visual row of each foldable region. Clicking a row containing a
/// region invokes the supplied toggle callback (typically wired to
/// CodeEditArea::toggleFoldAt).
///
/// Non-owning references: the gutter reads FoldState but does not own it,
/// and the callback is user-provided.
class FoldingGutter : public IMargin {
public:
    using ToggleCallback = std::function<void(int logicalLine)>;

    FoldingGutter(const FoldState* state, ToggleCallback toggle);

    int  preferredWidth(const ViewportState& vp) const override;
    void paint(QPainter& painter,
               const ViewportState& vp,
               const QRect& marginRect) override;
    void mousePressed(const QPoint& local,
                      const ViewportState& vp,
                      const QRect& marginRect) override;

private:
    const FoldState* m_state;
    ToggleCallback   m_toggle;
};

} // namespace qce
