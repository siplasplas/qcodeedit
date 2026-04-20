#pragma once

#include "FoldMarker.h"
#include "FoldRegion.h"
#include "HighlightState.h"

#include <QVector>

#include <cstddef>
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
///
/// Byte-range path (foldersInBytes): optional secondary entry point for
/// callers that don't have a full ITextDocument — e.g. a viewer of a
/// mmap'd multi-gigabyte file that wants fold markers for a bounded byte
/// window around its viewport. Markers are reported as raw (byte-offset)
/// FoldMarker events; the caller pairs them, typically via qce::pairFolds.
/// Default implementation produces no markers; providers that can
/// meaningfully tokenise bytes (currently RuleBasedFoldingProvider)
/// override this.
class IFoldingProvider {
public:
    virtual ~IFoldingProvider() = default;
    virtual QVector<FoldRegion> computeRegions(const ITextDocument* doc) const = 0;

    /// Emit fold markers for the UTF-8 byte range [data, data+len).
    ///   data     — pointer to the first byte; not null if len > 0
    ///   len      — number of bytes
    ///   stateIn  — highlighter state at the start of the range
    ///   markers  — OUT: FoldMarker events whose `column` is a byte
    ///              offset into `data` and whose `length` is in bytes.
    ///              Cleared by the callee before appending. Markers
    ///              never cross a line boundary (each marker sits
    ///              within a single line).
    ///   stateOut — OUT: highlighter state at the end of the range,
    ///              so the caller can stitch disjoint chunks.
    ///
    /// Default implementation is a no-op (empty markers). Providers
    /// that can tokenise a raw buffer override this.
    virtual void foldersInBytes(const char*           data,
                                qsizetype             len,
                                const HighlightState& stateIn,
                                QVector<FoldMarker>&  markers,
                                HighlightState&       stateOut) const;
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
