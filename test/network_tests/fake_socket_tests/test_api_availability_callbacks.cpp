// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/attribute_recorder.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/service_state.hpp"
#include "common/timeout_scale.hpp"

#include <vsomeip/vsomeip.hpp>
#include <gtest/gtest.h>

#include <chrono>

namespace vsomeip_v3::testing {

static std::string const routingmanager_name_{"routingmanagerd"};
static std::string const server_name_{"server"};
static std::string const client_name_{"client"};

// Window used to show that a *second* callback does not follow. A duplicate would be queued by
// on_availability() before the first one is dispatched, so this is slack, not a race.
static constexpr auto no_duplicate_window_ = std::chrono::milliseconds(300);

/**
 * Asserts how many times an availability handler is invoked, not just that it was. The rest of
 * the suite only checks the latter and therefore cannot tell "notified" from "notified twice".
 */
struct test_api_availability_callbacks : public base_fake_socket_fixture {
    test_api_availability_callbacks() {
        use_configuration("multiple_client_one_process.json");
        create_app(routingmanager_name_);
        create_app(server_name_);
        create_app(client_name_);
    }

    void start_apps(service_instance const& _si) {
        routingmanagerd_ = start_client(routingmanager_name_);
        ASSERT_NE(routingmanagerd_, nullptr);
        ASSERT_TRUE(await_connectable(routingmanager_name_));

        server_ = start_client(server_name_);
        ASSERT_NE(server_, nullptr);
        ASSERT_TRUE(server_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        server_->offer(_si);

        client_ = start_client(client_name_);
        ASSERT_NE(client_, nullptr);
        ASSERT_TRUE(client_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    void expect_no_duplicate(attribute_recorder<service_availability>& _record, service_availability const& _value, char const* _what) {
        EXPECT_FALSE(_record.wait_for_count(_value, 2, common::scaled_timeout(no_duplicate_window_)))
                << _what << " was reported more than once: " << _record;
    }

    // Offered without a version, so the offer travels as ANY_MAJOR/ANY_MINOR.
    service_instance wildcard_version_{.service_ = 0x3344, .instance_ = 0x1};
    service_instance concrete_version_{.service_ = 0x3344, .instance_ = 0x1, .major_ = 1, .minor_ = 0};

    app* routingmanagerd_{};
    app* server_{};
    app* client_{};
};

TEST_F(test_api_availability_callbacks, available_is_reported_once_for_a_wildcard_version_offer) {
    start_apps(wildcard_version_);
    client_->request_service(wildcard_version_);

    auto const its_available = service_availability::available(wildcard_version_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(its_available)) << client_->availability_record_;
    expect_no_duplicate(client_->availability_record_, its_available, "AS_AVAILABLE");

    server_->stop_offer(wildcard_version_);

    auto const its_unavailable = service_availability::unavailable(wildcard_version_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(its_unavailable)) << client_->availability_record_;
    expect_no_duplicate(client_->availability_record_, its_unavailable, "AS_UNAVAILABLE");

    EXPECT_TRUE(client_->availability_record_.equals({its_available, its_unavailable})) << client_->availability_record_;
}

TEST_F(test_api_availability_callbacks, available_is_reported_once_for_a_concrete_version_offer) {
    start_apps(concrete_version_);
    client_->request_service(concrete_version_);

    auto const its_available = service_availability::available(concrete_version_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(its_available)) << client_->availability_record_;
    expect_no_duplicate(client_->availability_record_, its_available, "AS_AVAILABLE");

    EXPECT_TRUE(client_->availability_record_.equals({its_available})) << client_->availability_record_;
}

TEST_F(test_api_availability_callbacks, registering_a_second_handler_does_not_renotify_the_first) {
    // Two handlers on one client. The first is the (ANY_SERVICE, ANY_INSTANCE) one that app::start()
    // registers before anything is available; it reports into availability_record_. The second is
    // registered below, while the service is already up, and reports into bool_availability_record_.
    // Registering it replays what is already available - a replay meant for it alone.
    start_apps(wildcard_version_);

    client_->request_service(wildcard_version_);
    auto const its_available = service_availability::available(wildcard_version_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(its_available)) << client_->availability_record_;

    client_->register_availability_bool_handler({.service_ = wildcard_version_.service_, .instance_ = vsomeip::ANY_INSTANCE});

    EXPECT_TRUE(client_->bool_availability_record_.wait_for_last(service_state{wildcard_version_, true}))
            << "the second handler was not told about the already-available service: " << client_->bool_availability_record_;
    expect_no_duplicate(client_->availability_record_, its_available, "AS_AVAILABLE for the first handler");

    EXPECT_TRUE(client_->availability_record_.equals({its_available})) << client_->availability_record_;
    EXPECT_TRUE(client_->bool_availability_record_.equals({service_state{wildcard_version_, true}})) << client_->bool_availability_record_;
}

} // namespace vsomeip_v3::testing
