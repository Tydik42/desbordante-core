set(NAME gk.common)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/validation.cpp)
target_link_libraries(${NAME} PRIVATE magic_enum::magic_enum spdlog::spdlog_header_only)

set(NAME gk.graph)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/graph_builder.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::common)

add_subdirectory(parser)

set(NAME gk.key)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/key.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::common)

set(NAME gk.eval)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/equivalence_relation.cpp evaluation/eval_gk.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::key)

set(NAME gk.optimizations)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE optimizations/evaluation.cpp optimizations/neighborhood.cpp
                               optimizations/pairing_filter.cpp optimizations/subgraph_reduction.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::eval spdlog::spdlog_header_only)

set(NAME gk.validator)
desbordante_add_lib(NAME STATIC)
target_sources(${NAME} PRIVATE gk_validator/gk_validator.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::algos ${DESBORDANTE_PREFIX}::config
                                     ${DESBORDANTE_PREFIX}::gk::optimizations
                                     ${DESBORDANTE_PREFIX}::gk::parser Boost::headers
                                     spdlog::spdlog_header_only)

set(NAME gk.chase)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE gk_deduplicator/chase_engine.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::optimizations
                                     ${DESBORDANTE_PREFIX}::config spdlog::spdlog_header_only)

set(NAME gk.deduplicator)
desbordante_add_lib(NAME STATIC)
target_sources(${NAME} PRIVATE gk_deduplicator/gk_deduplicator.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::algos ${DESBORDANTE_PREFIX}::config
                                     ${DESBORDANTE_PREFIX}::gk::chase
                                     ${DESBORDANTE_PREFIX}::gk::parser Boost::headers
                                     spdlog::spdlog_header_only)
