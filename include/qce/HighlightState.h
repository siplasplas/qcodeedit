#pragma once

#include <QVector>

namespace qce {

/// Opaque between-line state carried by a highlighter.
///
/// Represented as a stack of context ids (top = active context) so that
/// rule-based engines can model nested states such as "inside a string
/// that is inside a preprocessor directive". CodeEditArea treats this
/// struct as opaque and only uses operator== to detect when incremental
/// re-highlight can stop.
struct HighlightState {
    QVector<int> contextStack;

    bool operator==(const HighlightState& o) const noexcept {
        return contextStack == o.contextStack;
    }
    bool operator!=(const HighlightState& o) const noexcept {
        return !(*this == o);
    }
};

} // namespace qce
