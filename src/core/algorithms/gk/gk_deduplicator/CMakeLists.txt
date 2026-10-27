set(NAME gk.chase)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE chase_engine.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::gk::eval ${DESBORDANTE_PREFIX}::gk::common
                    spdlog::spdlog_header_only
)

set(NAME gk.deduplicator)
desbordante_add_lib(NAME)
target_sources(${NAME} PRIVATE gk_deduplicator.cpp)
target_link_libraries(
    ${NAME} PRIVATE ${DESBORDANTE_PREFIX}::algos ${DESBORDANTE_PREFIX}::config
                    ${DESBORDANTE_PREFIX}::gk::chase ${DESBORDANTE_PREFIX}::gk::common
                    ${DESBORDANTE_PREFIX}::gk::parser Boost::headers spdlog::spdlog_header_only
)
