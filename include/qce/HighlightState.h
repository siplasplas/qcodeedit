#pragma once

#include <QStringList>
#include <QVector>

namespace qce {

/// Opaque between-line state carried by a highlighter.
///
/// Represented as a stack of context ids (top = active context) so that
/// rule-based engines can model nested states such as "inside a string
/// that is inside a preprocessor directive". CodeEditArea treats this
/// struct as opaque and only uses operator== to detect when incremental
/// re-highlight can stop.
///
/// captureStack runs parallel to contextStack: each level stores the
/// regex capture groups of the rule that pushed that context, so that
/// dynamic rules (Kate's dynamic="true") can expand %1, %2, ... against
/// the captures at the top of the stack. For rules that do not produce
/// captures (or for the initial context), the corresponding entry is an
/// empty QStringList. Invariant: captureStack.size() == contextStack.size().
struct HighlightState {
    QVector<int>         contextStack;
    QVector<QStringList> captureStack;

    bool operator==(const HighlightState& o) const noexcept {
        return contextStack == o.contextStack
            && captureStack == o.captureStack;
    }
    bool operator!=(const HighlightState& o) const noexcept {
        return !(*this == o);
    }
};

} // namespace qce
