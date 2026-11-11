set(NAME gk_compact.graph)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/graph.cpp model/graph_metadata.cpp model/graph_builder.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::graph spdlog::spdlog_header_only
)

add_subdirectory(parser)

set(NAME gk_compact.key)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/key.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk_compact::graph
                                     ${DESBORDANTE_PREFIX}::gk::key)

# Pair evaluation is shared by the validator and future Chase implementation.
set(NAME gk_compact.eval)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/equivalence_relation.cpp evaluation/eval_gk.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk_compact::graph)

# Preparation and optional kernels are outside exact EvalGK.
set(NAME gk_compact.optimizations)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE optimizations/evaluation.cpp optimizations/neighborhood.cpp
                               optimizations/pairing_filter.cpp optimizations/subgraph_reduction.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk_compact::eval
                                     spdlog::spdlog_header_only)

# All three optional stages are implemented independently of exact EvalGK.
set(GK_COMPACT_LOCALIZATION_AVAILABLE ON)
set(GK_COMPACT_PAIRING_AVAILABLE ON)
set(GK_COMPACT_REDUCTION_AVAILABLE ON)

set(GK_COMPACT_VALIDATION_AVAILABLE ON)

set(NAME gk_compact.validator)
desbordante_add_lib(NAME STATIC)
target_sources(${NAME} PRIVATE gk_validator/gk_validator.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::algos
                                     ${DESBORDANTE_PREFIX}::config
                                     ${DESBORDANTE_PREFIX}::gk_compact::eval
                                     ${DESBORDANTE_PREFIX}::gk_compact::optimizations
                                     ${DESBORDANTE_PREFIX}::gk_compact::key
                                     ${DESBORDANTE_PREFIX}::gk_compact::parser
                                     Boost::headers spdlog::spdlog_header_only)
set_property(TARGET ${NAME} PROPERTY GK_COMPACT_KERNEL_AVAILABLE ${GK_COMPACT_VALIDATION_AVAILABLE})

message(STATUS "Compact GK validation: ${GK_COMPACT_VALIDATION_AVAILABLE}")
message(STATUS "Compact GK optimizations: localization=${GK_COMPACT_LOCALIZATION_AVAILABLE}, pairing=${GK_COMPACT_PAIRING_AVAILABLE}, reduction=${GK_COMPACT_REDUCTION_AVAILABLE}")
