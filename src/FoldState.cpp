#include "qce/FoldState.h"

#include <algorithm>

namespace qce {

// Comparator: (startLine, startColumn) ascending; ties broken by endLine
// descending so that outer regions come before their contents when scanning.
static bool foldLess(const FoldRegion& a, const FoldRegion& b) {
    if (a.startLine != b.startLine) return a.startLine < b.startLine;
    if (a.startColumn != b.startColumn) return a.startColumn < b.startColumn;
    return a.endLine > b.endLine;
}

// True iff `a` strictly contains `b` (b is inside a, not identical).
static bool contains(const FoldRegion& a, const FoldRegion& b) {
    // Same-start cases
    if (a.startLine == b.startLine && a.startColumn == b.startColumn
        && a.endLine == b.endLine && a.endColumn == b.endColumn) {
        return false; // identical
    }
    const bool startsAtOrBefore =
        (a.startLine < b.startLine) ||
        (a.startLine == b.startLine && a.startColumn <= b.startColumn);
    const bool endsAtOrAfter =
        (a.endLine > b.endLine) ||
        (a.endLine == b.endLine && a.endColumn >= b.endColumn);
    return startsAtOrBefore && endsAtOrAfter;
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

    // Compute depth: count ancestors that strictly contain this region.
    // Straightforward O(n^2). Fine for v1; tighten later with a sweep.
    for (int i = 0; i < out.size(); ++i) {
        int depth = 0;
        for (int j = 0; j < out.size(); ++j) {
            if (i == j) continue;
            if (contains(out[j], out[i])) ++depth;
        }
        out[i].depth = depth;
    }

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
