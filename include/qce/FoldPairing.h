#pragma once

#include "FoldMarker.h"

#include <QVector>

#include <cstddef>

namespace qce {

/// A paired fold region in byte coordinates, matching a begin/end marker
/// pair. Produced by pairFolds().
///
/// [beginByte, endByte) — half-open range; beginByte is the first byte of
/// the opener token, endByte is one past the last byte of the closer
/// token.
struct FoldRange {
    qsizetype beginByte = 0;   ///< inclusive
    qsizetype endByte   = 0;   ///< exclusive
    int       regionId  = -1;
    int       depth     = 0;   ///< nesting depth at the matching point
};

/// Pair begin/end FoldMarker events by regionId, Kate-style (most-recent
/// open wins; unmatched opens above a matched one are discarded).
///
/// Input is expected to be in byte order (producers — e.g.
/// IFoldingProvider::foldersInBytes — emit markers line-by-line so they
/// are already sorted). The function is position-agnostic and does not
/// re-sort.
///
/// `maxSpanBytes` is the per-region budget: any pair whose span
/// (endByte - beginByte) exceeds this is dropped from the result. This
/// is how viewers over huge files skip folds whose close lies beyond
/// the scanned window — the gigantic root element that the user can't
/// usefully collapse.
///
/// A marker is said to "cross a chunk boundary" when it belongs to a
/// pair whose other half was not included in `markers`. Unmatched opens
/// are dropped; unmatched ends are ignored. Callers that tokenise
/// adjacent chunks and want pairing across them should concatenate the
/// marker lists first, then call pairFolds() once on the union.
QVector<FoldRange> pairFolds(const QVector<FoldMarker>& markers,
                             qsizetype maxSpanBytes);

} // namespace qce
