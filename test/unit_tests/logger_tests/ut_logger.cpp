// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <vsomeip/internal/logger.hpp>

#include "../../../implementation/logger/include/logger_impl.hpp"

namespace {

using vsomeip_v3::logger::level_e;
using vsomeip_v3::logger::logger_impl;

class cout_capture {
public:
    cout_capture() : previous_{std::cout.rdbuf(buffer_.rdbuf())} { }
    ~cout_capture() { std::cout.rdbuf(previous_); }

    cout_capture(const cout_capture&) = delete;
    cout_capture& operator=(const cout_capture&) = delete;

    std::string str() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::streambuf* previous_;
};

std::filesystem::path test_logfile() {
    const char* its_path = std::getenv("VSOMEIP_LOGGER_TEST_FILE");
    return its_path != nullptr ? std::filesystem::path{its_path} : std::filesystem::path{};
}

std::string read_logfile() {
    std::ifstream its_file{test_logfile(), std::ios::binary};
    std::ostringstream its_contents;
    its_contents << its_file.rdbuf();
    return its_contents.str();
}

// --- Singleton lifetime ----------------------------------------------------------------------

TEST(logger_test, get_returns_stable_non_null_instance) {
    auto* first = logger_impl::get();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, logger_impl::get());
    EXPECT_EQ(first, logger_impl::get());
}

TEST(logger_test, get_is_stable_across_threads) {
    auto* expected = logger_impl::get();

    constexpr int num_threads = 16;
    std::vector<logger_impl*> results(num_threads, nullptr);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&results, i] { results[static_cast<size_t>(i)] = logger_impl::get(); });
    }
    for (auto& t : threads) {
        t.join();
    }

    std::set<logger_impl*> unique{results.begin(), results.end()};
    ASSERT_EQ(unique.size(), 1u);
    EXPECT_EQ(*unique.begin(), expected);
}

// --- Configuration plumbing ------------------------------------------------------------------

TEST(logger_test, configuration_is_loaded_when_the_logger_is_created) {
    const auto& config = logger_impl::get()->get_configuration();
    EXPECT_TRUE(config.console_enabled);
    EXPECT_TRUE(config.file_enabled);
#ifdef USE_DLT
    EXPECT_TRUE(config.dlt_enabled);
#else
    EXPECT_FALSE(config.dlt_enabled);
#endif
    EXPECT_EQ(config.loglevel, level_e::LL_WARNING);
}

TEST(logger_test, configuration_is_immutable_after_creation) {
    const auto before = logger_impl::get()->get_configuration();
    const char* previous = std::getenv("VSOMEIP_CONFIGURATION");
    const std::string previous_value = previous != nullptr ? previous : "";
#ifdef _WIN32
    _putenv_s("VSOMEIP_CONFIGURATION", "/does/not/exist");
#else
    setenv("VSOMEIP_CONFIGURATION", "/does/not/exist", 1);
#endif

    const auto after = logger_impl::get()->get_configuration();

    EXPECT_EQ(after.console_enabled, before.console_enabled);
    EXPECT_EQ(after.file_enabled, before.file_enabled);
    EXPECT_EQ(after.dlt_enabled, before.dlt_enabled);
    EXPECT_EQ(after.loglevel, before.loglevel);
    EXPECT_EQ(after.logfile, before.logfile);
#ifdef _WIN32
    _putenv_s("VSOMEIP_CONFIGURATION", previous_value.c_str());
#else
    if (previous != nullptr) {
        setenv("VSOMEIP_CONFIGURATION", previous_value.c_str(), 1);
    } else {
        unsetenv("VSOMEIP_CONFIGURATION");
    }
#endif
}

// --- File logging ----------------------------------------------------------------------------

TEST(logger_test, log_to_file_writes_content) {
    logger_impl::get()->log_to_file("direct-file-payload\n");
    EXPECT_NE(read_logfile().find("direct-file-payload"), std::string::npos);
}

// --- Message: level filtering and formatting (console sink) ----------------------------------

TEST(logger_test, message_respects_configured_loglevel) {
    cout_capture capture;
    { VSOMEIP_ERROR << "error-should-appear"; } // ERROR(2) <= WARNING(3) -> emitted
    { VSOMEIP_WARNING << "warning-should-appear"; } // WARNING(3) <= WARNING(3) -> emitted
    { VSOMEIP_INFO << "info-should-be-dropped"; } // INFO(4) > WARNING(3) -> dropped

    const auto out = capture.str();
    EXPECT_NE(out.find("error-should-appear"), std::string::npos);
    EXPECT_NE(out.find("warning-should-appear"), std::string::npos);
    EXPECT_EQ(out.find("info-should-be-dropped"), std::string::npos);
}

TEST(logger_test, message_format_contains_level_text_and_newline) {
    cout_capture capture;
    { VSOMEIP_WARNING << "formatted-payload"; }

    const auto out = capture.str();
    EXPECT_NE(out.find("[warning]"), std::string::npos);
    EXPECT_NE(out.find("formatted-payload"), std::string::npos);
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.back(), '\n');
}

TEST(logger_test, message_is_written_to_file_sink) {
    { VSOMEIP_ERROR << "message-file-sink-payload"; }

    const auto contents = read_logfile();
    EXPECT_NE(contents.find("message-file-sink-payload"), std::string::npos);
    EXPECT_NE(contents.find("[error]"), std::string::npos);
}

// --- DLT routing -----------------------------------------------------------------------------

// Runs in both configs: with USE_DLT the emitted levels reach log_to_dlt(), otherwise the DLT
// block is compiled out. Either way, logging every level via the macros must be crash-safe,
// whether the message is emitted (FATAL..WARNING) or dropped by the level filter (INFO..TRACE).
TEST(logger_test, dlt_enabled_messages_are_crash_safe) {
    cout_capture capture;
    EXPECT_NO_THROW({
        VSOMEIP_FATAL << "dlt-fatal";
        VSOMEIP_ERROR << "dlt-error";
        VSOMEIP_WARNING << "dlt-warning";
        VSOMEIP_INFO << "dlt-info";
        VSOMEIP_DEBUG << "dlt-debug";
        VSOMEIP_TRACE << "dlt-verbose";
    });
}

#ifdef USE_DLT

// DLT-backend specific: calling log_to_dlt() directly for every level (including the default
// branch) must not crash, and the by-value DLT context member must be reachable.
TEST(logger_test, log_to_dlt_all_levels_does_not_crash) {
    auto* logger = logger_impl::get();

    // context member is reachable
    EXPECT_NE(&logger->dlt_context_, nullptr);

    const level_e levels[] = {level_e::LL_NONE, level_e::LL_FATAL, level_e::LL_ERROR,  level_e::LL_WARNING,
                              level_e::LL_INFO, level_e::LL_DEBUG, level_e::LL_VERBOSE};
    for (auto level : levels) {
        // log_to_dlt expects a null-terminated payload.
        std::string msg = "dlt-direct-payload";
        msg.push_back('\0');
        EXPECT_NO_THROW(logger->log_to_dlt(level, msg));
    }
}

#endif // USE_DLT

} // namespace
