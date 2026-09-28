#pragma once

#include <unordered_map>
#include <utility>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk {

class JointMapping {
public:
    using Mapping = std::pair<model::Node, model::Node>;

    void Bind(model::Node const& v1, model::Node const& v2, model::Variable const& u) {
        mapping_[u] = {v1, v2};
    }

    void Unbind(model::Variable const& u) {
        mapping_.erase(u);
    }

    bool IsBound(model::Variable const& u) const {
        return mapping_.contains(u);
    }

    Mapping const& Get(model::Variable const& u) const {
        return mapping_.at(u);
    }

    auto const& All() const {
        return mapping_;
    }

    bool ViolatesInjectivity(model::Variable const& u, model::Node const& v1,
                             model::Node const& v2) const {
        for (auto const& [var, nodes] : mapping_) {
            if (var == u) continue;

            if (nodes.first == v1 || nodes.second == v2) return true;
        }

        return false;
    }

    bool IsInjective(model::Variable const& u, model::Node const& v1, model::Node const& v2) const {
        return !ViolatesInjectivity(u, v1, v2);
    }

private:
    std::unordered_map<model::Variable, std::pair<model::Node, model::Node>> mapping_;
};

}  // namespace gk
