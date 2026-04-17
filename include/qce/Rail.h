#pragma once

#include <qce/ViewportState.h>

#include <QList>
#include <QWidget>

namespace qce {

class IMargin;
class CodeEditArea;

/// Base widget for a column of IMargin drawers placed alongside CodeEditArea.
///
/// Holds a list of non-owning IMargin pointers and paints them side by side
/// in its paintEvent. Width is determined by summing each margin's
/// preferredWidth() for the current viewport state. Connect to a
/// CodeEditArea via connectToArea() so the rail repaints in sync with the
/// editor's viewport changes.
///
/// LeftRail and RightRail are thin subclasses; any scrollbar-side logic
/// (v0.3 section 6.5) is added there.
class Rail : public QWidget {
    Q_OBJECT
public:
    explicit Rail(QWidget* parent = nullptr);

    /// Appends a margin to the rail. The rail does not take ownership.
    void addMargin(IMargin* margin);

    /// Removes a margin from the rail. No-op if not present.
    void removeMargin(IMargin* margin);

    /// Connects viewportChanged() from the area to this rail's update slot.
    void connectToArea(CodeEditArea* area);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private slots:
    void onViewportChanged(const ViewportState& vp);

private:
    QList<IMargin*> m_margins;
    ViewportState m_vp;

    int totalWidth() const;
};

} // namespace qce
