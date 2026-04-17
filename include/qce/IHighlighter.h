#pragma once

#include "HighlightState.h"
#include "StyleSpan.h"
#include "TextAttribute.h"

#include <QString>
#include <QVector>

namespace qce {

/// Abstract syntax-highlighting interface called by CodeEditArea per line.
///
/// Implementations are injected from outside the library (e.g. demo builds a
/// RulesHighlighter from a Kate XML file). The editor core never parses
/// configuration files — it only consumes this interface.
class IHighlighter {
public:
    virtual ~IHighlighter() = default;

    /// State at the beginning of the document (line 0 starts with this).
    virtual HighlightState initialState() const = 0;

    /// Highlight a single line.
    ///   line     — raw line text (without the terminating '\n')
    ///   stateIn  — state at the end of the previous line (or initialState()
    ///              for line 0)
    ///   spans    — OUT: non-overlapping, sorted spans covering parts of the
    ///              line. Must be cleared by the callee before appending.
    ///   stateOut — OUT: state at the end of this line; consumed by the next
    ///              line. CodeEditArea stops re-highlighting when stateOut
    ///              equals the previously-cached end state for this line.
    virtual void highlightLine(const QString&        line,
                               const HighlightState& stateIn,
                               QVector<StyleSpan>&   spans,
                               HighlightState&       stateOut) const = 0;

    /// Attribute palette: StyleSpan::attributeId indexes into this vector.
    /// Size is fixed after the highlighter is configured.
    virtual const QVector<TextAttribute>& attributes() const = 0;
};

} // namespace qce
