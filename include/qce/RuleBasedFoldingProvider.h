#pragma once

#include "IFoldingProvider.h"

#include <QHash>
#include <QString>

namespace qce {

class RulesHighlighter;

/// IFoldingProvider backed by a RulesHighlighter's beginRegion/endRegion
/// markers. Runs a fresh pass over the document, collecting fold events
/// from highlightLineEx, and pairs them using a Kate-style name-keyed stack.
///
/// Non-owning pointer to the highlighter. Caller must keep both alive and
/// in sync (i.e. calling setHighlighter with the same highlighter that is
/// feeding this provider).
class RuleBasedFoldingProvider : public IFoldingProvider {
public:
    explicit RuleBasedFoldingProvider(const RulesHighlighter* hl) : m_hl(hl) {}

    /// Set a placeholder template for regions with a given group (= Kate
    /// region name). Default "…" used when nothing is configured.
    void setPlaceholderFor(const QString& group, const QString& text) {
        m_placeholders.insert(group, text);
    }

    QVector<FoldRegion> computeRegions(const ITextDocument* doc) const override;

private:
    const RulesHighlighter* m_hl;
    QHash<QString, QString> m_placeholders;
};

} // namespace qce
