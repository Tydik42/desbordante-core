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

constexpr std::array<std::string_view, 6> kHeader = {"subject_id",         "subject_type",
                                                     "predicate",          "object_kind",
                                                     "object_id_or_value", "object_type"};

using Record = std::array<std::string, kHeader.size()>;

[[noreturn]] void ThrowParseError(std::size_t line_number, std::string const& message) {
    throw std::runtime_error("GK CSV parser: line " + std::to_string(line_number) + ": " + message);
}

// Read one logical CSV record. A quoted field may span physical lines; its line endings,
// whitespace and backslashes are data, not escape sequences.
bool ReadRecord(std::istream& stream, Record& fields, std::size_t& line_number) {
    enum class State { kStart, kUnquoted, kQuoted, kAfterQuote };
    State state = State::kStart;
    fields = {};
    std::size_t field = 0;
    std::string line;
    while (std::getline(stream, line)) {
        ++line_number;
        bool const has_cr = !line.empty() && line.back() == '\r';
        if (has_cr) line.pop_back();
        if (line.empty() && state == State::kStart && field == 0) continue;

        for (char character : line) {
            if (state == State::kQuoted) {
                if (character == '"') {
                    state = State::kAfterQuote;
                } else {
                    fields[field].push_back(character);
                }
            } else if (character == ',') {
                if (++field == fields.size()) {
                    ThrowParseError(line_number, "expected exactly 6 comma-separated fields");
                }
                state = State::kStart;
            } else if (character == '"') {
                if (state == State::kStart) {
                    state = State::kQuoted;
                } else if (state == State::kAfterQuote) {
                    fields[field].push_back('"');
                    state = State::kQuoted;
                } else {
                    ThrowParseError(line_number, "unexpected quote in an unquoted field");
                }
            } else {
                if (state == State::kAfterQuote) {
                    ThrowParseError(line_number, "unexpected character after a closing quote");
                }
                fields[field].push_back(character);
                state = State::kUnquoted;
            }
        }

        if (state != State::kQuoted) {
            if (field + 1 != fields.size()) {
                ThrowParseError(line_number, "expected exactly 6 comma-separated fields");
            }
            return true;
        }
        if (!stream.eof()) {
            if (has_cr) fields[field].push_back('\r');
            fields[field].push_back('\n');
        }
    }
    if (stream.bad()) throw std::runtime_error("GK CSV parser: failed while reading the graph");
    if (state == State::kQuoted) ThrowParseError(line_number, "unterminated quoted field");
    return false;
}

}  // namespace

model::Graph ReadGraph(std::istream& stream) {
    model::Graph result;
    std::unordered_map<model::EntityId, model::TypeId> entity_types;

    Record fields;
    std::size_t line_number = 0;
    bool header_read = false;
    while (ReadRecord(stream, fields, line_number)) {
        if (!header_read) {
            for (std::size_t index = 0; index < fields.size(); ++index) {
                if (fields[index] != kHeader[index]) {
                    ThrowParseError(line_number, "expected the GK CSV header");
                }
            }
            header_read = true;
            continue;
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

        model::Node subject = register_entity(subject_id, subject_type);
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
        throw std::runtime_error("GK CSV parser: missing GK CSV header");
    }
    return result;
}

model::Graph ReadGraph(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("GK CSV parser: cannot open graph file " + path.string());
    }
    return ReadGraph(file);
}

}  // namespace gk::parser
