#include "core/algorithms/gk/parser/csv_reader.h"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace gk::parser::detail {

[[noreturn]] void ThrowCsvError(std::size_t line, std::string const& message) {
    throw std::runtime_error("GK CSV parser: line " + std::to_string(line) + ": " + message);
}

namespace {
constexpr std::array<std::string_view, 6> kHeader = {"subject_id",         "subject_type",
                                                     "predicate",          "object_kind",
                                                     "object_id_or_value", "object_type"};

using Record = std::array<std::string, kHeader.size()>;

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
                    ThrowCsvError(line_number, "expected exactly 6 comma-separated fields");
                }
                state = State::kStart;
            } else if (character == '"') {
                if (state == State::kStart) {
                    state = State::kQuoted;
                } else if (state == State::kAfterQuote) {
                    fields[field].push_back('"');
                    state = State::kQuoted;
                } else {
                    ThrowCsvError(line_number, "unexpected quote in an unquoted field");
                }
            } else {
                if (state == State::kAfterQuote) {
                    ThrowCsvError(line_number, "unexpected character after a closing quote");
                }
                fields[field].push_back(character);
                state = State::kUnquoted;
            }
        }

        if (state != State::kQuoted) {
            if (field + 1 != fields.size()) {
                ThrowCsvError(line_number, "expected exactly 6 comma-separated fields");
            }
            return true;
        }
        if (!stream.eof()) {
            if (has_cr) fields[field].push_back('\r');
            fields[field].push_back('\n');
        }
    }
    if (stream.bad()) throw std::runtime_error("GK CSV parser: failed while reading the graph");
    if (state == State::kQuoted) ThrowCsvError(line_number, "unterminated quoted field");
    return false;
}

}  // namespace

CsvReader::CsvReader(std::istream& stream) : stream_(stream) {
    Record fields;
    if (!ReadRecord(stream_, fields, line_number_)) {
        throw std::runtime_error("GK CSV parser: missing GK CSV header");
    }
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (fields[index] != kHeader[index])
            ThrowCsvError(line_number_, "expected the GK CSV header");
    }
}

bool CsvReader::ReadTriple(model::Triple& triple) {
    Record fields;
    if (!ReadRecord(stream_, fields, line_number_)) return false;
    std::string_view const subject_id = fields[0];
    std::string_view const subject_type = fields[1];
    std::string_view const predicate = fields[2];
    std::string_view const object_kind = fields[3];
    std::string_view const object_id_or_value = fields[4];
    std::string_view const object_type = fields[5];

    if (subject_id.empty() || subject_type.empty() || subject_type == "-" || predicate.empty() ||
        object_id_or_value.empty()) {
        ThrowCsvError(line_number_,
                      "subject, subject type, predicate, and object must be "
                      "non-empty; entity type must differ from '-'");
    }

    model::Node subject{model::NodeKind::kEntity, std::string(subject_id),
                        std::string(subject_type)};
    model::Node object;
    if (object_kind == "entity") {
        if (object_type.empty() || object_type == "-") {
            ThrowCsvError(line_number_, "entity object must have a non-empty type");
        }
        object = model::Node{model::NodeKind::kEntity, std::string(object_id_or_value),
                             std::string(object_type)};
    } else if (object_kind == "value") {
        if (object_type != "-") {
            ThrowCsvError(line_number_, "value object must use '-' as its type");
        }
        object = model::Node{model::NodeKind::kValue, std::string(object_id_or_value), {}};
    } else {
        ThrowCsvError(line_number_, "object kind must be 'entity' or 'value'");
    }

    triple = {std::move(subject), std::string(predicate), std::move(object)};
    return true;
}

}  // namespace gk::parser::detail
