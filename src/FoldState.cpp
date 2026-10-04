#include "qce/FoldState.h"

#include <QHash>
#include <QPair>

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

    // Carry the collapsed state over from the old regions: key (startLine,
    // group); several regions on one line are paired in column order, which
    // both lists already have.
    QHash<QPair<int, QString>, QList<bool>> previous;
    for (int i = 0; i < m_regions.size(); ++i) {
        const FoldRegion& r = m_regions[i];
        previous[{r.startLine, r.group}].append(m_collapsed.contains(i));
    }

    m_regions = std::move(out);
    m_collapsed.clear();
    for (int i = 0; i < m_regions.size(); ++i) {
        const FoldRegion& r = m_regions[i];
        auto it = previous.find({r.startLine, r.group});
        if (it != previous.end() && !it->isEmpty()) {
            if (it->takeFirst()) m_collapsed.insert(i);
        } else if (r.collapsedByDefault) {
            m_collapsed.insert(i);
        }
    }
}

void FoldState::clear() {
    m_regions.clear();
    m_collapsed.clear();
}

void FoldState::shiftLines(int line, int delta) {
    if (delta == 0 || m_regions.isEmpty()) return;
    const int removedEnd = delta < 0 ? line - delta : line;  // [line, removedEnd) removed
    QVector<FoldRegion> kept;
    kept.reserve(m_regions.size());
    QSet<int> collapsed;
    for (int i = 0; i < m_regions.size(); ++i) {
        FoldRegion r = m_regions[i];
        if (delta < 0 && r.startLine >= line && r.startLine < removedEnd) continue;
        if (r.startLine >= line) r.startLine += delta;
        if (r.endLine >= removedEnd)  r.endLine += delta;
        else if (r.endLine >= line)   r.endLine = line - 1;  // end was removed
        r.endLine = qMax(r.endLine, r.startLine);
        if (m_collapsed.contains(i)) collapsed.insert(kept.size());
        kept.push_back(std::move(r));
    }
    m_regions = std::move(kept);
    m_collapsed = std::move(collapsed);
}

bool FoldState::expandContaining(int line) {
    bool changed = false;
    for (auto it = m_collapsed.begin(); it != m_collapsed.end();) {
        const FoldRegion& r = m_regions[*it];
        if (line > r.startLine && line <= r.endLine) {
            it = m_collapsed.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    return changed;
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
    // Regions are sorted by (startLine, startColumn): the first region on the
    // line is the lower bound. Called per visible row while painting.
    const auto it = std::lower_bound(m_regions.cbegin(), m_regions.cend(), line,
        [](const FoldRegion& r, int l) { return r.startLine < l; });
    return (it != m_regions.cend() && it->startLine == line)
        ? int(it - m_regions.cbegin()) : -1;
}

QVector<QPair<int, int>> FoldState::hiddenLineRanges() const {
    QVector<QPair<int, int>> ranges;
    ranges.reserve(m_collapsed.size());
    for (int i : m_collapsed) {
        const FoldRegion& r = m_regions[i];
        if (r.endLine > r.startLine) ranges.append({r.startLine + 1, r.endLine});
    }
    std::sort(ranges.begin(), ranges.end());
    QVector<QPair<int, int>> merged;
    for (const auto& range : std::as_const(ranges)) {
        if (!merged.isEmpty() && range.first <= merged.last().second + 1)
            merged.last().second = qMax(merged.last().second, range.second);
        else
            merged.append(range);
    }
    return merged;
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
