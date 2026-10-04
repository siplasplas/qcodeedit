#pragma once

#include "FoldRegion.h"

#include <QPair>
#include <QSet>
#include <QVector>

namespace qce {

/// Editor-owned state that tracks which fold regions are collapsed and which
/// document lines are currently visible.
///
/// - setRegions() normalizes the input: drops single-line regions, sorts by
///   (startLine, startColumn), dedupes identical ranges, and annotates depth.
/// - The collapsed set is keyed by region index. When regions are replaced,
///   a new region takes over the collapsed state of the old region with the
///   same start line and group (several on one line are paired in column
///   order); regions without such a match start from collapsedByDefault.
///   After an edit that inserts or removes lines, call shiftLines() first so
///   the old regions line up with the new text. Use clear() to forget the
///   state (new document, new provider).
class FoldState {
public:
    void setRegions(QVector<FoldRegion> regions);
    const QVector<FoldRegion>& regions() const { return m_regions; }

    /// Drop all regions and collapsed state.
    void clear();

    /// Move the current regions after `delta` lines were inserted at `line`
    /// (delta > 0) or the lines [line, line - delta) were removed (delta < 0).
    /// Regions starting in removed lines are dropped.
    void shiftLines(int line, int delta);

    /// Expand every collapsed region that hides `line`. Returns true if any
    /// region changed.
    bool expandContaining(int line);

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
    /// inside any collapsed region). O(collapsed regions); for whole-document
    /// passes use hiddenLineRanges().
    bool isLineVisible(int line) const;

    /// Hidden lines as sorted, disjoint, non-adjacent inclusive ranges
    /// [first, last] (a collapsed region hides startLine+1 .. endLine).
    QVector<QPair<int, int>> hiddenLineRanges() const;

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
