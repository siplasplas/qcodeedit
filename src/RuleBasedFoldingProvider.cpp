#include "qce/RuleBasedFoldingProvider.h"

#include "qce/ITextDocument.h"
#include "qce/RulesHighlighter.h"
#include "qce/Utf8Map.h"

#include <QString>

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

void RuleBasedFoldingProvider::foldersInBytes(const char*           data,
                                               qsizetype             len,
                                               const HighlightState& stateIn,
                                               QVector<FoldMarker>&  markers,
                                               HighlightState&       stateOut) const {
    markers.clear();
    stateOut = stateIn;
    if (!m_hl || len <= 0) return;

    QVector<StyleSpan>  spans;      // throwaway — we only want folds
    QVector<FoldMarker> lineFolds;
    Utf8Map             map;

    qsizetype i = 0;
    while (i < len) {
        qsizetype eol = i;
        while (eol < len && data[eol] != '\n') ++eol;

        const qsizetype segLen = eol - i;
        const QString line = QString::fromUtf8(data + i, segLen);

        map.buildFromUtf8(data + i, segLen);

        HighlightState before = stateOut;
        spans.clear();
        lineFolds.clear();
        m_hl->highlightLineEx(line, before, spans, stateOut, lineFolds);

        // Translate each fold event's QChar column/length to byte offsets
        // inside the line, then add the line's base byte offset.
        for (const FoldMarker& f : lineFolds) {
            const qsizetype b0 = map.byteForQChar(f.column);
            const qsizetype b1 = map.byteForQChar(f.column + f.length);
            markers.append(FoldMarker{
                static_cast<int>(i + b0),
                static_cast<int>(b1 - b0),
                f.regionId,
                f.isBegin,
            });
        }

        i = eol + (eol < len ? 1 : 0);
    }
}

} // namespace qce
