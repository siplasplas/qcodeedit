#include "qce/FillerState.h"

#include <algorithm>

namespace qce {

void FillerState::setFillers(QVector<FillerLine> fillers) {
    // 1. Drop no-ops.
    QVector<FillerLine> out;
    out.reserve(fillers.size());
    for (const auto& f : fillers) {
        if (f.rowCount > 0 && f.beforeLine >= 0) out.push_back(f);
    }
    // 2. Sort by beforeLine ascending (stable — keeps input order among ties).
    std::stable_sort(out.begin(), out.end(),
        [](const FillerLine& a, const FillerLine& b) {
            return a.beforeLine < b.beforeLine;
        });
    // 3. Merge ties.
    QVector<FillerLine> merged;
    merged.reserve(out.size());
    for (auto& f : out) {
        if (!merged.isEmpty() && merged.last().beforeLine == f.beforeLine) {
            merged.last().rowCount += f.rowCount;
            if (!merged.last().fillColor.isValid()) merged.last().fillColor = f.fillColor;
            if (merged.last().label.isEmpty())      merged.last().label     = f.label;
        } else {
            merged.push_back(std::move(f));
        }
    }
    m_fillers = std::move(merged);
    m_total = 0;
    for (const auto& f : m_fillers) m_total += f.rowCount;
}

int FillerState::fillerRowsBeforeOrAt(int logicalLine) const {
    int sum = 0;
    for (const auto& f : m_fillers) {
        if (f.beforeLine > logicalLine) break;
        sum += f.rowCount;
    }
    return sum;
}

} // namespace qce
