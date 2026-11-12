#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/algorithms/gk_compact/model/graph.h"

namespace gk_compact {

// Eq over the original entity IDs [0, entity_count), initially Eq_0. Merging
// classes never renumbers graph vertices or changes embedding injectivity.
// Eq_0 stores only counts. DSU arrays appear at the first nontrivial Merge.
class EquivalenceRelation {
public:
    explicit EquivalenceRelation(std::size_t entity_count);
    // Shared metadata enables a conservative identity fast path per type.
    // Without it, only global Eq_0 can be recognized.
    explicit EquivalenceRelation(std::shared_ptr<GraphMetadata const> metadata);
    EquivalenceRelation(EquivalenceRelation const&) = default;
    EquivalenceRelation& operator=(EquivalenceRelation const& other);
    // A moved-from relation is the empty identity relation.
    EquivalenceRelation(EquivalenceRelation&& other) noexcept;
    EquivalenceRelation& operator=(EquivalenceRelation&& other) noexcept;

    bool AreEquivalent(VertexId first, VertexId second) const;

    // A read-only class representative, valid until the next Merge. Useful for
    // grouping only active entities without materializing classes for the full domain.
    VertexId GetRepresentative(VertexId vertex) const {
        return FindRoot(vertex);
    }

    bool Merge(VertexId first, VertexId second);
    std::vector<std::vector<VertexId>> GetClasses() const;

    std::size_t GetEntityCount() const noexcept {
        return entity_count_;
    }

    std::size_t GetClassCount() const noexcept {
        return class_count_;
    }

    // Classes only merge, so the original class count exactly identifies Eq_0.
    bool IsIdentity() const noexcept {
        return class_count_ == GetEntityCount();
    }

    // True guarantees that two entities of this type are equivalent only when
    // their IDs agree. False may be conservative, including after cross-type merges.
    bool IsIdentity(TypeId type) const noexcept {
        return IsIdentity() || (type < merged_types_.size() && !merged_types_[type]);
    }

private:
    std::vector<VertexId> parents_;
    std::vector<std::uint32_t> sizes_;
    std::shared_ptr<GraphMetadata const> metadata_;
    std::vector<std::uint8_t> merged_types_;
    std::size_t entity_count_;
    std::size_t class_count_;

    // Const queries do not mutate paths: a frozen Eq can be read by independent
    // evaluators. Merge requires exclusive access and compresses its paths.
    VertexId FindRoot(VertexId vertex) const;
    VertexId CompressRoot(VertexId vertex);
};

}  // namespace gk_compact
