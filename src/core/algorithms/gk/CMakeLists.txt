set(NAME gk.common)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE validation.cpp)
target_link_libraries(${NAME} PRIVATE magic_enum::magic_enum spdlog::spdlog_header_only)

add_subdirectory(parser)
add_subdirectory(gk_validator)
add_subdirectory(chase)
add_subdirectory(gk_deduplicator)
