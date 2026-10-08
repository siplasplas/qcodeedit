#include "WrapLayout.h"

#include <qce/FoldState.h>
#include <qce/ITextDocument.h>

#include <QTextBoundaryFinder>

namespace qce {

namespace {

// Positions inside `line` (0 < pos < size) where a row may start.
QVector<int> lineBreakOpportunities(const QString& line) {
    QVector<int> result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Line, line);
    for (qsizetype pos = finder.toNextBoundary(); pos > 0 && pos < line.size();
         pos = finder.toNextBoundary()) {
        if (finder.boundaryReasons() & QTextBoundaryFinder::BreakOpportunity)
            result.push_back(int(pos));
    }
    return result;
}

} // namespace

void WrapLayout::rebuild(const ITextDocument* doc,
                          int availableVisualCols,
                          int tabWidth,
                          const FoldState* foldState) {
    m_rows.clear();
    m_lineFirstRow.clear();

    if (!doc || availableVisualCols <= 0) return;

    const int n = doc->lineCount();
    m_lineFirstRow.reserve(n);
    m_rows.reserve(n + 16);

    // Hidden lines map to the first row of the last visible line before them
    // so that rowForCursor on a hidden cursor lands on the region's header.
    int lastVisibleFirstRow = 0;

    // Walk the sorted hidden ranges alongside the lines: O(lines + ranges)
    // instead of asking isLineVisible() (O(collapsed)) for every line.
    const QVector<QPair<int, int>> hidden =
        foldState ? foldState->hiddenLineRanges() : QVector<QPair<int, int>>();
    qsizetype nextHidden = 0;

    for (int li = 0; li < n; ++li) {
        while (nextHidden < hidden.size() && hidden[nextHidden].second < li) ++nextHidden;
        if (nextHidden < hidden.size() && hidden[nextHidden].first <= li) {
            m_lineFirstRow.push_back(lastVisibleFirstRow);
            continue;
        }
        lastVisibleFirstRow = m_rows.size();
        m_lineFirstRow.push_back(lastVisibleFirstRow);
        const QString line = doc->lineAt(li);

        if (line.isEmpty()) {
            m_rows.push_back({li, 0, 0});
            continue;
        }

        // Break opportunities by the Unicode line breaking rules (UAX #14),
        // as Kate does through QTextLayout: after spaces, but also e.g.
        // between "](" or after "/" and "-". Computed only for lines that
        // overflow.
        QVector<int> breaks;
        qsizetype nextBreak = 0;

        int col = 0;
        while (col < line.size()) {
            int visual = 0;
            int endCol = col;

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
            }

            if (endCol >= line.size()) {
                m_rows.push_back({li, col, static_cast<int>(line.size())});
                break;
            }

            if (breaks.isEmpty()) breaks = lineBreakOpportunities(line);
            // Spaces right after the last fitting character hang at the end
            // of this row instead of starting the next one.
            int hangEnd = endCol;
            while (hangEnd < line.size() && line.at(hangEnd) == QLatin1Char(' ')) ++hangEnd;

            // Last opportunity in (col, hangEnd], or a hard break at endCol.
            while (nextBreak < breaks.size() && breaks[nextBreak] <= col) ++nextBreak;
            int breakAt = endCol;
            for (qsizetype i = nextBreak; i < breaks.size() && breaks[i] <= hangEnd; ++i)
                breakAt = breaks[i];
            if (breakAt >= line.size()) {
                m_rows.push_back({li, col, static_cast<int>(line.size())});
                break;
            }
            m_rows.push_back({li, col, breakAt});
            col = breakAt;
        }
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
    // Last row whose startCol <= col.
    for (int r = next - 1; r >= first; --r) {
        if (m_rows[r].startCol <= col) return r;
    }
    return first;
}

} // namespace qce
