#include "core/algorithms/gk_compact/equivalence_relation.h"

#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace gk_compact {

EquivalenceRelation::EquivalenceRelation(std::size_t entity_count)
    : entity_count_(entity_count), class_count_(entity_count) {
    if (entity_count > std::numeric_limits<VertexId>::max())
        throw std::length_error("Compact GK equivalence exceeds its 32-bit entity ID space");
}

EquivalenceRelation& EquivalenceRelation::operator=(EquivalenceRelation const& other) {
    if (this != &other) {
        EquivalenceRelation copy(other);
        *this = std::move(copy);
    }
    return *this;
}

EquivalenceRelation::EquivalenceRelation(EquivalenceRelation&& other) noexcept
    : parents_(std::move(other.parents_)),
      sizes_(std::move(other.sizes_)),
      entity_count_(std::exchange(other.entity_count_, 0)),
      class_count_(std::exchange(other.class_count_, 0)) {}

EquivalenceRelation& EquivalenceRelation::operator=(EquivalenceRelation&& other) noexcept {
    if (this != &other) {
        parents_ = std::move(other.parents_);
        sizes_ = std::move(other.sizes_);
        entity_count_ = std::exchange(other.entity_count_, 0);
        class_count_ = std::exchange(other.class_count_, 0);
    }
    return *this;
}

VertexId EquivalenceRelation::FindRoot(VertexId vertex) const {
    if (vertex >= entity_count_)
        throw std::out_of_range("Compact GK equivalence requires an entity ID");
    if (IsIdentity()) return vertex;
    while (parents_[vertex] != vertex) vertex = parents_[vertex];
    return vertex;
}

VertexId EquivalenceRelation::CompressRoot(VertexId vertex) {
    auto const root = FindRoot(vertex);
    while (parents_[vertex] != vertex) {
        auto const next = parents_[vertex];
        parents_[vertex] = root;
        vertex = next;
    }
    return root;
}

bool EquivalenceRelation::AreEquivalent(VertexId first, VertexId second) const {
    return FindRoot(first) == FindRoot(second);
}

bool EquivalenceRelation::Merge(VertexId first, VertexId second) {
    // Validate both IDs before making any change to the relation.
    if (first >= entity_count_ || second >= entity_count_)
        throw std::out_of_range("Compact GK equivalence requires an entity ID");
    if (first == second) return false;
    if (IsIdentity()) {
        // Build both buffers before committing either: allocation failure must
        // leave Eq_0 intact and usable for a later retry.
        std::vector<VertexId> parents(entity_count_);
        std::iota(parents.begin(), parents.end(), VertexId{0});
        std::vector<std::uint32_t> sizes(entity_count_, 1);
        parents_ = std::move(parents);
        sizes_ = std::move(sizes);
    }
    auto first_root = CompressRoot(first);
    auto second_root = CompressRoot(second);
    if (first_root == second_root) return false;
    if (sizes_[first_root] < sizes_[second_root]) std::swap(first_root, second_root);
    parents_[second_root] = first_root;
    sizes_[first_root] += sizes_[second_root];
    --class_count_;
    return true;
}

std::vector<std::vector<VertexId>> EquivalenceRelation::GetClasses() const {
    constexpr auto missing = std::numeric_limits<VertexId>::max();
    std::vector<std::vector<VertexId>> classes;
    classes.reserve(class_count_);
    if (IsIdentity()) {
        for (std::size_t index = 0; index < entity_count_; ++index)
            classes.push_back({static_cast<VertexId>(index)});
        return classes;
    }
    std::vector<VertexId> indices(entity_count_, missing);
    for (std::size_t index = 0; index < entity_count_; ++index) {
        auto const vertex = static_cast<VertexId>(index);
        auto& group = indices[FindRoot(vertex)];
        if (group == missing) {
            group = static_cast<VertexId>(classes.size());
            classes.emplace_back();
        }
        classes[group].push_back(vertex);
    }
    // Ascending input IDs already give sorted classes, ordered by their minima.
    return classes;
}

}  // namespace gk_compact
