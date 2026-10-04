#include "qce/IFoldingProvider.h"

namespace qce {

void IFoldingProvider::foldersInBytes(const char*           /*data*/,
                                       qsizetype             /*len*/,
                                       const HighlightState& stateIn,
                                       QVector<FoldMarker>&  markers,
                                       HighlightState&       stateOut) const {
    // Default: no byte-range path. Providers that know how to tokenise
    // raw bytes override this.
    markers.clear();
    stateOut = stateIn;
}

bool IFoldingProvider::regionsFromLineMarkers(const IHighlighter*                  /*hl*/,
                                              const QVector<QVector<FoldMarker>>& /*markersPerLine*/,
                                              QVector<FoldRegion>&                 /*regions*/) const {
    return false;
}

} // namespace qce
