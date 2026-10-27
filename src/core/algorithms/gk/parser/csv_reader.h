#pragma once

#include <cstddef>
#include <istream>
#include <string>

#include "core/algorithms/gk/graph_descriptor.h"

namespace gk::parser::detail {

// Shared CSV syntax only. Each graph implementation owns interning, type consistency
// checks and storage construction. The input stream must outlive this reader.
class CsvReader {
public:
    explicit CsvReader(std::istream& stream);
    bool ReadTriple(model::Triple& triple);

    std::size_t GetLineNumber() const noexcept {
        return line_number_;
    }

private:
    std::istream& stream_;
    std::size_t line_number_ = 0;
};

[[noreturn]] void ThrowCsvError(std::size_t line, std::string const& message);

}  // namespace gk::parser::detail
