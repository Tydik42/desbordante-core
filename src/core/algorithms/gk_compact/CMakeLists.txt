set(NAME gk_compact.graph)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE graph/graph.cpp graph/graph_builder.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::common spdlog::spdlog_header_only
)

add_subdirectory(parser)
