#pragma once

#include "TextCursor.h"
#include <QColor>

namespace qce {

/// Caller-owned decoration in logical UTF-16 coordinates, half-open [start, end).
/// Invalid colors preserve the underlying syntax/default color.
struct ExtraSelection {
    TextCursor start;
    TextCursor end;
    QColor background;
    QColor foreground;
    /// Wavy line under the text in this color, e.g. a misspelled word; invalid
    /// = none.
    QColor underline;
};

} // namespace qce
