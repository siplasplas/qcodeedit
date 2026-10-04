#include "qce/FoldState.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace qce {

// Comparator: (startLine, startColumn) ascending; ties broken by end
// (endLine, endColumn) descending so that outer regions come before their
// contents when scanning. computeDepths() relies on this order.
static bool foldLess(const FoldRegion& a, const FoldRegion& b) {
    if (a.startLine != b.startLine) return a.startLine < b.startLine;
    if (a.startColumn != b.startColumn) return a.startColumn < b.startColumn;
    if (a.endLine != b.endLine) return a.endLine > b.endLine;
    return a.endColumn > b.endColumn;
}

// depth = number of regions that strictly contain the region (start at or
// before it and end at or after it, not identical). `regions` must be sorted
// by foldLess and free of exact duplicates. Then the containers of region i
// are exactly the earlier regions whose end is >= its end: an earlier region
// starts at or before it, and one with the same start has a larger end. The
// count of earlier ends below a value comes from a Fenwick tree over the
// distinct end positions: O(n log n), also for crossing regions.
static void computeDepths(QVector<FoldRegion>& regions) {
    using Pos = std::pair<int, int>;  // (endLine, endColumn)
    std::vector<Pos> ends;
    ends.reserve(regions.size());
    for (const FoldRegion& r : regions) ends.emplace_back(r.endLine, r.endColumn);
    std::sort(ends.begin(), ends.end());
    ends.erase(std::unique(ends.begin(), ends.end()), ends.end());

    std::vector<int> tree(ends.size() + 1, 0);  // 1-based Fenwick tree
    auto countBelow = [&](int rank) {           // inserted ends with rank < `rank`
        int sum = 0;
        for (int i = rank; i > 0; i -= i & -i) sum += tree[i];
        return sum;
    };
    for (int i = 0; i < regions.size(); ++i) {
        const Pos end(regions[i].endLine, regions[i].endColumn);
        const int rank = int(std::lower_bound(ends.begin(), ends.end(), end) - ends.begin());
        regions[i].depth = i - countBelow(rank);
        for (int j = rank + 1; j < int(tree.size()); j += j & -j) ++tree[j];
    }
}

void FoldState::setRegions(QVector<FoldRegion> regions) {
    // Drop single-line regions and sort.
    QVector<FoldRegion> out;
    out.reserve(regions.size());
    for (auto& r : regions) {
        if (r.startLine == r.endLine) continue;
        if (r.placeholder.isEmpty()) r.placeholder = QStringLiteral("\u2026");
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), foldLess);

    // Dedupe exact duplicates (after sort they are adjacent).
    out.erase(std::unique(out.begin(), out.end(),
        [](const FoldRegion& a, const FoldRegion& b) {
            return a.startLine == b.startLine && a.startColumn == b.startColumn
                && a.endLine == b.endLine && a.endColumn == b.endColumn;
        }), out.end());

    computeDepths(out);

    m_regions = std::move(out);
    m_collapsed.clear();

    // Apply collapsedByDefault.
    for (int i = 0; i < m_regions.size(); ++i) {
        if (m_regions[i].collapsedByDefault) m_collapsed.insert(i);
    }
}

void FoldState::setCollapsed(int regionIndex, bool collapsed) {
    if (regionIndex < 0 || regionIndex >= m_regions.size()) return;
    if (collapsed) m_collapsed.insert(regionIndex);
    else           m_collapsed.remove(regionIndex);
}

void FoldState::toggle(int regionIndex) {
    setCollapsed(regionIndex, !isCollapsed(regionIndex));
}

int FoldState::regionStartingAt(int line) const {
    // Regions are sorted by startLine ascending. Return first match.
    for (int i = 0; i < m_regions.size(); ++i) {
        if (m_regions[i].startLine == line) return i;
        if (m_regions[i].startLine > line) break;
    }
    return -1;
}

bool FoldState::isLineVisible(int line) const {
    // A line is hidden iff some collapsed region covers it with line in
    // (startLine, endLine]. The start line itself always remains visible
    // — the placeholder is drawn on it.
    for (int i : m_collapsed) {
        const FoldRegion& r = m_regions[i];
        if (line > r.startLine && line <= r.endLine) return false;
    }
    return true;
}

void FoldState::foldAll() {
    m_collapsed.clear();
    for (int i = 0; i < m_regions.size(); ++i) m_collapsed.insert(i);
}

void FoldState::unfoldAll() {
    m_collapsed.clear();
}

void FoldState::foldToLevel(int level) {
    m_collapsed.clear();
    for (int i = 0; i < m_regions.size(); ++i) {
        if (m_regions[i].depth <= level) m_collapsed.insert(i);
    }
}

} // namespace qce
