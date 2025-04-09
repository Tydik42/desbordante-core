//
// Created by Ilya Vologin
// https://github.com/cupertank
//
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <gtest/gtest.h>

void ConfigureSpdlog() {
    try {
        std::string filename = "./latest.log";
        std::string format = "%v";

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename);
        file_sink->set_pattern(format);

        auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
        console_sink->set_pattern(format);

        auto logger = std::make_shared<spdlog::logger>("test",
            spdlog::sinks_init_list{file_sink, console_sink});
        logger->set_level(spdlog::level::info);

        set_default_logger(logger);

        spdlog::set_error_handler([](const std::string& msg) {
            std::cerr << "SPDLOG error: " << msg << std::endl;
        });

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        throw;
    }
}

int main(int argc, char** argv) {
    ConfigureSpdlog();
    ::testing::InitGoogleTest(&argc, argv);
    spdlog::info("Starting tests...");

    return RUN_ALL_TESTS();
}