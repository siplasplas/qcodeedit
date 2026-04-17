#pragma once

namespace qce {

/// A contiguous range of characters in a single line that share one attribute.
/// Produced by IHighlighter::highlightLine() and consumed by LineRenderer.
///
/// Spans for a single line are sorted by `start` and do not overlap. Positions
/// in the line that fall outside any span are rendered with the editor's
/// default foreground color (no attribute).
struct StyleSpan {
    int start       = 0;   ///< first QChar index in the line (logical column)
    int length      = 0;   ///< number of QChars; 0 is permitted but ignored
    int attributeId = -1;  ///< index into palette; -1 = no attribute (default color)
};

} // namespace qce
