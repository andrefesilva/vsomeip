// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <vsomeip/internal/logger.hpp>

#include "../../../implementation/configuration/include/configuration.hpp"
#include "../../../implementation/configuration/include/configuration_plugin_impl.hpp"
#include "../../../implementation/logger/include/logger_impl.hpp"

namespace {

using vsomeip_v3::configuration_plugin_impl;
using vsomeip_v3::logger::level_e;
using vsomeip_v3::logger::logger_impl;

// An application on the global source is loaded before the first message, so the
// logger takes its settings from that parse. A named source loaded afterwards keeps
// its own logging values and must not alter the process logger.
TEST(logger_configuration_handoff_test, application_first_hands_global_settings_to_logger) {
    configuration_plugin_impl plugin;

    const auto global_configuration = plugin.get_configuration("handoff_global_app", "");
    ASSERT_NE(global_configuration, nullptr);
    EXPECT_EQ(global_configuration->get_network(), "handoff-global-network");

    const auto& logger_configuration = logger_impl::get()->get_configuration();
    EXPECT_FALSE(logger_configuration.console_enabled);
    EXPECT_FALSE(logger_configuration.file_enabled);
    EXPECT_FALSE(logger_configuration.dlt_enabled);
    EXPECT_EQ(logger_configuration.loglevel, level_e::LL_ERROR);
    EXPECT_EQ(logger_configuration.logfile, "handoff-global.log");

    const auto named_configuration = plugin.get_configuration("handoff_named_app", "");
    ASSERT_NE(named_configuration, nullptr);
    EXPECT_EQ(named_configuration->get_network(), "handoff-named-network");
    EXPECT_TRUE(named_configuration->has_console_log());
    EXPECT_EQ(named_configuration->get_loglevel(), level_e::LL_FATAL);

    EXPECT_FALSE(logger_impl::get()->get_configuration().console_enabled);
    EXPECT_EQ(logger_impl::get()->get_configuration().loglevel, level_e::LL_ERROR);
    EXPECT_EQ(logger_impl::get()->get_configuration().logfile, "handoff-global.log");
}

} // namespace
