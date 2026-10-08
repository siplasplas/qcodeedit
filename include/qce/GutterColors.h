#pragma once

#include <QColor>

namespace qce {

/// Colours of the rails (gutters) beside CodeEditArea.
///
/// An invalid colour is derived from the area's palette, so the gutter
/// follows the editor background and text colours: a strip slightly darker
/// than the background (lighter on dark themes), muted line numbers and a
/// thin separator line next to the text. Kate themes provide all three as
/// editor-colors IconBorder, LineNumbers and Separator.
struct GutterColors {
    QColor background; ///< rail background
    QColor foreground; ///< line numbers and wrap arrows
    QColor separator;  ///< 1 px line on the edge next to the text
};

} // namespace qce
