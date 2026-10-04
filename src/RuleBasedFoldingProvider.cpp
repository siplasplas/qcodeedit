#include "qce/RuleBasedFoldingProvider.h"

#include "qce/ITextDocument.h"
#include "qce/RulesHighlighter.h"
#include "qce/Utf8Map.h"

#include <QHash>
#include <QString>

namespace qce {

// Pairs begin/end markers line by line with a Kate-style stack keyed by
// region id. Shared by computeRegions() and regionsFromLineMarkers().
class RuleBasedFoldingProvider::Pairer {
public:
    explicit Pairer(const RuleBasedFoldingProvider& p) : m_p(p) {}

    // Unclosed open regions at end of document are silently dropped (same as Kate).
    QVector<FoldRegion> take() { return std::move(m_regions); }

    void addLine(int li, const QVector<FoldMarker>& folds) {
        for (const FoldMarker& f : folds) {
            if (f.isBegin) {
                m_open.push_back({f.regionId, li, f.column});
            } else {
                // Close the most-recent open region with the same id.
                int idx = -1;
                for (int i = m_open.size() - 1; i >= 0; --i) {
                    if (m_open[i].regionId == f.regionId) { idx = i; break; }
                }
                if (idx < 0) continue; // unmatched end — ignore (Kate semantics)

                const Open o = m_open[idx];
                // Unbalanced open regions nested above are discarded.
                m_open.resize(idx);

                FoldRegion r;
                r.startLine   = o.startLine;
                r.startColumn = o.startColumn;
                r.endLine     = li;
                r.endColumn   = f.column + f.length;
                r.group       = groupName(f.regionId);
                const auto it = m_p.m_placeholders.find(r.group);
                r.placeholder = (it != m_p.m_placeholders.end())
                    ? *it : QStringLiteral("\u2026");
                m_regions.push_back(std::move(r));
            }
        }
    }

private:
    struct Open {
        int regionId;
        int startLine;
        int startColumn;
    };

    // Looked up once per region id rather than once per region.
    const QString& groupName(int id) {
        auto it = m_names.find(id);
        if (it == m_names.end()) it = m_names.insert(id, m_p.m_hl->regionNameById(id));
        return *it;
    }

    const RuleBasedFoldingProvider& m_p;
    QVector<Open>        m_open;
    QVector<FoldRegion>  m_regions;
    QHash<int, QString>  m_names;
};

QVector<FoldRegion> RuleBasedFoldingProvider::computeRegions(const ITextDocument* doc) const {
    if (!m_hl || !doc) return {};

    Pairer pairer(*this);
    HighlightState state = m_hl->initialState();
    QVector<StyleSpan>  spans;
    QVector<FoldMarker> folds;
    const int n = doc->lineCount();

    for (int li = 0; li < n; ++li) {
        HighlightState next;
        m_hl->highlightLineEx(doc->lineAt(li), state, spans, next, folds);
        state = next;
        pairer.addLine(li, folds);
    }
    return pairer.take();
}

bool RuleBasedFoldingProvider::regionsFromLineMarkers(
        const IHighlighter*                  hl,
        const QVector<QVector<FoldMarker>>& markersPerLine,
        QVector<FoldRegion>&                 regions) const {
    // Markers from another highlighter would carry other region ids.
    if (!m_hl || hl != static_cast<const IHighlighter*>(m_hl)) return false;
    Pairer pairer(*this);
    for (int li = 0; li < markersPerLine.size(); ++li) pairer.addLine(li, markersPerLine[li]);
    regions = pairer.take();
    return true;
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
