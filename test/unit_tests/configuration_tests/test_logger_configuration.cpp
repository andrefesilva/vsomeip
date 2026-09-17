// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../../implementation/configuration/include/configuration_impl.hpp"
#include "../../../implementation/configuration/include/configuration_plugin_impl.hpp"

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using vsomeip_v3::configuration_plugin_impl;
using vsomeip_v3::cfg::configuration_impl;

class scratch_config_dir {
public:
    scratch_config_dir() : path_(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("vsomeip_log_cfg_%%%%-%%%%")) {
        boost::filesystem::create_directories(path_);
    }

    ~scratch_config_dir() {
        boost::system::error_code its_error;
        boost::filesystem::remove_all(path_, its_error);
    }

    void write(const std::string& _filename, const std::string& _content) const {
        std::ofstream its_file{(path_ / _filename).string()};
        its_file << _content;
    }

    std::string path() const { return path_.string(); }

private:
    boost::filesystem::path path_;
};

class env_guard {
public:
    explicit env_guard(std::string _name) : name_{std::move(_name)} {
        if (const char* its_value = std::getenv(name_.c_str()); its_value != nullptr) {
            previous_value_ = its_value;
        }
    }

    ~env_guard() {
        if (previous_value_) {
            set(previous_value_.value());
        } else {
            unset();
        }
    }

    void set(const std::string& _value) const {
#ifdef _WIN32
        static_cast<void>(_putenv_s(name_.c_str(), _value.c_str()));
#else
        static_cast<void>(setenv(name_.c_str(), _value.c_str(), 1));
#endif
    }

    void unset() const {
#ifdef _WIN32
        static_cast<void>(_putenv_s(name_.c_str(), ""));
#else
        static_cast<void>(unsetenv(name_.c_str()));
#endif
    }

private:
    std::string name_;
    std::optional<std::string> previous_value_;
};

template<typename F>
void run_concurrently(std::size_t _thread_count, F _body) {
    std::atomic<std::size_t> its_ready{0};
    std::atomic<bool> its_start{false};
    std::vector<std::thread> its_threads;
    its_threads.reserve(_thread_count);

    for (std::size_t i = 0; i < _thread_count; ++i) {
        its_threads.emplace_back([&its_ready, &its_start, &_body, i] {
            its_ready.fetch_add(1);
            while (!its_start.load()) {
                std::this_thread::yield();
            }
            _body(i);
        });
    }

    while (its_ready.load() != _thread_count) {
        std::this_thread::yield();
    }
    its_start.store(true);

    for (auto& its_thread : its_threads) {
        its_thread.join();
    }
}

#ifndef ANDROID

// Logger settings must be readable before the logger exists. This exercises
// the bootstrap-only parser, whose diagnostics cannot recursively create the
// logger while it is still being constructed.
TEST(logger_configuration_test, loads_global_configuration_without_bootstrap_logging) {
    scratch_config_dir directory;
    directory.write("custom.json", R"({ "logging": { "console": "false", "level": "fatal" } })");
    directory.write("README.txt", "not configuration data");

    env_guard global_configuration{"VSOMEIP_CONFIGURATION"};
    global_configuration.set(directory.path());

    const auto configuration = configuration_impl::get_logger_configuration();

    EXPECT_FALSE(configuration->has_console_log());
    EXPECT_EQ(configuration->get_loglevel(), vsomeip_v3::logger::level_e::LL_FATAL);
}

// The logger bootstrap parses the global mandatory files before any application.
// The first application on the same source reuses those documents instead of
// reading the files again, so it sees the bootstrap content, not the rewrite.
TEST(logger_configuration_test, application_reuses_documents_parsed_by_logger_bootstrap) {
    scratch_config_dir directory;
    directory.write("vsomeip_std.json", R"({ "network": "parsed-by-bootstrap", "logging": { "console": "false", "level": "warning" } })");

    env_guard global_configuration{"VSOMEIP_CONFIGURATION"};
    global_configuration.set(directory.path());

    const auto logger_configuration = configuration_impl::get_logger_configuration();
    EXPECT_EQ(logger_configuration->get_loglevel(), vsomeip_v3::logger::level_e::LL_WARNING);

    directory.write("vsomeip_std.json",
                    R"({ "network": "rewritten-after-bootstrap", "logging": { "console": "false", "level": "warning" } })");

    configuration_plugin_impl first_plugin;
    const auto first_configuration = first_plugin.get_configuration("bootstrap_documents_first_app", "");
    EXPECT_EQ(first_configuration->get_network(), "parsed-by-bootstrap");

    configuration_plugin_impl second_plugin;
    const auto second_configuration = second_plugin.get_configuration("bootstrap_documents_second_app", "");
    EXPECT_EQ(second_configuration->get_network(), "rewritten-after-bootstrap");
}

// If mandatory files contain no logger backend settings, bootstrap discovery
// must continue into optional files such as the repository's vsomeipd_std.json.
TEST(logger_configuration_test, finds_logger_configuration_in_optional_vsomeipd_file) {
    scratch_config_dir directory;
    directory.write("vsomeip_std.json", R"({ "network": "mandatory-network" })");
    directory.write("vsomeipd_std.json", R"({ "logging": { "console": "false", "level": "error" } })");

    env_guard global_configuration{"VSOMEIP_CONFIGURATION"};
    global_configuration.set(directory.path());

    const auto configuration = configuration_impl::get_logger_configuration();

    EXPECT_FALSE(configuration->has_console_log());
    EXPECT_EQ(configuration->get_loglevel(), vsomeip_v3::logger::level_e::LL_ERROR);
}

#if GTEST_HAS_STREAM_REDIRECTION

// Logger bootstrap must suppress parse diagnostics to avoid recursively
// creating the logger. An application loading the same broken file later
// must still report the failure through the now-safe normal logging path.
TEST(logger_configuration_test, bootstrap_parse_failure_is_silent_and_reported_on_application_load) {
    scratch_config_dir directory;
    directory.write("vsomeip_std.json", R"({ "logging": { "console": )");

    env_guard global_configuration{"VSOMEIP_CONFIGURATION"};
    global_configuration.set(directory.path());

    testing::internal::CaptureStdout();
    static_cast<void>(configuration_impl::get_logger_configuration());
    const auto bootstrap_output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(bootstrap_output.find("Could not parse JSON file"), std::string::npos);

    configuration_plugin_impl plugin;
    testing::internal::CaptureStdout();
    const auto application_configuration = plugin.get_configuration("parse_failure_app", "");
    const auto application_output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(application_configuration->get_network(), "vsomeip");
    EXPECT_NE(application_output.find("Could not parse JSON file"), std::string::npos);
}

#endif // GTEST_HAS_STREAM_REDIRECTION

// A named source keeps owning the application's configuration, but its logger
// backend keys must not change the process-wide logger configuration, which is
// always resolved from the global source.
TEST(logger_configuration_test, named_application_source_does_not_change_logger_configuration) {
    scratch_config_dir global_directory;
    global_directory.write("vsomeip_std.json", R"({
        "logging": {
            "console": "false",
            "file": { "enable": "false", "path": "global-logger.log" },
            "dlt": "false",
            "level": "error"
        }
    })");
    scratch_config_dir named_directory;
    named_directory.write("vsomeip_std.json", R"({
        "network": "named-application-network",
        "logging": {
            "console": "true",
            "file": { "enable": "true", "path": "named-logger.log" },
            "dlt": "true",
            "level": "fatal",
            "statistics": { "interval": "2345", "min-frequency": "12", "max-messages": "34" }
        }
    })");

    env_guard global_configuration{"VSOMEIP_CONFIGURATION"};
    env_guard named_configuration{"VSOMEIP_CONFIGURATION_logger_source_test_app"};
    global_configuration.set(global_directory.path());
    named_configuration.set(named_directory.path());

    const auto logger_configuration = configuration_impl::get_logger_configuration();
    configuration_plugin_impl plugin;
    const auto application_configuration = plugin.get_configuration("logger_source_test_app", "");

    EXPECT_EQ(application_configuration->get_network(), "named-application-network");
    EXPECT_EQ(application_configuration->get_statistics_interval(), 2345u);
    EXPECT_EQ(application_configuration->get_statistics_min_freq(), 12u);
    EXPECT_EQ(application_configuration->get_statistics_max_messages(), 34u);

    EXPECT_FALSE(logger_configuration->has_console_log());
    EXPECT_FALSE(logger_configuration->has_file_log());
    EXPECT_FALSE(logger_configuration->has_dlt_log());
    EXPECT_EQ(logger_configuration->get_logfile(), "global-logger.log");
    EXPECT_EQ(logger_configuration->get_loglevel(), vsomeip_v3::logger::level_e::LL_ERROR);
}

// Application-first initialization must not leak the named source into the
// logger configuration: whichever side loads first, the process logger
// configuration comes from the global source.
TEST(logger_configuration_test, application_first_keeps_logger_on_global_source) {
    scratch_config_dir global_directory;
    global_directory.write("vsomeip_std.json", R"({
        "network": "global-network",
        "logging": { "console": "false", "level": "warning" }
    })");
    scratch_config_dir named_directory;
    named_directory.write("vsomeip_std.json", R"({
        "network": "named-network",
        "logging": { "console": "true", "level": "fatal" }
    })");

    env_guard global_configuration{"VSOMEIP_CONFIGURATION"};
    env_guard named_configuration{"VSOMEIP_CONFIGURATION_application_first_test_app"};
    global_configuration.set(global_directory.path());
    named_configuration.set(named_directory.path());

    configuration_plugin_impl plugin;
    const auto application_configuration = plugin.get_configuration("application_first_test_app", "");
    const auto logger_configuration = configuration_impl::get_logger_configuration();

    EXPECT_EQ(application_configuration->get_network(), "named-network");
    EXPECT_FALSE(logger_configuration->has_console_log());
    EXPECT_EQ(logger_configuration->get_loglevel(), vsomeip_v3::logger::level_e::LL_WARNING);
}

// Logger-first and application-first bootstrap paths may run concurrently in
// real multi-application processes. Releasing both paths at the same time must
// complete without lock inversion while preserving global/named separation.
TEST(logger_configuration_test, concurrent_logger_and_application_bootstrap_is_deadlock_free) {
    scratch_config_dir global_directory;
    global_directory.write("vsomeip_std.json", R"({
        "logging": { "console": "false", "level": "error" }
    })");
    scratch_config_dir named_directory;
    named_directory.write("vsomeip_std.json", R"({
        "network": "concurrent-named-network",
        "logging": { "console": "true", "level": "fatal" }
    })");

    env_guard global_configuration{"VSOMEIP_CONFIGURATION"};
    env_guard named_configuration{"VSOMEIP_CONFIGURATION_concurrent_bootstrap_test_app"};
    global_configuration.set(global_directory.path());
    named_configuration.set(named_directory.path());

    configuration_plugin_impl plugin;
    std::shared_ptr<configuration_impl> logger_configuration;
    std::shared_ptr<vsomeip_v3::configuration> application_configuration;

    run_concurrently(2, [&](std::size_t _index) {
        if (_index == 0) {
            logger_configuration = configuration_impl::get_logger_configuration();
        } else {
            application_configuration = plugin.get_configuration("concurrent_bootstrap_test_app", "");
        }
    });

    ASSERT_NE(logger_configuration, nullptr);
    ASSERT_NE(application_configuration, nullptr);
    EXPECT_EQ(application_configuration->get_network(), "concurrent-named-network");
    EXPECT_FALSE(logger_configuration->has_console_log());
    EXPECT_EQ(logger_configuration->get_loglevel(), vsomeip_v3::logger::level_e::LL_ERROR);
}

#endif // !ANDROID

} // namespace
