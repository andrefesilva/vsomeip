// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <iostream>
#include <sstream>
#include <string>

#include <vsomeip/internal/logger.hpp>

#include "../../../implementation/logger/include/logger_impl.hpp"

namespace {

using vsomeip_v3::logger::level_e;
using vsomeip_v3::logger::logger_impl;

// Redirects std::cout into a buffer so console output can be inspected.
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

// Guards the premise of this executable: the configuration written by main.cpp
// disables every sink. Without this, the tests below could pass vacuously.
TEST(logger_test, configuration_disables_all_sinks) {
    const auto& config = logger_impl::get()->get_configuration();
    EXPECT_FALSE(config.console_enabled);
    EXPECT_FALSE(config.file_enabled);
    EXPECT_FALSE(config.dlt_enabled);
    EXPECT_EQ(config.loglevel, level_e::LL_VERBOSE);
}

TEST(logger_test, message_produces_no_output_when_all_sinks_disabled) {
    // All sinks off: even FATAL produces nothing, despite the permissive level.
    cout_capture capture;
    { VSOMEIP_FATAL << "nothing-should-be-logged"; }
    EXPECT_TRUE(capture.str().empty());
}

TEST(logger_test, log_to_file_is_noop_when_file_disabled) {
    // With file logging disabled, writing must be a safe no-op rather than crashing.
    EXPECT_NO_THROW(logger_impl::get()->log_to_file("must-not-crash"));
}

} // namespace
