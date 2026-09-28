#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

// An explicit representation of Eq from the GK specification.  Every stored pair is an ordered
// pair (e1, e2), so reflexivity and symmetry are visible in the relation rather than being hidden
// in a representative map.
class EquivalenceRelation {
public:
    using Pair = std::pair<model::EntityId, model::EntityId>;
    using Relation = std::set<Pair>;

    EquivalenceRelation() = default;

    explicit EquivalenceRelation(std::vector<model::EntityId> const& entities) {
        Initialize(entities);
    }

    void AddEntity(model::EntityId const& entity) {
        relation_.emplace(entity, entity);
    }

    void AddEntities(std::vector<model::EntityId> const& entities) {
        for (model::EntityId const& entity : entities) AddEntity(entity);
    }

    bool AreEquivalent(model::EntityId const& e1, model::EntityId const& e2) const {
        return relation_.contains({e1, e2});
    }

    // Merge two equivalence classes and materialize the Cartesian product of the resulting class.
    void Merge(model::EntityId const& e1, model::EntityId const& e2) {
        AddEntity(e1);
        AddEntity(e2);
        if (AreEquivalent(e1, e2)) return;

        std::set<model::EntityId> lhs_class = GetClass(e1);
        std::set<model::EntityId> rhs_class = GetClass(e2);
        for (model::EntityId const& lhs : lhs_class) {
            for (model::EntityId const& rhs : rhs_class) {
                relation_.emplace(lhs, rhs);
                relation_.emplace(rhs, lhs);
            }
        }
    }

    Relation const& GetRelation() const noexcept {
        return relation_;
    }

private:
    // Construct Eq_0 over the supplied entity domain.
    void Initialize(std::vector<model::EntityId> const& entities) {
        relation_.clear();
        AddEntities(entities);
    }

    Relation relation_;

    std::set<model::EntityId> GetClass(model::EntityId const& entity) const {
        std::set<model::EntityId> result;
        for (auto const& [lhs, rhs] : relation_) {
            if (lhs == entity) result.insert(rhs);
        }
        return result;
    }
};

}  // namespace gk
