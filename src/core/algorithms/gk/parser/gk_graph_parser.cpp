#include "core/algorithms/gk/parser/gk_graph_parser.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gk::parser {

namespace {

constexpr std::string_view kHeader =
        "subject_id\tsubject_type\tpredicate\tobject_kind\tobject_id_or_value\tobject_type";

[[noreturn]] void ThrowParseError(std::size_t line_number, std::string const& message) {
    throw std::runtime_error("GK TSV parser: line " + std::to_string(line_number) + ": " + message);
}

std::array<std::string_view, 6> SplitFields(std::string_view line) {
    std::array<std::string_view, 6> fields;
    std::size_t begin = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        std::size_t const end = line.find('\t', begin);
        if (index + 1 == fields.size()) {
            if (end != std::string_view::npos) {
                throw std::invalid_argument("expected exactly 6 tab-separated fields");
            }
            fields[index] = line.substr(begin);
            return fields;
        }
        if (end == std::string_view::npos) {
            throw std::invalid_argument("expected exactly 6 tab-separated fields");
        }
        fields[index] = line.substr(begin, end - begin);
        begin = end + 1;
    }
    return fields;  // Unreachable, keeps the return explicit for static analysis.
}

}  // namespace

model::Graph ReadGraph(std::istream& stream) {
    model::Graph result;
    std::unordered_map<model::EntityId, model::TypeId> entity_types;

    std::string line;
    std::size_t line_number = 0;
    bool header_read = false;
    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (!header_read) {
            if (line != kHeader) {
                ThrowParseError(line_number, "expected the GK TSV header");
            }
            header_read = true;
            continue;
        }

        std::array<std::string_view, 6> fields;
        try {
            fields = SplitFields(line);
        } catch (std::invalid_argument const& error) {
            ThrowParseError(line_number, error.what());
        }

        std::string_view const subject_id = fields[0];
        std::string_view const subject_type = fields[1];
        std::string_view const predicate = fields[2];
        std::string_view const object_kind = fields[3];
        std::string_view const object_id_or_value = fields[4];
        std::string_view const object_type = fields[5];

        if (subject_id.empty() || subject_type.empty() || predicate.empty() ||
            object_id_or_value.empty()) {
            ThrowParseError(line_number,
                            "subject, subject type, predicate, and object must be "
                            "non-empty");
        }

        auto register_entity = [&](std::string_view id, std::string_view type) {
            auto [it, inserted] = entity_types.emplace(std::string(id), std::string(type));
            if (!inserted && it->second != type) {
                ThrowParseError(line_number,
                                "entity " + std::string(id) + " has inconsistent types");
            }
            return model::Node{model::NodeKind::kEntity, std::string(id), it->second};
        };

        model::Node const subject = register_entity(subject_id, subject_type);
        model::Node object;
        if (object_kind == "entity") {
            if (object_type.empty() || object_type == "-") {
                ThrowParseError(line_number, "entity object must have a non-empty type");
            }
            object = register_entity(object_id_or_value, object_type);
        } else if (object_kind == "value") {
            if (object_type != "-") {
                ThrowParseError(line_number, "value object must use '-' as its type");
            }
            object = model::Node{model::NodeKind::kValue, std::string(object_id_or_value), {}};
        } else {
            ThrowParseError(line_number, "object kind must be 'entity' or 'value'");
        }

        result.push_back({std::move(subject), std::string(predicate), std::move(object)});
    }

    if (!header_read) {
        throw std::runtime_error("GK TSV parser: missing GK TSV header");
    }
    if (stream.bad()) {
        throw std::runtime_error("GK TSV parser: failed while reading the graph");
    }
    return result;
}

model::Graph ReadGraph(std::filesystem::path const& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("GK TSV parser: cannot open graph file " + path.string());
    }
    return ReadGraph(file);
}

}  // namespace gk::parser
