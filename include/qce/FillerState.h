#pragma once

#include "FillerLine.h"

#include <QVector>

namespace qce {

/// Editor-owned container for the filler blocks that should be rendered in
/// this document view. Analogous to FoldState: the application mutates it
/// and tells CodeEditArea to refresh; the editor queries it from WrapLayout
/// and from the scrollbar-range computation.
class FillerState {
public:
    /// Replace the list. Blocks with rowCount <= 0 are dropped; the rest is
    /// sorted by beforeLine ascending; duplicates with identical beforeLine
    /// are merged (rowCount summed, first non-null fillColor wins, first
    /// non-empty label wins).
    void setFillers(QVector<FillerLine> fillers);

    const QVector<FillerLine>& fillers() const { return m_fillers; }

    /// Sum of rowCount across all blocks.
    int totalFillerRows() const { return m_total; }

    /// Sum of rowCount for blocks with beforeLine <= logicalLine.
    /// Useful when mapping logical lines to absolute visual-row indices.
    int fillerRowsBeforeOrAt(int logicalLine) const;

private:
    QVector<FillerLine> m_fillers;   ///< sorted, merged
    int                 m_total = 0;
};

} // namespace qce
