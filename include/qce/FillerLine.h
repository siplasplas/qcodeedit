#pragma once

#include <QColor>
#include <QString>

namespace qce {

/// A block of virtual rows inserted before a specific logical document line.
/// Used by side-by-side diff views: when one panel has N extra lines the
/// other panel gets a filler block of the same rowCount so the remaining
/// lines stay aligned in screen coordinates.
///
/// Filler rows are visible but do NOT belong to the document: they are not
/// numbered, the caret cannot land on them, they don't take part in search
/// / selection, and cursor-navigation skips over them. Producers set
/// fillColor (soft red / green / blue to indicate add/remove/change) and
/// an optional label.
struct FillerLine {
    /// Insert this block BEFORE this logical line index. Pass
    /// doc->lineCount() to append at the very bottom.
    int beforeLine = 0;

    /// Number of visual rows this block occupies. Values <= 0 make the
    /// block a no-op (dropped during FillerState normalization).
    int rowCount = 1;

    /// Solid background for the whole block. Invalid QColor = no fill
    /// (unusual — normally you set a tint).
    QColor fillColor;

    /// Optional centred text drawn on the first filler row only.
    QString label;
};

} // namespace qce
