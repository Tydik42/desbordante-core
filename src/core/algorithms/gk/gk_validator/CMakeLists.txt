set(NAME gk.validator)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE gk_validator.cpp)
target_sources(${NAME} PRIVATE eval_gk.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::algos ${DESBORDANTE_PREFIX}::config Boost::headers
                    ${DESBORDANTE_PREFIX}::gk::parser magic_enum::magic_enum
                    spdlog::spdlog_header_only
)
