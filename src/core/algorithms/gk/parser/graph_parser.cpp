#include "core/algorithms/gk/parser/graph_parser.h"

#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "core/algorithms/gk/model/graph_builder.h"
#include "core/algorithms/gk/parser/csv_reader.h"

namespace gk::parser {

model::Graph ReadGraph(std::istream& stream) {
    detail::CsvReader reader(stream);
    GraphBuilder builder;
    model::Triple triple;
    while (reader.ReadTriple(triple)) {
        try {
            builder.AddTriple(triple);
        } catch (std::invalid_argument const& error) {
            detail::ThrowCsvError(reader.GetLineNumber(), error.what());
        }
    }
    return std::move(builder).Build();
}

model::Graph ReadGraph(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("GK CSV parser: cannot open graph file " + path.string());
    return ReadGraph(file);
}

}  // namespace gk::parser
