set(NAME gk_compact.graph)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/graph.cpp model/graph_metadata.cpp model/graph_builder.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::common spdlog::spdlog_header_only
)

add_subdirectory(parser)

set(NAME gk_compact.key)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE model/key.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk_compact::graph
                                     ${DESBORDANTE_PREFIX}::gk::common)
