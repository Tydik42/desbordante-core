#pragma once

#include <algorithm>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

// Eq is represented by disjoint sets of the original entity IDs. This changes storage,
// not the mathematical relation: AreEquivalent(a, b) means (a, b) belongs to Eq.
class EquivalenceRelation {
public:
    using Pair = std::pair<model::EntityId, model::EntityId>;
    using Relation = std::set<Pair>;
    using Classes = std::vector<std::vector<model::EntityId>>;

    EquivalenceRelation() = default;

    explicit EquivalenceRelation(std::vector<model::EntityId> const& entities) {
        AddEntities(entities);
    }

    void AddEntity(model::EntityId const& entity) {
        std::size_t const index = entities_.size();
        if (!indices_.emplace(entity, index).second) return;
        entities_.push_back(entity);
        parents_.push_back(index);
        sizes_.push_back(1);
        ++class_count_;
        relation_valid_ = false;
    }

    void AddEntities(std::vector<model::EntityId> const& entities) {
        for (auto const& entity : entities) AddEntity(entity);
    }

    bool AreEquivalent(model::EntityId const& first, model::EntityId const& second) const {
        auto const lhs = indices_.find(first);
        auto const rhs = indices_.find(second);
        return lhs != indices_.end() && rhs != indices_.end() &&
               FindRoot(lhs->second) == FindRoot(rhs->second);
    }

    // True only when two previously distinct classes are joined.
    bool Merge(model::EntityId const& first, model::EntityId const& second) {
        AddEntity(first);
        AddEntity(second);
        std::size_t lhs = CompressRoot(indices_.at(first));
        std::size_t rhs = CompressRoot(indices_.at(second));
        if (lhs == rhs) return false;
        if (sizes_[lhs] < sizes_[rhs]) std::swap(lhs, rhs);
        parents_[rhs] = lhs;
        sizes_[lhs] += sizes_[rhs];
        --class_count_;
        relation_valid_ = false;
        return true;
    }

    std::size_t GetEntityCount() const noexcept {
        return entities_.size();
    }

    std::size_t GetClassCount() const noexcept {
        return class_count_;
    }

    Classes GetClasses() const {
        Classes classes;
        classes.reserve(class_count_);
        std::unordered_map<std::size_t, std::size_t> groups;
        for (std::size_t index = 0; index < entities_.size(); ++index) {
            auto [it, inserted] = groups.emplace(FindRoot(index), classes.size());
            if (inserted) classes.emplace_back();
            classes[it->second].push_back(entities_[index]);
        }
        for (auto& group : classes) std::sort(group.begin(), group.end());
        std::sort(classes.begin(), classes.end());
        return classes;
    }

    // Explicit ordered pairs for compatibility and small diagnostics only. Materialization
    // costs sum(|class|^2); algorithms use AreEquivalent instead. The cached view is refreshed
    // on the next GetRelation call after a mutation and is not a concurrent-read API.
    Relation const& GetRelation() const {
        if (!relation_valid_) {
            relation_.clear();
            for (auto const& group : GetClasses()) {
                for (auto const& lhs : group) {
                    for (auto const& rhs : group) relation_.emplace(lhs, rhs);
                }
            }
            relation_valid_ = true;
        }
        return relation_;
    }

private:
    std::unordered_map<model::EntityId, std::size_t> indices_;
    std::vector<model::EntityId> entities_;
    std::vector<std::size_t> parents_;
    std::vector<std::size_t> sizes_;
    std::size_t class_count_ = 0;
    mutable Relation relation_;
    mutable bool relation_valid_ = false;

    // Const queries do not compress paths, so independent EvalGK calls can share a frozen Eq.
    std::size_t FindRoot(std::size_t index) const {
        while (parents_[index] != index) index = parents_[index];
        return index;
    }

    std::size_t CompressRoot(std::size_t index) {
        std::size_t const root = FindRoot(index);
        while (parents_[index] != index) {
            std::size_t const next = parents_[index];
            parents_[index] = root;
            index = next;
        }
        return root;
    }
};

}  // namespace gk
