#pragma once

#include "FoldRegion.h"

#include <QVector>

#include <memory>

namespace qce {

class ITextDocument;

/// Abstract source of fold regions. The editor core stores a non-owning
/// pointer and calls computeRegions() whenever the document changes. The
/// implementation decides how to compute — from a rule engine, from a parser,
/// from manual annotations, etc.
///
/// The returned vector does not have to be sorted or depth-annotated: the
/// editor (FoldState) sorts, filters single-line regions, and computes depth.
class IFoldingProvider {
public:
    virtual ~IFoldingProvider() = default;
    virtual QVector<FoldRegion> computeRegions(const ITextDocument* doc) const = 0;
};

/// Combines several providers into one. Regions from all children are
/// concatenated; duplicates (same start+end) are left to FoldState to dedup.
class CompositeFoldingProvider : public IFoldingProvider {
public:
    void add(std::unique_ptr<IFoldingProvider> p) {
        if (p) m_providers.push_back(std::move(p));
    }
    int size() const { return (int)m_providers.size(); }

    QVector<FoldRegion> computeRegions(const ITextDocument* doc) const override {
        QVector<FoldRegion> all;
        for (const auto& p : m_providers) {
            all.append(p->computeRegions(doc));
        }
        return all;
    }

private:
    std::vector<std::unique_ptr<IFoldingProvider>> m_providers;
};

} // namespace qce
