set(NAME gk.validator)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE gk_validator.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::algos ${DESBORDANTE_PREFIX}::config Boost::headers
                    ${DESBORDANTE_PREFIX}::gk::parser ${DESBORDANTE_PREFIX}::gk::common
                    ${DESBORDANTE_PREFIX}::gk::eval ${DESBORDANTE_PREFIX}::gk::optimizations
                    spdlog::spdlog_header_only
)
