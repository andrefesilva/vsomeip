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

int main(int argc, char** argv) {
#ifdef _WIN32
    const auto process_id = _getpid();
#else
    const auto process_id = ::getpid();
#endif
    const auto test_directory =
            std::filesystem::temp_directory_path() / ("vsomeip_logger_tests_" + std::to_string(static_cast<long long>(process_id)));
    const auto configuration_file = test_directory / "vsomeip_std.json";
    const auto logfile = test_directory / "logger.log";

    const auto setup = [&]() {
        std::filesystem::create_directories(test_directory);
        std::ofstream configuration{configuration_file};
        configuration << R"({
            "logging": {
                "console": "true",
                "file": { "enable": "true", "path": ")"
                      << logfile.generic_string() << R"(" },
                "dlt": "true",
                "level": "warning"
            }
        })";
#ifdef _WIN32
        _putenv_s("VSOMEIP_CONFIGURATION", test_directory.string().c_str());
        _putenv_s("VSOMEIP_LOGGER_TEST_FILE", logfile.string().c_str());
#else
        setenv("VSOMEIP_CONFIGURATION", test_directory.string().c_str(), 1);
        setenv("VSOMEIP_LOGGER_TEST_FILE", logfile.string().c_str(), 1);
#endif
    };

    const auto teardown = [&]() {
#ifndef _WIN32
        std::error_code its_error;
        std::filesystem::remove_all(test_directory, its_error);
#endif
    };

    return test_main(argc, argv, std::chrono::seconds(0), true, setup, teardown);
}
