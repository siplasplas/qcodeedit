#include "WrapLayout.h"

#include <qce/FillerLine.h>
#include <qce/FillerState.h>
#include <qce/FoldState.h>
#include <qce/ITextDocument.h>

namespace qce {

void WrapLayout::rebuild(const ITextDocument* doc,
                          int availableVisualCols,
                          int tabWidth,
                          const FoldState* foldState,
                          const FillerState* fillerState) {
    m_rows.clear();
    m_lineFirstRow.clear();

    if (!doc || availableVisualCols <= 0) return;

    const int n = doc->lineCount();
    m_lineFirstRow.reserve(n);
    m_rows.reserve(n + 16);

    // Filler iteration: blocks are sorted by beforeLine; we walk them in
    // lockstep with the document-line loop, flushing any with beforeLine <= li
    // right before emitting the corresponding content rows.
    const QVector<FillerLine>* fillers = fillerState ? &fillerState->fillers() : nullptr;
    int fillerIdx = 0;
    auto emitFiller = [&](int blockIdx) {
        const int rows = (*fillers)[blockIdx].rowCount;
        for (int k = 0; k < rows; ++k) {
            Row r;
            r.logicalLine = -1;
            r.fillerBlockIndex = blockIdx;
            m_rows.push_back(r);
        }
    };

    // Hidden lines map to the first row of the last visible line before them
    // so that rowForCursor on a hidden cursor lands on the region's header.
    int lastVisibleFirstRow = 0;

    for (int li = 0; li < n; ++li) {
        // Emit any filler block that lands right before this logical line.
        while (fillers && fillerIdx < fillers->size()
               && (*fillers)[fillerIdx].beforeLine <= li) {
            emitFiller(fillerIdx);
            ++fillerIdx;
        }
        if (foldState && !foldState->isLineVisible(li)) {
            m_lineFirstRow.push_back(lastVisibleFirstRow);
            continue;
        }
        lastVisibleFirstRow = m_rows.size();
        m_lineFirstRow.push_back(lastVisibleFirstRow);
        const QString line = doc->lineAt(li);

        if (line.isEmpty()) {
            m_rows.push_back({li, 0, 0, -1});
            continue;
        }

        int col = 0;
        while (col < line.size()) {
            int visual = 0;
            int endCol = col;
            int breakAfter = -1; // last break-after-space position

            while (endCol < line.size()) {
                const QChar ch = line.at(endCol);
                const int cw = (ch == QLatin1Char('\t'))
                    ? (tabWidth - (visual % tabWidth))
                    : 1;
                if (visual + cw > availableVisualCols && endCol > col) {
                    break;
                }
                visual += cw;
                ++endCol;
                if (ch.isSpace()) {
                    breakAfter = endCol; // include the space in this row
                }
            }

            if (endCol >= line.size()) {
                m_rows.push_back({li, col, (int)line.size(), -1});
                break;
            }

            // Choose break point: after the last whitespace, or hard-break.
            const int breakAt = (breakAfter > col) ? breakAfter : endCol;
            m_rows.push_back({li, col, breakAt, -1});
            col = breakAt;
        }
    }

    // Trailing fillers: beforeLine >= lineCount (append at end of document).
    while (fillers && fillerIdx < fillers->size()) {
        emitFiller(fillerIdx);
        ++fillerIdx;
    }
}

int WrapLayout::firstRowOf(int logicalLine) const {
    if (logicalLine < 0 || logicalLine >= m_lineFirstRow.size()) return 0;
    return m_lineFirstRow[logicalLine];
}

int WrapLayout::rowCountOf(int logicalLine) const {
    if (logicalLine < 0 || logicalLine >= m_lineFirstRow.size()) return 0;
    const int first = m_lineFirstRow[logicalLine];
    const int next  = (logicalLine + 1 < m_lineFirstRow.size())
                      ? m_lineFirstRow[logicalLine + 1]
                      : m_rows.size();
    return next - first;
}

int WrapLayout::rowForCursor(int logicalLine, int col) const {
    if (logicalLine < 0 || logicalLine >= m_lineFirstRow.size()) return 0;
    const int first = m_lineFirstRow[logicalLine];
    const int next  = (logicalLine + 1 < m_lineFirstRow.size())
                      ? m_lineFirstRow[logicalLine + 1]
                      : m_rows.size();
    // Last row whose startCol <= col. Skip filler rows (they can be
    // interleaved at the end of [first, next) if a filler block precedes
    // the next logical line).
    for (int r = next - 1; r >= first; --r) {
        if (m_rows[r].logicalLine < 0) continue;
        if (m_rows[r].startCol <= col) return r;
    }
    return first;
}

} // namespace qce
