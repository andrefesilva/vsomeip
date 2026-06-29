// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/attribute_recorder.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/command_gate.hpp"
#include "helpers/command_record.hpp"
#include "helpers/ecu_config.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/fake_socket_factory.hpp"
#include "helpers/message_checker.hpp"
#include "helpers/service_state.hpp"
#include "helpers/someip_gate.hpp"

#include "sample_configurations.hpp"

#include <vsomeip/enumeration_types.hpp>
#include <vsomeip/vsomeip.hpp>

#include <gtest/gtest.h>

#include <boost/asio/error.hpp>

#include <cstdint>
#include <cstdlib>
#include <utility>

namespace vsomeip_v3::testing {

struct test_multiversion_support_single_ecu : public base_fake_socket_fixture {
    void start_apps() {
        ecu_.add_app(client_v1_name_);
        ecu_.add_app(client_v2_name_);
        ecu_.add_app(server_v1_name_);
        ecu_.add_app(server_v2_name_);

        ecu_.prepare();
        ecu_.start_apps();

        server_v1_ = ecu_.apps_[server_v1_name_];
        server_v2_ = ecu_.apps_[server_v2_name_];
        client_v1_ = ecu_.apps_[client_v1_name_];
        client_v2_ = ecu_.apps_[client_v2_name_];
    }

    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};
    std::string const server_v1_name_{"server_v1"};
    std::string const server_v2_name_{"server_v2"};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {},
                            {interface::event_spec{0x8001, 0x8001, vsomeip::reliability_type_e::RT_UNRELIABLE}}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {},
                            {interface::event_spec{0x8002, 0x8002, vsomeip::reliability_type_e::RT_UNRELIABLE}}};

    ecu_setup ecu_{"single-ecu", ecu_config{boardnet::ecu_one_config}, *socket_manager_};

    app* server_v1_;
    app* server_v2_;
    app* client_v1_;
    app* client_v2_;
};

TEST_F(test_multiversion_support_single_ecu, when_v1_is_offered_first_then_only_client_v1_receives_availability) {
    start_apps();

    server_v1_->offer(interface_v1_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                                std::chrono::milliseconds(10)));
}
TEST_F(test_multiversion_support_single_ecu, when_v1_is_offered_last_then_only_client_v1_receives_availability) {
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    server_v1_->offer(interface_v1_.instance_);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                                std::chrono::milliseconds(10)));
}
TEST_F(test_multiversion_support_single_ecu, when_v2_is_offered_first_then_only_client_v2_receives_availability) {
    start_apps();

    server_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                                std::chrono::milliseconds(10)));
}
TEST_F(test_multiversion_support_single_ecu, when_v2_is_offered_last_then_only_client_v2_receives_availability) {
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    server_v2_->offer(interface_v2_.instance_);

    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                                std::chrono::milliseconds(10)));
}
}
