set(NAME gk.eval)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE eval_gk.cpp)
target_link_libraries(${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::pairing spdlog::spdlog_header_only)

set(NAME gk.validator)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE gk_validator.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::algos ${DESBORDANTE_PREFIX}::config Boost::headers
                    ${DESBORDANTE_PREFIX}::gk::parser ${DESBORDANTE_PREFIX}::gk::common
                    ${DESBORDANTE_PREFIX}::gk::eval
                    spdlog::spdlog_header_only
)
