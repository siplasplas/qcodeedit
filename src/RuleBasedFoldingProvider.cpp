#include "qce/RuleBasedFoldingProvider.h"

#include "qce/ITextDocument.h"
#include "qce/RulesHighlighter.h"

namespace qce {

QVector<FoldRegion> RuleBasedFoldingProvider::computeRegions(const ITextDocument* doc) const {
    QVector<FoldRegion> regions;
    if (!m_hl || !doc) return regions;

    struct Open {
        int regionId;
        int startLine;
        int startColumn;
    };
    QVector<Open> open;

    HighlightState state = m_hl->initialState();
    QVector<StyleSpan>  spans;
    QVector<FoldMarker> folds;
    const int n = doc->lineCount();

    for (int li = 0; li < n; ++li) {
        spans.clear();
        folds.clear();
        HighlightState next;
        m_hl->highlightLineEx(doc->lineAt(li), state, spans, next, folds);
        state = next;

        for (const FoldMarker& f : folds) {
            if (f.isBegin) {
                open.push_back({f.regionId, li, f.column});
            } else {
                // Close the most-recent open region with the same id.
                int idx = -1;
                for (int i = open.size() - 1; i >= 0; --i) {
                    if (open[i].regionId == f.regionId) { idx = i; break; }
                }
                if (idx < 0) continue; // unmatched end — ignore (Kate semantics)

                const Open o = open[idx];
                // Unbalanced open regions nested above are discarded.
                open.resize(idx);

                FoldRegion r;
                r.startLine   = o.startLine;
                r.startColumn = o.startColumn;
                r.endLine     = li;
                r.endColumn   = f.column + f.length;
                r.group       = m_hl->regionNameById(f.regionId);
                const auto it = m_placeholders.find(r.group);
                r.placeholder = (it != m_placeholders.end())
                    ? *it : QStringLiteral("\u2026");
                regions.push_back(std::move(r));
            }
        }
    }
    // Unclosed open regions at end of document are silently dropped (same as Kate).
    return regions;
}

} // namespace qce
