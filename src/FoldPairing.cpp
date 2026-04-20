#include "qce/FoldPairing.h"

namespace qce {

QVector<FoldRange> pairFolds(const QVector<FoldMarker>& markers,
                              qsizetype maxSpanBytes) {
    QVector<FoldRange> out;
    struct Open {
        qsizetype beginByte;
        int       regionId;
    };
    QVector<Open> stack;

    for (const FoldMarker& m : markers) {
        if (m.isBegin) {
            stack.push_back({static_cast<qsizetype>(m.column), m.regionId});
            continue;
        }
        // isBegin == false: find the most-recent open with a matching
        // regionId, Kate-style. Opens above it are unbalanced and dropped.
        int idx = -1;
        for (int i = stack.size() - 1; i >= 0; --i) {
            if (stack[i].regionId == m.regionId) { idx = i; break; }
        }
        if (idx < 0) continue;  // unmatched end — ignore

        const Open o = stack[idx];
        stack.resize(idx);

        const qsizetype endByte =
            static_cast<qsizetype>(m.column) + static_cast<qsizetype>(m.length);
        const qsizetype span = endByte - o.beginByte;
        if (span < 0 || span > maxSpanBytes) continue;  // over budget — drop

        FoldRange r;
        r.beginByte = o.beginByte;
        r.endByte   = endByte;
        r.regionId  = m.regionId;
        r.depth     = idx;
        out.push_back(r);
    }
    // Unmatched opens at the end of the stream — dropped.
    return out;
}

} // namespace qce
