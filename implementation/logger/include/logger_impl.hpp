// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#ifdef USE_DLT
#include <dlt/dlt.h>
#endif

#include <vsomeip/internal/logger.hpp>

namespace vsomeip_v3 {

class configuration;

namespace logger {

class logger_impl {
public:
    static logger_impl* get();
    static void init(const configuration& _configuration);

    struct config {
        bool console_enabled;
        bool dlt_enabled;
        bool file_enabled;
        level_e loglevel;
        std::string logfile;
    };

    const config& get_configuration() const;

    void log_to_file(std::string_view _msg);

#ifdef USE_DLT
    void log_to_dlt(level_e _level, std::string_view _msg);
    DltContext dlt_context_{};
#endif

private:
    explicit logger_impl(config _config);
    static logger_impl* get_or_create(const configuration* _configuration);

    const config config_;

    std::mutex log_file_mutex_;
    std::ofstream log_file_;
};

} // namespace logger
} // namespace vsomeip_v3
