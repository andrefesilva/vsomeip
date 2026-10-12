// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/service_state.hpp"
#include "common/timeout_scale.hpp" // common::scaled_timeout

#include <vsomeip/vsomeip.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace vsomeip_v3::testing {

static std::string const routingmanager_name_{"routingmanagerd"};
static std::string const server_name_{"server"};
static std::string const client_name_{"client"};

struct test_router_lifecycle : public base_fake_socket_fixture {
    test_router_lifecycle() {
        use_configuration("multiple_client_one_process.json");
        create_app(routingmanager_name_);
        create_app(server_name_);
        create_app(client_name_);
    }

    void start_apps() {
        routingmanagerd_ = start_client(routingmanager_name_);
        ASSERT_NE(routingmanagerd_, nullptr);
        ASSERT_TRUE(await_connectable(routingmanager_name_));

        server_ = start_client(server_name_);
        ASSERT_NE(server_, nullptr);
        ASSERT_TRUE(server_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        server_->offer(service_instance_);

        client_ = start_client(client_name_);
        ASSERT_NE(client_, nullptr);
        ASSERT_TRUE(client_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    service_instance service_instance_{.service_ = 0x3344, .instance_ = 0x1, .major_ = 1, .minor_ = 0};

    app* routingmanagerd_{};
    app* server_{};
    app* client_{};
};

TEST_F(test_router_lifecycle, service_stopped_while_the_router_is_down_stays_unavailable_after_it_restarts) {
    start_apps();
    client_->request_service(service_instance_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::available(service_instance_)))
            << client_->availability_record_;
    client_->availability_record_.clear();

    // app::stop() only returns once the router is fully stopped. The app is kept.
    routingmanagerd_->stop();
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::unavailable(service_instance_)))
            << client_->availability_record_;

    // The router is down, so it never receives this stop offer.
    server_->stop_offer(service_instance_);

    client_->app_state_record_.clear();
    ASSERT_TRUE(routingmanagerd_->start());
    ASSERT_TRUE(await_connectable(routingmanager_name_));
    // once registered again, the client renews its request, which a router still holding a stale
    // offer answers with that offer
    ASSERT_TRUE(client_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

    // The record was cleared while the service was available, so an AS_AVAILABLE in it can only
    // come from a stale offer handed out by the restarted router.
    EXPECT_FALSE(client_->availability_record_.wait_for_any(service_availability::available(service_instance_),
                                                            common::scaled_timeout(std::chrono::milliseconds(500))))
            << client_->availability_record_;
}

} // namespace vsomeip_v3::testing
