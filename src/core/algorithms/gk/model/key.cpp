#include "core/algorithms/gk/model/key.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>
#include <utility>

#include "core/algorithms/gk/model/validation.h"

namespace gk {
namespace {

TraversalOrder BuildTraversalOrder(model::GraphKey const& key) {
    TraversalOrder order;
    order.reserve(key.Q.size());

    std::unordered_set<std::size_t> used;
    used.reserve(key.Q.size());
    std::vector<model::Variable> reached{key.root_x};

    while (order.size() < key.Q.size()) {
        std::size_t next_index = key.Q.size();

        for (std::size_t index = 0; index < key.Q.size(); ++index) {
            if (used.contains(index)) continue;

            auto const& triple = key.Q[index];
            bool const subject_reached =
                    std::find(reached.begin(), reached.end(), triple.subject) != reached.end();
            bool const object_reached =
                    std::find(reached.begin(), reached.end(), triple.object) != reached.end();
            if (subject_reached && object_reached) {
                next_index = index;
                break;
            }
        }

        if (next_index == key.Q.size()) {
            for (std::size_t index = 0; index < key.Q.size(); ++index) {
                if (used.contains(index)) continue;
                auto const& triple = key.Q[index];
                if (std::find(reached.begin(), reached.end(), triple.subject) != reached.end() ||
                    std::find(reached.begin(), reached.end(), triple.object) != reached.end()) {
                    next_index = index;
                    break;
                }
            }
        }

        // CompileKey has already established connectivity and nonempty Q.
        assert(next_index < key.Q.size());

        used.insert(next_index);
        order.push_back(next_index);

        auto const& triple = key.Q[next_index];
        if (std::find(reached.begin(), reached.end(), triple.subject) == reached.end()) {
            reached.push_back(triple.subject);
        }
        if (std::find(reached.begin(), reached.end(), triple.object) == reached.end()) {
            reached.push_back(triple.object);
        }
    }

    return order;
}

}  // namespace

CompiledKey CompileKey(model::GraphKey key) {
    CheckKey(key);
    auto order = BuildTraversalOrder(key);
    return {std::move(key), std::move(order)};
}

}  // namespace gk
