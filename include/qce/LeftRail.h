#pragma once

#include <qce/Rail.h>

namespace qce {

/// Rail placed to the left of CodeEditArea.
/// Hosts margins such as line-number gutters and fold indicators.
/// The vertical scrollbar can be moved here via CodeEdit::setScrollBarSide
/// (section 6.5).
class LeftRail : public Rail {
    Q_OBJECT
public:
    explicit LeftRail(QWidget* parent = nullptr) : Rail(parent) {}
};

} // namespace qce
