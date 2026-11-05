#include "core/algorithms/gk_compact/parser/graph_parser.h"

#include <fstream>
#include <stdexcept>
#include <utility>

#include "core/algorithms/gk/parser/csv_reader.h"
#include "core/algorithms/gk_compact/model/graph_builder.h"

namespace gk_compact::parser {

Graph ReadGraph(std::istream& stream) {
    gk::parser::detail::CsvReader reader(stream);
    GraphBuilder builder;
    gk::model::Triple triple;
    while (reader.ReadTriple(triple)) {
        try {
            builder.AddTriple(triple);
        } catch (std::invalid_argument const& error) {
            gk::parser::detail::ThrowCsvError(reader.GetLineNumber(), error.what());
        }
    }
    return std::move(builder).Build();
}

Graph ReadGraph(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("GK CSV parser: cannot open graph file " + path.string());
    return ReadGraph(file);
}

}  // namespace gk_compact::parser
