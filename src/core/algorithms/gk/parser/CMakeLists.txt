set(NAME gk.csv)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE csv_reader.cpp)

set(NAME gk.parser)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE gk_graph_parser.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::csv)
