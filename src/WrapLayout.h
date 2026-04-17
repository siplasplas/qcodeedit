#pragma once

#include <QVector>
#include <QString>

namespace qce {

class ITextDocument;
class FoldState;

/// Maps visual rows to logical document positions for word-wrap mode.
/// Rebuilt whenever the document or viewport changes.
/// Each visual row covers one contiguous segment [startCol, endCol) of a
/// logical line. A logical line with no overflow produces exactly one row.
class WrapLayout {
public:
    struct Row {
        int logicalLine; ///< 0-based document line.
        int startCol;    ///< First logical column of this visual row.
        int endCol;      ///< One past last logical column (= line.size() on last row).
    };

    /// Rebuild from document. availableVisualCols is the number of character
    /// cells that fit in the viewport width (already excluding left padding).
    /// When foldState is non-null, lines it reports as not visible are skipped
    /// (they map to the previous visible line's first-row, so rowForCursor on
    /// a hidden line returns the row of the visible header of the collapsed
    /// region that contains it).
    void rebuild(const ITextDocument* doc, int availableVisualCols, int tabWidth,
                 const FoldState* foldState = nullptr);

    int totalRows() const { return m_rows.size(); }
    const Row& rowAt(int visualRow) const { return m_rows[visualRow]; }

    /// First visual row index for a given logical line.
    int firstRowOf(int logicalLine) const;

    /// Number of visual rows occupied by a logical line.
    int rowCountOf(int logicalLine) const;

    /// Visual row that contains cursor position (logicalLine, col).
    int rowForCursor(int logicalLine, int col) const;

private:
    QVector<Row> m_rows;
    QVector<int> m_lineFirstRow; ///< m_lineFirstRow[li] = index of first Row for line li.
};

} // namespace qce
