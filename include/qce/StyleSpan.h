#pragma once

namespace qce {

/// A contiguous range of characters in a single line that share one attribute.
///
/// Two producers, two position units:
/// - IHighlighter::highlightLine() emits spans with `start`/`length` in
///   QChar indices (UTF-16 code units).
/// - IHighlighter::tokenizeBytes() emits spans with `start`/`length` in
///   UTF-8 byte offsets into the input buffer.
///
/// Callers pick one path per buffer and stay there. The unit is never
/// auto-detected; interpret `start`/`length` according to the producer.
///
/// Spans for a single line are sorted by `start` and do not overlap. Positions
/// in the line that fall outside any span are rendered with the editor's
/// default foreground color (no attribute).
struct StyleSpan {
    int start       = 0;   ///< QChar index OR byte offset — depends on producer
    int length      = 0;   ///< QChars OR bytes; 0 is permitted but ignored
    int attributeId = -1;  ///< index into palette; -1 = no attribute (default color)
};

} // namespace qce
