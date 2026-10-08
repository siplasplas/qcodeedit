#pragma once

#include <qce/GutterColors.h>
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
    /// `textEdge` is the edge next to CodeEditArea, where the separator
    /// line is drawn (right for LeftRail, left for RightRail).
    explicit Rail(QWidget* parent = nullptr, Qt::Edge textEdge = Qt::RightEdge);

    /// Appends a margin to the rail. The rail does not take ownership.
    void addMargin(IMargin* margin);

    /// Removes a margin from the rail. No-op if not present.
    void removeMargin(IMargin* margin);

    /// Connects viewportChanged() from the area to this rail's update slot.
    void connectToArea(CodeEditArea* area);

    /// Sets the rail colours; invalid entries are derived from the area's
    /// palette (see GutterColors).
    void setColors(const GutterColors& colors);
    const GutterColors& colors() const { return m_colors; }

    /// The colours actually used for painting, with invalid entries derived
    /// from the connected area's palette.
    GutterColors effectiveColors() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private slots:
    void onViewportChanged(const ViewportState& vp);

private:
    QList<IMargin*> m_margins;
    CodeEditArea* m_area = nullptr;
    GutterColors m_colors;
    Qt::Edge m_textEdge;
    ViewportState m_vp;
    IMargin* m_hoveredMargin = nullptr;

    int totalWidth() const;
    void updateHoveredMargin(const QPoint& pos);
};

} // namespace qce
