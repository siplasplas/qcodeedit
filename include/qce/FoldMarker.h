#pragma once

namespace qce {

/// A single fold-region open/close event reported by a highlighter or a
/// folding provider. Produced in two places:
/// - RulesHighlighter::highlightLineEx() emits markers per matched rule;
///   `column` is a QChar column in the input line, `length` is in QChars.
/// - IFoldingProvider::foldersInBytes() emits markers over a byte buffer;
///   `column` is a BYTE offset into the buffer, `length` is in bytes.
///
/// Callers pick one path per buffer and stay there. The unit is never
/// auto-detected; interpret `column`/`length` according to the producer.
///
/// A single rule may emit both an end event and a begin event in one
/// matched range (Kate's 'elif' idiom: close the previous branch, open
/// a new one). The end event is emitted first in that case.
struct FoldMarker {
    int  column;    ///< QChar column OR byte offset — depends on producer
    int  length;    ///< QChars OR bytes for the matched token
    int  regionId;  ///< region id from RulesHighlighter::regionIdForName()
    bool isBegin;   ///< true = opens a region; false = closes one
};

} // namespace qce
