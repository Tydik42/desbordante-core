set(NAME gk_compact.parser)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE graph_parser.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk_compact::graph ${DESBORDANTE_PREFIX}::gk::csv
)
