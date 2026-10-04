#pragma once

#include "FoldMarker.h"
#include "HighlightState.h"
#include "StyleSpan.h"
#include "TextAttribute.h"

#include <QString>
#include <QVector>

#include <cstddef>

namespace qce {

/// Abstract syntax-highlighting interface called by CodeEditArea per line.
///
/// Implementations are injected from outside the library (e.g. demo builds a
/// RulesHighlighter from a Kate XML file). The editor core never parses
/// configuration files — it only consumes this interface.
///
/// Two entry points:
/// - highlightLine()  — per-line, QString input, QChar-indexed spans.
///                      Used by CodeEditArea which stores the document as
///                      QStrings and re-highlights line-by-line.
/// - tokenizeBytes()  — arbitrary byte range, UTF-8 input, byte-indexed
///                      spans. Intended for viewers that mmap large files
///                      and need to tokenise a visible range without
///                      decoding it to QString first. Has a default
///                      implementation that splits on '\n' and delegates
///                      to highlightLine(); subclasses can override for a
///                      native byte path (see RulesHighlighter).
class IHighlighter {
public:
    virtual ~IHighlighter() = default;

    /// State at the beginning of the document (line 0 starts with this).
    virtual HighlightState initialState() const = 0;

    /// Highlight a single line.
    ///   line     — raw line text (without the terminating '\n')
    ///   stateIn  — state at the end of the previous line (or initialState()
    ///              for line 0)
    ///   spans    — OUT: non-overlapping, sorted spans with QChar-indexed
    ///              start/length. Must be cleared by the callee before
    ///              appending.
    ///   stateOut — OUT: state at the end of this line; consumed by the next
    ///              line. CodeEditArea stops re-highlighting when stateOut
    ///              equals the previously-cached end state for this line.
    virtual void highlightLine(const QString&        line,
                               const HighlightState& stateIn,
                               QVector<StyleSpan>&   spans,
                               HighlightState&       stateOut) const = 0;

    /// highlightLine() that also reports the line's fold markers (QChar
    /// columns; `folds` cleared by the callee). CodeEditArea keeps them per
    /// line so a folding provider can build regions without a second pass
    /// (IFoldingProvider::regionsFromLineMarkers). Default: highlightLine()
    /// with no markers.
    virtual void highlightLineWithFolds(const QString&        line,
                                        const HighlightState& stateIn,
                                        QVector<StyleSpan>&   spans,
                                        HighlightState&       stateOut,
                                        QVector<FoldMarker>&  folds) const;

    /// Tokenise an arbitrary UTF-8 byte range.
    ///   data     — pointer to the first byte; not null if len > 0
    ///   len      — number of bytes
    ///   stateIn  — state at the start of the range
    ///   spans    — OUT: spans whose start/length are BYTE offsets into
    ///              `data`. Cleared by the callee before appending.
    ///              Threading: for a range that contains embedded '\n'
    ///              bytes, spans never cross a line boundary.
    ///   stateOut — OUT: state at the end of the range
    ///
    /// Default implementation splits on '\n', decodes each line to QString,
    /// delegates to highlightLine(), and re-maps QChar spans to bytes via
    /// Utf8Map. Subclasses that can match directly on bytes should
    /// override this for efficiency.
    virtual void tokenizeBytes(const char*           data,
                               qsizetype             len,
                               const HighlightState& stateIn,
                               QVector<StyleSpan>&   spans,
                               HighlightState&       stateOut) const;

    /// Attribute palette: StyleSpan::attributeId indexes into this vector.
    /// Size is fixed after the highlighter is configured.
    virtual const QVector<TextAttribute>& attributes() const = 0;
};

} // namespace qce
