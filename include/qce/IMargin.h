#pragma once

class QPainter;
class QRect;

namespace qce {

struct ViewportState;

/// Interface for margin drawers (gutters, side bars, minimap, etc.).
///
/// Margins are pure drawers — they do not inherit from QWidget. The rail
/// widgets (LeftRail, RightRail) own the margins and call paint() during
/// their own paintEvent. This keeps margins lightweight and independent of
/// the Qt widget lifecycle.
///
/// Margins receive the current ViewportState so they can stay in sync with
/// the editor without holding a reference to CodeEditArea.
class IMargin {
public:
    virtual ~IMargin() = default;

    /// Returns the preferred width of this margin in pixels, given the
    /// current viewport state. Called by the rail before layout.
    virtual int preferredWidth(const ViewportState& vp) const = 0;

    /// Paints the margin into `painter`, clipped to `marginRect`.
    /// `marginRect` is in the rail widget's coordinate system.
    virtual void paint(QPainter& painter,
                       const ViewportState& vp,
                       const QRect& marginRect) = 0;
};

} // namespace qce
