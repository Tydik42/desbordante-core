#include "core/algorithms/gk/model/equivalence_relation.h"

#include <numeric>

namespace gk {

EquivalenceRelation::EquivalenceRelation(std::vector<model::EntityId> const& entities) {
    AddEntities(entities);
}

EquivalenceRelation& EquivalenceRelation::operator=(EquivalenceRelation const& other) {
    if (this != &other) {
        EquivalenceRelation copy(other);
        Swap(copy);
    }
    return *this;
}

EquivalenceRelation::EquivalenceRelation(EquivalenceRelation&& other) noexcept {
    Swap(other);
}

EquivalenceRelation& EquivalenceRelation::operator=(EquivalenceRelation&& other) noexcept {
    if (this != &other) {
        EquivalenceRelation moved(std::move(other));
        Swap(moved);
    }
    return *this;
}

void EquivalenceRelation::Swap(EquivalenceRelation& other) noexcept {
    indices_.swap(other.indices_);
    entities_.swap(other.entities_);
    parents_.swap(other.parents_);
    sizes_.swap(other.sizes_);
    std::swap(class_count_, other.class_count_);
    relation_.swap(other.relation_);
    std::swap(relation_valid_, other.relation_valid_);
}

void EquivalenceRelation::AddEntity(model::EntityId const& entity) {
    bool const materialized = !IsIdentity();
    std::size_t const index = entities_.size();
    if (!indices_.emplace(entity, index).second) return;
    entities_.push_back(entity);
    if (materialized) {
        parents_.push_back(index);
        sizes_.push_back(1);
    }
    ++class_count_;
    relation_valid_ = false;
}

void EquivalenceRelation::AddEntities(std::vector<model::EntityId> const& entities) {
    for (auto const& entity : entities) AddEntity(entity);
}

bool EquivalenceRelation::AreEquivalent(model::EntityId const& first,
                                        model::EntityId const& second) const {
    auto const lhs = indices_.find(first);
    auto const rhs = indices_.find(second);
    return lhs != indices_.end() && rhs != indices_.end() &&
           FindRoot(lhs->second) == FindRoot(rhs->second);
}

bool EquivalenceRelation::Merge(model::EntityId const& first, model::EntityId const& second) {
    AddEntity(first);
    AddEntity(second);
    if (first == second) return false;
    if (IsIdentity()) {
        std::vector<std::size_t> parents(entities_.size());
        std::iota(parents.begin(), parents.end(), std::size_t{0});
        std::vector<std::size_t> sizes(entities_.size(), 1);
        parents_ = std::move(parents);
        sizes_ = std::move(sizes);
    }
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

std::size_t EquivalenceRelation::GetEntityCount() const noexcept {
    return entities_.size();
}

std::size_t EquivalenceRelation::GetClassCount() const noexcept {
    return class_count_;
}

EquivalenceRelation::Classes EquivalenceRelation::GetClasses() const {
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

EquivalenceRelation::Relation const& EquivalenceRelation::GetRelation() const {
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

std::size_t EquivalenceRelation::FindRoot(std::size_t index) const {
    if (IsIdentity()) return index;
    while (parents_[index] != index) index = parents_[index];
    return index;
}

std::size_t EquivalenceRelation::CompressRoot(std::size_t index) {
    std::size_t const root = FindRoot(index);
    while (parents_[index] != index) {
        std::size_t const next = parents_[index];
        parents_[index] = root;
        index = next;
    }
    return root;
}

}  // namespace gk
