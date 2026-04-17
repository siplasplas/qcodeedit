#pragma once

#include <qce/Rail.h>

namespace qce {

/// Rail placed to the right of CodeEditArea.
/// Hosts margins such as scroll-overview bars or minimap.
class RightRail : public Rail {
    Q_OBJECT
public:
    explicit RightRail(QWidget* parent = nullptr) : Rail(parent) {}
};

} // namespace qce
