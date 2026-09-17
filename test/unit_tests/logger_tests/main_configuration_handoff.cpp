// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <common/test_main.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

void set_env(const char* _name, const std::string& _value) {
#ifdef _WIN32
    _putenv_s(_name, _value.c_str());
#else
    setenv(_name, _value.c_str(), 1);
#endif
}

void write_file(const std::filesystem::path& _path, const char* _content) {
    std::ofstream its_file{_path};
    its_file << _content;
}

} // namespace

// The logger settings are process-wide and immutable, so the application-first
// handoff is verified in its own executable: the global source is parsed by an
// application before any message is logged.
int main(int argc, char** argv) {
#ifdef _WIN32
    const auto process_id = _getpid();
#else
    const auto process_id = ::getpid();
#endif
    const auto test_directory = std::filesystem::temp_directory_path()
            / ("vsomeip_logger_configuration_handoff_tests_" + std::to_string(static_cast<long long>(process_id)));
    const auto global_directory = test_directory / "global";
    const auto named_directory = test_directory / "named";

    const auto setup = [&]() {
        std::filesystem::create_directories(global_directory);
        std::filesystem::create_directories(named_directory);
        write_file(global_directory / "vsomeip_std.json", R"({
            "network": "handoff-global-network",
            "logging": {
                "console": "false",
                "file": { "enable": "false", "path": "handoff-global.log" },
                "dlt": "false",
                "level": "error"
            }
        })");
        write_file(named_directory / "vsomeip_std.json", R"({
            "network": "handoff-named-network",
            "logging": {
                "console": "true",
                "file": { "enable": "true", "path": "handoff-named.log" },
                "dlt": "true",
                "level": "fatal"
            }
        })");
        set_env("VSOMEIP_CONFIGURATION", global_directory.string());
        set_env("VSOMEIP_CONFIGURATION_handoff_named_app", named_directory.string());
    };
    const auto teardown = [&]() {
#ifndef _WIN32
        std::error_code its_error;
        std::filesystem::remove_all(test_directory, its_error);
#endif
    };

    return test_main(argc, argv, std::chrono::seconds(0), true, setup, teardown);
}
