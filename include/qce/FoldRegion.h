#pragma once

#include <QString>

namespace qce {

/// One foldable region in a document. Produced by an IFoldingProvider and
/// consumed by the editor (FoldState).
///
/// Single-line regions (startLine == endLine) are legal but ignored by the
/// editor — there is nothing to hide when collapsing them. The provider may
/// still emit them; the editor filters at FoldState::setRegions() time.
struct FoldRegion {
    int     startLine          = 0;   ///< 0-based line of the opening marker
    int     startColumn        = 0;   ///< column of the opening marker
    int     endLine            = 0;   ///< 0-based line of the closing marker
    int     endColumn          = 0;   ///< one past the last char of the closing marker
    QString placeholder;              ///< text shown in-line when collapsed; default "…"
    bool    collapsedByDefault = false;
    QString group;                    ///< descriptive tag: "curly", "Comment", ...
    int     depth              = 0;   ///< computed by FoldState; providers may ignore
};

} // namespace qce
