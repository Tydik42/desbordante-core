#include "core/algorithms/gk_compact/parser/graph_parser.h"

#include <fstream>
#include <stdexcept>
#include <utility>

#include "core/algorithms/gk/parser/csv_reader.h"
#include "core/algorithms/gk_compact/model/graph_builder.h"
#include "core/util/logger.h"

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#include <chrono>
#endif

namespace gk_compact::parser {

Graph ReadGraph(std::istream& stream) {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    bool const trace = util::logging::Initialize()->should_log(spdlog::level::trace);
    auto const started =
            trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
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
    auto graph = std::move(builder).Build();
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    if (trace)
        LOG_TRACE("Compact GK phase 'graph_parse': {} ns, input_triples={}, vertices={}, edges={}",
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - started)
                          .count(),
                  graph.GetInputTripleCount(), graph.GetVertexCount(), graph.GetEdgeCount());
#endif
    return graph;
}

Graph ReadGraph(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("GK CSV parser: cannot open graph file " + path.string());
    return ReadGraph(file);
}

}  // namespace gk_compact::parser
