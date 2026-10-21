set(NAME gk.chase)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE chase_engine.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::eval ${DESBORDANTE_PREFIX}::gk::common
                    spdlog::spdlog_header_only
)
