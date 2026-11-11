#pragma once

#include <algorithm>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/algorithms/gk/model/key.h"

namespace gk {

// Eq is represented by disjoint sets of the original entity IDs. This changes storage,
// not the mathematical relation: AreEquivalent(a, b) means (a, b) belongs to Eq.
class EquivalenceRelation {
public:
    using Pair = std::pair<model::EntityId, model::EntityId>;
    using Relation = std::set<Pair>;
    using Classes = std::vector<std::vector<model::EntityId>>;

    EquivalenceRelation() = default;

    explicit EquivalenceRelation(std::vector<model::EntityId> const& entities);

    EquivalenceRelation(EquivalenceRelation const&) = default;
    EquivalenceRelation& operator=(EquivalenceRelation const& other);
    // A moved-from relation is the empty identity relation and can be reused.
    EquivalenceRelation(EquivalenceRelation&& other) noexcept;
    EquivalenceRelation& operator=(EquivalenceRelation&& other) noexcept;

    void AddEntity(model::EntityId const& entity);

    void AddEntities(std::vector<model::EntityId> const& entities);

    bool AreEquivalent(model::EntityId const& first, model::EntityId const& second) const;

    // True only when two previously distinct classes are joined.
    bool Merge(model::EntityId const& first, model::EntityId const& second);

    std::size_t GetEntityCount() const noexcept;

    std::size_t GetClassCount() const noexcept;

    bool IsIdentity() const noexcept {
        return class_count_ == entities_.size();
    }

    Classes GetClasses() const;

    // Explicit ordered pairs for compatibility and small diagnostics only. Materialization
    // costs sum(|class|^2); algorithms use AreEquivalent instead. The cached view is refreshed
    // on the next GetRelation call after a mutation and is not a concurrent-read API.
    Relation const& GetRelation() const;

private:
    std::unordered_map<model::EntityId, std::size_t> indices_;
    std::vector<model::EntityId> entities_;
    std::vector<std::size_t> parents_;
    std::vector<std::size_t> sizes_;
    std::size_t class_count_ = 0;
    mutable Relation relation_;
    mutable bool relation_valid_ = false;

    void Swap(EquivalenceRelation& other) noexcept;

    // Const queries do not compress paths, so independent EvalGK calls can share a frozen Eq.
    std::size_t FindRoot(std::size_t index) const;

    std::size_t CompressRoot(std::size_t index);
};

}  // namespace gk
