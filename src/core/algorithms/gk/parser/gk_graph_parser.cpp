#include "core/algorithms/gk/parser/gk_graph_parser.h"

#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "core/algorithms/gk/parser/csv_reader.h"

namespace gk::parser {

model::Graph ReadGraph(std::istream& stream) {
    detail::CsvReader reader(stream);
    model::Graph result;
    std::unordered_map<model::EntityId, model::TypeId> entity_types;
    model::Triple triple;
    while (reader.ReadTriple(triple)) {
        for (auto const* node : {&triple.subject, &triple.object}) {
            if (node->kind != model::NodeKind::kEntity) continue;
            auto const [it, inserted] = entity_types.emplace(node->id_or_val, node->type);
            if (!inserted && it->second != node->type) {
                detail::ThrowCsvError(reader.GetLineNumber(),
                                      "entity " + node->id_or_val + " has inconsistent types");
            }
        }
        result.push_back(std::move(triple));
    }
    return result;
}

model::Graph ReadGraph(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("GK CSV parser: cannot open graph file " + path.string());
    return ReadGraph(file);
}

}  // namespace gk::parser
