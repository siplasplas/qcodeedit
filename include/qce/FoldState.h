#pragma once

#include "FoldRegion.h"

#include <QSet>
#include <QVector>

namespace qce {

/// Editor-owned state that tracks which fold regions are collapsed and which
/// document lines are currently visible.
///
/// - setRegions() normalizes the input: drops single-line regions, sorts by
///   (startLine, startColumn), dedupes identical ranges, and annotates depth.
/// - The collapsed set is keyed by region index. When regions are replaced
///   the old set is cleared; callers wanting to preserve user state across
///   reparses should migrate manually (upcoming stage H).
class FoldState {
public:
    void setRegions(QVector<FoldRegion> regions);
    const QVector<FoldRegion>& regions() const { return m_regions; }

    bool isCollapsed(int regionIndex) const {
        return m_collapsed.contains(regionIndex);
    }
    void setCollapsed(int regionIndex, bool collapsed);
    void toggle(int regionIndex);

    /// Index of a region starting on `line`, or -1 if none. When multiple
    /// regions start on the same line, the one with the smallest startColumn
    /// is returned.
    int regionStartingAt(int line) const;

    /// Returns true if the given line is currently visible (i.e. not hidden
    /// inside any collapsed region).
    bool isLineVisible(int line) const;

    void foldAll();
    void unfoldAll();

    /// Collapse regions up to (and including) `level` depth; leave deeper
    /// ones expanded.
    void foldToLevel(int level);

private:
    QVector<FoldRegion> m_regions;   ///< sorted, depth-annotated
    QSet<int>           m_collapsed; ///< region indices that are currently collapsed
};

} // namespace qce
