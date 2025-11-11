#include <functional>
#include <unordered_map>

#include <boost/any.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "algorithms/algebraic_constraints/bin_operation_enum.h"
#include "algorithms/cfd/enums.h"
#include "algorithms/dd/dd.h"
#include "algorithms/md/hymd/enums.h"
#include "algorithms/md/hymd/hymd.h"
#include "algorithms/md/md_verifier/column_similarity_classifier.h"
#include "algorithms/metric/enums.h"
#include "association_rules/ar_algorithm_enums.h"
#include "config/custom_random_seed/type.h"
#include "config/error_measure/type.h"
#include "config/exceptions.h"
#include "config/tabular_data/input_table_type.h"
#include "config/tabular_data/input_tables_type.h"
#include "od/fastod/od_ordering.h"
#include "parser/csv_parser/csv_parser.h"
#include "py_util/create_dataframe_reader.h"
#include "util/enum_to_available_values.h"

namespace {

namespace py = pybind11;
using ConvFunc = std::function<boost::any(std::string_view, py::handle)>;

inline std::string SnakeToKCamel(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 1);
    out.push_back('k');

    bool capitalize_next = true;

    for (char c : input) {
        if (c == '_') {
            capitalize_next = true;
        } else {
            if (capitalize_next) {
                out.push_back(std::toupper(static_cast<unsigned char>(c)));
                capitalize_next = false;
            } else {
                out.push_back(std::tolower(static_cast<unsigned char>(c)));
            }
        }
    }
    return out;
}

inline std::string KCamelToSnake(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    size_t start_idx = (input.size() > 1 && input[0] == 'k' && std::isupper(input[1])) ? 1 : 0;

    for (size_t i = start_idx; i < input.size(); ++i) {
        char c = input[i];
        if (std::isupper(c)) {
            if (i > start_idx) {
                out.push_back('_');
            }
            out.push_back(std::tolower(static_cast<unsigned char>(c)));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

template <typename T>
T CastAndReplaceCastError(std::string_view option_name, py::handle value) {
    try {
        return py::cast<T>(value);
    } catch (py::cast_error& e) {
        throw config::ConfigurationError(
                std::string("Unable to cast Python object to C++ type for option \"") +
                option_name.data() + '"');
    }
}

config::InputTable CreateCsvParser(std::string_view option_name, py::tuple const& arguments) {
    if (py::len(arguments) != 3) {
        throw config::ConfigurationError("Cannot create a CSV parser from passed tuple.");
    }

    return std::make_shared<CSVParser>(
            CastAndReplaceCastError<std::string>(option_name, arguments[0]),
            CastAndReplaceCastError<char>(option_name, arguments[1]),
            CastAndReplaceCastError<bool>(option_name, arguments[2]));
}

config::InputTable PythonObjToInputTable(std::string_view option_name, py::handle obj) {
    if (py::isinstance<py::tuple>(obj)) {
        return CreateCsvParser(option_name, py::cast<py::tuple>(obj));
    }
    return python_bindings::CreateDataFrameReader(obj);
}

template <typename Type>
std::pair<std::type_index, ConvFunc> const kNormalConvPair{
        std::type_index(typeid(Type)), [](std::string_view option_name, py::handle value) {
            return CastAndReplaceCastError<Type>(option_name, value);
        }};

template <typename EnumType>
    requires magic_enum::is_scoped_enum_v<EnumType> || magic_enum::is_unscoped_enum_v<EnumType>
std::pair<std::type_index, ConvFunc> const kEnumConvPair{
        std::type_index(typeid(EnumType)),
        [](std::string_view option_name, py::handle value) -> boost::any {
            auto user_str = CastAndReplaceCastError<std::string>(option_name, value);
            std::string cpp_enum_name = SnakeToKCamel(user_str);

            auto enum_optional = magic_enum::enum_cast<EnumType>(cpp_enum_name);

            if (enum_optional) return *enum_optional;

            std::stringstream error_message;
            std::stringstream possible_values;

            possible_values << "[";
            constexpr auto& values = magic_enum::enum_values<EnumType>();
            for (size_t i = 0; i < values.size(); ++i) {
                possible_values << KCamelToSnake(magic_enum::enum_name(values[i]));
                if (i < values.size() - 1) {
                    possible_values << "|";
                }
            }
            possible_values << "]";

            error_message << "Incorrect value '" << user_str << "' for option \"" << option_name
                          << "\". Possible values: " << possible_values.str();

            throw config::ConfigurationError(error_message.str());
        }};

template <typename EnumType>
    requires magic_enum::is_scoped_enum_v<EnumType> || magic_enum::is_unscoped_enum_v<EnumType>
std::pair<std::type_index, ConvFunc> const kCharEnumConvPair{
        std::type_index(typeid(EnumType)),
        [](std::string_view option_name, py::handle value) -> boost::any {
            using UnderlyingType = magic_enum::underlying_type_t<EnumType>;
            static_assert(std::is_same_v<UnderlyingType, char>,
                          "This converter is for char-based enums only.");

            auto char_value = CastAndReplaceCastError<UnderlyingType>(option_name, value);
            auto enum_optional = magic_enum::enum_cast<EnumType>(char_value);

            if (enum_optional) return *enum_optional;

            std::stringstream error_message;
            error_message << "Incorrect integral value '" << static_cast<int>(char_value)
                          << "' for option \"" << option_name << "\". Possible values are: [";

            constexpr auto& enum_values = magic_enum::enum_values<EnumType>();
            for (size_t i = 0; i < enum_values.size(); ++i) {
                error_message << static_cast<int>(magic_enum::enum_integer(enum_values[i]));
                if (i < enum_values.size() - 1) {
                    error_message << '|';
                }
            }
            error_message << ']';

            throw config::ConfigurationError(error_message.str());
        }};

boost::any InputTableToAny(std::string_view option_name, py::handle obj) {
    return PythonObjToInputTable(option_name, obj);
}

boost::any InputTablesToAny(std::string_view option_name, py::handle obj) {
    auto tables = CastAndReplaceCastError<std::vector<py::handle>>(option_name, obj);
    std::vector<config::InputTable> parsers;
    for (auto const& table : tables) parsers.push_back(PythonObjToInputTable(option_name, table));
    return parsers;
}

std::unordered_map<std::type_index, ConvFunc> const kConverters{
        kNormalConvPair<bool>,
        kNormalConvPair<double>,
        kNormalConvPair<unsigned int>,
        kNormalConvPair<long double>,
        kNormalConvPair<std::vector<unsigned int>>,
        kNormalConvPair<unsigned short>,
        kNormalConvPair<int>,
        kNormalConvPair<size_t>,
        kNormalConvPair<algos::hymd::HyMD::ColumnMatches>,
        kNormalConvPair<std::optional<int>>,
        kNormalConvPair<algos::md::ColumnSimilarityClassifier>,
        kNormalConvPair<std::vector<algos::md::ColumnSimilarityClassifier>>,
        kEnumConvPair<algos::metric::Metric>,
        kEnumConvPair<algos::metric::MetricAlgo>,
        kEnumConvPair<config::PfdErrorMeasureType>,
        kEnumConvPair<config::AfdErrorMeasureType>,
        kEnumConvPair<algos::InputFormat>,
        kEnumConvPair<algos::cfd::Substrategy>,
        kEnumConvPair<algos::hymd::LevelDefinition>,
        kEnumConvPair<algos::od::Ordering>,
        kCharEnumConvPair<algos::Binop>,
        {typeid(config::InputTable), InputTableToAny},
        {typeid(config::InputTables), InputTablesToAny},
        kNormalConvPair<std::filesystem::path>,
        kNormalConvPair<std::vector<std::filesystem::path>>,
        kNormalConvPair<std::unordered_set<size_t>>,
        kNormalConvPair<model::DDString>,
        kNormalConvPair<std::string>,
        kNormalConvPair<std::vector<std::pair<std::string, std::string>>>,
        kNormalConvPair<std::pair<std::string, std::string>>};

}  // namespace

namespace python_bindings {

boost::any PyToAny(std::string_view option_name, std::type_index index, py::handle obj) {
    return kConverters.at(index)(option_name, obj);
}

}  // namespace python_bindings
