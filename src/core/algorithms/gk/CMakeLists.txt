set(NAME gk.common)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE validation.cpp model/key.cpp)
target_link_libraries(${NAME} PRIVATE magic_enum::magic_enum spdlog::spdlog_header_only)

set(NAME gk.eval)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE evaluation/eval_gk.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::common)

set(NAME gk.optimizations)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE optimizations/pairing_filter.cpp optimizations/neighborhood.cpp
                               optimizations/evaluation.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::common
                                     ${DESBORDANTE_PREFIX}::gk::eval spdlog::spdlog_header_only)

add_subdirectory(parser)
add_subdirectory(gk_validator)
add_subdirectory(gk_deduplicator)
