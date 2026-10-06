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

#include "common/timeout_scale.hpp"
#include "vsomeip/constants.hpp"

#include <chrono>
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
        ecu_.add_app(prope_client_name_);
        ecu_.add_app(extra_app_one_name_);
        ecu_.add_app(extra_app_two_name_);

        ecu_.prepare();
        ecu_.start_apps();

        server_v1_ = ecu_.apps_[server_v1_name_];
        server_v2_ = ecu_.apps_[server_v2_name_];
        client_v1_ = ecu_.apps_[client_v1_name_];
        client_v2_ = ecu_.apps_[client_v2_name_];
        prope_client_ = ecu_.apps_[prope_client_name_];
        extra_app_one_ = ecu_.apps_[extra_app_one_name_];
        extra_app_two_ = ecu_.apps_[extra_app_two_name_];
    }

    std::string const prope_client_name_{"prope_client"};
    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};
    std::string const server_v1_name_{"server_v1"};
    std::string const server_v2_name_{"server_v2"};
    std::string const extra_app_one_name_{"extra_app_one"};
    std::string const extra_app_two_name_{"extra_app_two"};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {},
                            {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_UNRELIABLE}}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {},
                            {event_spec{0x8002, {0x8002}, vsomeip::reliability_type_e::RT_UNRELIABLE}}};

    ecu_setup ecu_{"single-ecu", ecu_config{boardnet::ecu_one_config}, *socket_manager_};

    app* server_v1_;
    app* server_v2_;
    app* prope_client_;
    app* client_v1_;
    app* client_v2_;
    app* extra_app_one_;
    app* extra_app_two_;
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

TEST_F(test_multiversion_support_single_ecu, when_v1_and_v2_are_offered_first_both_become_available) {
    start_apps();

    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}
TEST_F(test_multiversion_support_single_ecu, when_v1_and_v2_are_offered_last_both_become_available) {
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}

TEST_F(test_multiversion_support_single_ecu, v1_and_v2_offered_when_v1_is_withdrawn_only_v2_is_available) {
    start_apps();
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);

    prope_client_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(prope_client_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    server_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(prope_client_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                                std::chrono::milliseconds(10)));
}

TEST_F(test_multiversion_support_single_ecu, v1_and_v2_offered_when_v2_is_withdrawn_only_v1_is_available) {
    start_apps();
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);

    prope_client_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(prope_client_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    server_v2_->stop_offer(interface_v2_.instance_);
    ASSERT_TRUE(prope_client_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                                std::chrono::milliseconds(10)));
}

TEST_F(test_multiversion_support_single_ecu, when_v1_withdraws_only_v1_becomes_unavailable) {
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    server_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    ASSERT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                                std::chrono::milliseconds(10)));

    server_v1_->offer(interface_v1_.instance_);
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                                std::chrono::milliseconds(10)));
}
TEST_F(test_multiversion_support_single_ecu, when_v2_withdraws_only_v2_becomes_unavailable) {
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    server_v2_->stop_offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));
    ASSERT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                                std::chrono::milliseconds(10)));

    server_v2_->offer(interface_v2_.instance_);
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                                std::chrono::milliseconds(10)));
}

TEST_F(test_multiversion_support_single_ecu, when_v1_client_connection_breaks_then_v1_service_is_pinged) {
    std::shared_ptr<command_gate> gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(server_v1_name_, ecu_.router_name_, socket_role::client, gate->get_data_pipe()));
    start_apps();

    client_v1_->subscribe(interface_v1_);
    client_v2_->request_service(interface_v2_.instance_);
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v1_->subscription_record_.wait_for_last(
            event_subscription::successfully_subscribed_to({interface_v1_.instance_, interface_v1_.fields_[0]})));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    gate->block_at(vsomeip_v3::protocol::id_e::PING_ID);
    ASSERT_TRUE(disconnect(client_v1_name_, boost::asio::error::timed_out, server_v1_name_, boost::asio::error::connection_reset));

    EXPECT_TRUE(gate->wait_for_blocked());
}
TEST_F(test_multiversion_support_single_ecu, when_v1_client_connection_breaks_then_v1_service_returns_available) {
    start_apps();

    client_v1_->subscribe(interface_v1_);
    server_v1_->offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v1_->subscription_record_.wait_for_last(
            event_subscription::successfully_subscribed_to({interface_v1_.instance_, interface_v1_.fields_[0]})));
    client_v1_->availability_record_.clear();

    ASSERT_TRUE(disconnect(client_v1_name_, boost::asio::error::timed_out, server_v1_name_, boost::asio::error::connection_reset));

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    // the availability handler can not tell us what version is available, because we don't receive it. But if the "wrong" version is noted
    // in the router, we would be told about a different version turning available
    client_v1_->availability_record_.clear();
    server_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    server_v2_->offer(interface_v2_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                                std::chrono::milliseconds(10)));
}
TEST_F(test_multiversion_support_single_ecu, when_v2_client_connection_breaks_then_v2_service_is_pinged) {
    std::shared_ptr<command_gate> gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(server_v2_name_, ecu_.router_name_, socket_role::client, gate->get_data_pipe()));
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->subscribe(interface_v2_);
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    ASSERT_TRUE(client_v2_->subscription_record_.wait_for_last(
            event_subscription::successfully_subscribed_to({interface_v2_.instance_, interface_v2_.fields_[0]})));
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    gate->block_at(vsomeip_v3::protocol::id_e::PING_ID);
    ASSERT_TRUE(disconnect(client_v2_name_, boost::asio::error::timed_out, server_v2_name_, boost::asio::error::connection_reset));

    EXPECT_TRUE(gate->wait_for_blocked());
}
TEST_F(test_multiversion_support_single_ecu, when_v2_client_connection_breaks_then_v2_service_returns_available) {
    start_apps();

    client_v2_->subscribe(interface_v2_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    ASSERT_TRUE(client_v2_->subscription_record_.wait_for_last(
            event_subscription::successfully_subscribed_to({interface_v2_.instance_, interface_v2_.fields_[0]})));
    client_v2_->availability_record_.clear();

    ASSERT_TRUE(disconnect(client_v2_name_, boost::asio::error::timed_out, server_v2_name_, boost::asio::error::connection_reset));

    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    // the availability handler can not tell us what version is available, because we don't receive it. But if the "wrong" version is noted
    // in the router, we would be told about a different version turning available
    client_v2_->availability_record_.clear();
    server_v2_->stop_offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));

    server_v1_->offer(interface_v1_.instance_);
    client_v1_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                                std::chrono::milliseconds(10)));
}

TEST_F(test_multiversion_support_single_ecu, when_another_service_tries_to_also_offer_v1_then_v1_is_pinged) {
    std::shared_ptr<command_gate> gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(server_v1_name_, ecu_.router_name_, socket_role::client, gate->get_data_pipe()));
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    gate->block_at(vsomeip_v3::protocol::id_e::PING_ID);
    prope_client_->offer(interface_v1_.instance_);

    EXPECT_TRUE(gate->wait_for_blocked());
}
TEST_F(test_multiversion_support_single_ecu, when_another_service_tries_to_also_offer_v2_then_v2_is_pinged) {
    std::shared_ptr<command_gate> gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(server_v2_name_, ecu_.router_name_, socket_role::client, gate->get_data_pipe()));
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    gate->block_at(vsomeip_v3::protocol::id_e::PING_ID);
    prope_client_->offer(interface_v2_.instance_);

    EXPECT_TRUE(gate->wait_for_blocked());
}

TEST_F(test_multiversion_support_single_ecu, when_v1_and_v2_are_reofferd_both_incumbents_are_pinged) {
    std::shared_ptr<command_gate> gate_v1 = command_gate::create();
    std::shared_ptr<command_gate> gate_v2 = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(server_v1_name_, ecu_.router_name_, socket_role::client, gate_v1->get_data_pipe()));
    ASSERT_TRUE(setup_data_pipe(server_v2_name_, ecu_.router_name_, socket_role::client, gate_v2->get_data_pipe()));
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    server_v1_->offer(interface_v1_.instance_);
    server_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    gate_v1->block_at(vsomeip_v3::protocol::id_e::PING_ID);
    gate_v2->block_at(vsomeip_v3::protocol::id_e::PING_ID);

    // Note by firing these two in a row in some test executions the order is swapped at the router side
    // this is "by-design", because if the test would fail orderly dependent we would have a flaky test
    extra_app_one_->offer(interface_v1_.instance_);
    extra_app_two_->offer(interface_v2_.instance_);

    EXPECT_TRUE(gate_v1->wait_for_blocked());
    EXPECT_TRUE(gate_v2->wait_for_blocked());
}

/**
 * Three-ECU topology isolating the *consuming* ECU's ability to independently track two
 * remote offers of the same (service, instance) discovered from two different hosts:
 *
 *   - provider_v1_ecu_ offers major v1 with real boardnet ports (never offers v2 itself).
 *   - provider_v2_ecu_ offers major v2 with real boardnet ports (never offers v1 itself).
 *   - consumer_ecu_ only ever requests/consumes, never offers.
 *
 * Neither offering ECU offers more than one major itself, so this does not exercise a
 * single host announcing multiple majors of the same (service, instance) via SD (a
 * separate, not yet supported concern) -- it purely tests the consumer's bookkeeping of
 * two independently-discovered remote offers.
 */
struct test_multiversion_support_aggregate_consumption : public base_fake_socket_fixture {
    void start_apps() {
        provider_v1_ecu_.add_app(provider_v1_name_);
        provider_v2_ecu_.add_app(provider_v2_name_);
        consumer_ecu_.add_app(client_v1_name_);
        consumer_ecu_.add_app(client_v2_name_);
        consumer_ecu_.add_app("client_any");

        provider_v1_ecu_.prepare();
        provider_v2_ecu_.prepare();
        consumer_ecu_.prepare();

        provider_v1_ecu_.start_apps();
        provider_v2_ecu_.start_apps();
        consumer_ecu_.start_apps();

        provider_v1_ = provider_v1_ecu_.apps_[provider_v1_name_];
        provider_v2_ = provider_v2_ecu_.apps_[provider_v2_name_];
        client_v1_ = consumer_ecu_.apps_[client_v1_name_];
        client_v2_ = consumer_ecu_.apps_[client_v2_name_];
        client_any_ = consumer_ecu_.apps_["client_any"];

        ASSERT_TRUE(provider_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(provider_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_any_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {},
                            {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_RELIABLE}}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {},
                            {event_spec{0x8002, {0x8002}, vsomeip::reliability_type_e::RT_RELIABLE}}};

    vsomeip::method_t const method_{0x1111};
    request request_v1_{interface_v1_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {}};
    request request_v2_{interface_v2_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {}};

    // The SD multicast group address (VSOMEIP_SD_DEFAULT_MULTICAST); none of the ecu_configs used
    // here override it. FindService/OfferService sent to this address still go out as an ordinary
    // sendto() on the sender's own unicast SD socket -- joining the group is only needed to
    // *receive* multicast traffic, so gating on this as the receiver party does not depend on
    // whether the multicast join has completed.
    boost::asio::ip::udp::endpoint const sd_multicast_ep_{boost::asio::ip::make_address("224.224.224.0"), 30490};

    // provider_v1_ecu_/provider_v2_ecu_ each offer only their own major -- reusing the
    // client-only boardnet::ecu_one_config/ecu_two_config bases (stripped of the unrelated
    // service_3344 baked into ecu_two_config) purely for their distinct unicast IP/router
    // name/network; consumer_ecu_ never offers anything.
    //
    // Each ECU gets its own diagnosis address so their auto-assigned client ids don't collide --
    // utility::request_client_id() reads diagnosis/mask fresh from _config per call (no longer
    // function-local-static-cached), so this now gives each ECU its own non-overlapping range.
    ecu_setup provider_v1_ecu_{"provider_v1", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup provider_v2_ecu_{"provider_v2", ecu_config{boardnet::ecu_two_config}.add_interface({interface_v2_}), *socket_manager_};
    // consumer_ecu_ never offers anything.
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* provider_v1_;
    app* provider_v2_;
    app* client_v1_;
    app* client_v2_;
    app* client_any_;
};

TEST_F(test_multiversion_support_aggregate_consumption, both_majors_offered_from_different_ecus_become_independently_available) {
    start_apps();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}

TEST_F(test_multiversion_support_aggregate_consumption, releasing_v2_then_any_major_eventually_drops_v2s_connection_but_never_v1s) {
    start_apps();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    service_instance const any_major{.service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_};
    client_any_->request_service(any_major);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    ASSERT_TRUE(client_any_->availability_record_.wait_for_last(service_availability::available(any_major)));

    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_));
    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_));

    auto drop_watch_v1 = watch_connection_drop(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_);
    auto drop_watch_v2 = watch_connection_drop(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_);

    client_v2_->release_service(interface_v2_.instance_);
    EXPECT_FALSE(drop_watch_v2.wait(std::chrono::milliseconds(500)))
            << "v2's connection dropped even though client_any_ (ANY_MAJOR) could still need it";
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(0))) << "v1's connection must never be touched by releasing v2";

    client_any_->release_service(any_major);
    EXPECT_TRUE(drop_watch_v2.wait(std::chrono::milliseconds(500)))
            << "v2's connection was never dropped after its last possible requester (client_any_) released -- "
               "release_service() only re-checked the one major client_any_ happened to be bound to, leaking v2's "
               "already-abandoned connection forever";
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(0)))
            << "v1's connection must survive: client_v1_ still concretely requests it";
}

// Mirror of releasing_v2_then_any_major_eventually_drops_v2s_connection_but_never_v1s with the
// roles of v1 and v2 swapped, to guard against a fix that happens to only work for whichever
// major find_service()'s ANY_MAJOR resolution (lowest offered major) happens to bind to.
TEST_F(test_multiversion_support_aggregate_consumption, releasing_v1_then_any_major_eventually_drops_v1s_connection_but_never_v2s) {
    start_apps();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    service_instance const any_major{.service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_};
    client_any_->request_service(any_major);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    ASSERT_TRUE(client_any_->availability_record_.wait_for_last(service_availability::available(any_major)));

    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_));
    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_));

    auto drop_watch_v1 = watch_connection_drop(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_);
    auto drop_watch_v2 = watch_connection_drop(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_);

    client_v1_->release_service(interface_v1_.instance_);
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(500)))
            << "v1's connection dropped even though client_any_ (ANY_MAJOR) could still need it";
    EXPECT_FALSE(drop_watch_v2.wait(std::chrono::milliseconds(0))) << "v2's connection must never be touched by releasing v1";

    client_any_->release_service(any_major);
    EXPECT_TRUE(drop_watch_v1.wait(std::chrono::milliseconds(500)))
            << "v1's connection was never dropped after its last possible requester (client_any_) released -- "
               "release_service() only re-checked the one major client_any_ happened to be bound to, leaking v1's "
               "already-abandoned connection forever";
    EXPECT_FALSE(drop_watch_v2.wait(std::chrono::milliseconds(0)))
            << "v2's connection must survive: client_v2_ still concretely requests it";
}

TEST_F(test_multiversion_support_aggregate_consumption, stopping_v1_offer_does_not_disturb_v2_in_aggregate_setup) {
    start_apps();

    std::vector<unsigned char> const payload_v2{0xA1, 0xB2};
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    provider_v1_->stop_offer(interface_v1_.instance_);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                                std::chrono::milliseconds(250)));

    client_v2_->send_request(request_v2_);
    EXPECT_TRUE(client_v2_->wait_for_messages({payload_v2}));
}

TEST_F(test_multiversion_support_aggregate_consumption, stopping_v2_offer_does_not_disturb_v1_in_aggregate_setup) {
    start_apps();

    std::vector<unsigned char> const payload_v1{0xC3, 0xD4};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    provider_v2_->stop_offer(interface_v2_.instance_);

    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                                std::chrono::milliseconds(250)));

    client_v1_->send_request(request_v1_);
    EXPECT_TRUE(client_v1_->wait_for_messages({payload_v1}));
}

TEST_F(test_multiversion_support_aggregate_consumption, request_to_each_major_is_routed_to_the_matching_remote_provider) {
    start_apps();

    std::vector<unsigned char> const payload_v1{0x01, 0x02};
    std::vector<unsigned char> const payload_v2{0x03, 0x04};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->send_request(request_v1_);
    client_v2_->send_request(request_v2_);

    EXPECT_TRUE(client_v1_->wait_for_messages({payload_v1}));
    EXPECT_TRUE(client_v2_->wait_for_messages({payload_v2}));
}

TEST_F(test_multiversion_support_aggregate_consumption, bringing_v1_provider_ecu_routing_down_expires_only_v1_and_keeps_v2_available) {
    start_apps();

    // Bring one offering ECU fully off-boardnet. This models "one ECU goes down" regardless of
    // service reliability and avoids depending on router-name TCP connection wiring in the test.

    std::vector<unsigned char> const payload_v2{0x33, 0x44};
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    ASSERT_TRUE(provider_v1_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));

    // note we are using ttl expiry here to ensure the path in which a STOP_OFFER had not been send out.
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(30))));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                                std::chrono::milliseconds(250)));

    client_v2_->send_request(request_v2_);
    EXPECT_TRUE(client_v2_->wait_for_messages({payload_v2}));
}

// Mirror of bringing_v1_provider_ecu_routing_down_expires_only_v1_and_keeps_v2_available with the
// roles of v1 and v2 swapped, to guard against a fix that happens to only work for whichever major
// sorts first.
TEST_F(test_multiversion_support_aggregate_consumption, bringing_v2_provider_ecu_routing_down_expires_only_v2_and_keeps_v1_available) {
    start_apps();

    std::vector<unsigned char> const payload_v1{0x55, 0x66};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    ASSERT_TRUE(provider_v2_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));

    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(30))));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                                std::chrono::milliseconds(250)));

    client_v1_->send_request(request_v1_);
    EXPECT_TRUE(client_v1_->wait_for_messages({payload_v1}));
}

TEST_F(test_multiversion_support_aggregate_consumption,
       bringing_v1_provider_ecu_routing_down_does_not_corrupt_v2_client_endpoint_bookkeeping) {
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, gate->get_data_pipe()));

    provider_v2_ecu_.config().service_discovery_.ttl_ = 60;

    start_apps();

    std::vector<unsigned char> const payload_v2{0x77, 0x88};
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_));
    auto drop_watch_v2 = watch_connection_drop(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_);

    // Block every incoming SD message to consumer_ecu_ from here on: this does not stop v1's TTL
    // from expiring (that is a purely time-based decrement, independent of incoming traffic), but
    // it does prevent provider_v2's next cyclic re-offer from silently repairing v2's bookkeeping
    // before we get to probe it below.
    gate->block_at(someip_gate::sd_predicate{[](auto const&) { return true; }});

    ASSERT_TRUE(provider_v1_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(30))));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                                std::chrono::milliseconds(250)));

    EXPECT_FALSE(drop_watch_v2.wait(std::chrono::milliseconds(0)))
            << "v2's reliable boardnet connection was dropped by v1's TTL expiry -- clear_client_endpoints() matched "
               "v2's entry purely by (service, instance), ignoring major";

    client_v2_->send_request(request_v2_);
    EXPECT_TRUE(client_v2_->wait_for_messages({payload_v2}));
}

TEST_F(test_multiversion_support_aggregate_consumption,
       bringing_v2_provider_ecu_routing_down_does_not_corrupt_v1_client_endpoint_bookkeeping) {
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, gate->get_data_pipe()));

    provider_v1_ecu_.config().service_discovery_.ttl_ = 60;

    start_apps();

    std::vector<unsigned char> const payload_v1{0x77, 0x88};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_));
    auto drop_watch_v1 = watch_connection_drop(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_);

    // Block every incoming SD message to consumer_ecu_ from here on: this does not stop v2's TTL
    // from expiring (that is a purely time-based decrement, independent of incoming traffic), but
    // it does prevent provider_v1's next cyclic re-offer from silently repairing v1's bookkeeping
    // before we get to probe it below.
    gate->block_at(someip_gate::sd_predicate{[](auto const&) { return true; }});

    ASSERT_TRUE(provider_v2_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));

    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(30))));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                                std::chrono::milliseconds(250)));

    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(0)))
            << "v1's reliable boardnet connection was dropped by v2's TTL expiry -- clear_client_endpoints() matched "
               "v1's entry purely by (service, instance), ignoring major";

    client_v1_->send_request(request_v1_);
    EXPECT_TRUE(client_v1_->wait_for_messages({payload_v1}));
}

TEST_F(test_multiversion_support_aggregate_consumption,
       local_offer_of_a_remotely_known_major_is_rejected_and_request_still_reaches_remote_provider) {
    consumer_ecu_.add_app("local_pretender_v2");
    start_apps();
    app* local_pretender_v2 = consumer_ecu_.apps_["local_pretender_v2"];

    provider_v2_->offer(interface_v2_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    // Attempt to ALSO offer the same (service, instance, major) locally on the consumer ECU,
    // where it is already known remotely (via provider_v2). Must be rejected internally.
    local_pretender_v2->offer(interface_v2_.instance_);

    client_v2_->send_request(request_v2_);

    message_checker const request_checker{.client_session_ = std::nullopt,
                                          .service_instance_ = interface_v2_.instance_,
                                          .method_ = method_,
                                          .message_type_ = vsomeip::message_type_e::MT_REQUEST};
    EXPECT_TRUE(provider_v2_->message_record_.wait_for(request_checker)) << provider_v2_->message_record_;
    EXPECT_FALSE(local_pretender_v2->message_record_.wait_for(request_checker, std::chrono::milliseconds(200)))
            << local_pretender_v2->message_record_;
}

TEST_F(test_multiversion_support_aggregate_consumption, when_neither_major_is_offered_both_are_still_found_independently) {
    // Both requesters ask before either provider offers anything -- this exercises the
    // consumer's outgoing SD FindService traffic itself, not just availability bookkeeping
    // after the fact. Both majors must be found -- but NOT necessarily in the same SD message:
    // the debounce timer is a single, continuously-running loop (not restarted per-request), so
    // whether two back-to-back requests land in the same firing is a race against the timer's
    // existing phase, not something a wider debounce window can fix. Requiring "same message"
    // would test a stronger guarantee than this implementation actually provides.
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, gate->get_data_pipe()));
    start_apps();

    // Predicate carries state across calls (safe: someip_gate invokes it under its own mutex),
    // accumulating "seen" across however many separate messages the two Finds end up in.
    struct seen_state {
        bool has_v1_{false};
        bool has_v2_{false};
    };
    auto seen = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen->has_v1_ |= e->get_major_version() == 0x1;
                seen->has_v2_ |= e->get_major_version() == 0x2;
            }
        }
        return seen->has_v1_ && seen->has_v2_;
    }});

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    EXPECT_TRUE(gate->wait_for_blocked());
}

TEST_F(test_multiversion_support_aggregate_consumption, when_v1_and_v2_are_requested_both_are_found_before_and_after_suspend_resume) {

    consumer_ecu_.config().service_discovery_.repetitions_max_ =
            0; // this guarantess that FIND_OFFERS are not leaking across suspend/resume
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    start_apps();

    struct seen_state {
        bool has_v1_{false};
        bool has_v2_{false};
    };

    auto seen_before_suspend = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_before_suspend](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_before_suspend->has_v1_ |= e->get_major_version() == 0x1;
                seen_before_suspend->has_v2_ |= e->get_major_version() == 0x2;
            }
        }
        return seen_before_suspend->has_v1_ && seen_before_suspend->has_v2_;
    }});

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(30))));
    gate->block(false);

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);

    auto seen_after_resume = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_after_resume](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_after_resume->has_v1_ |= e->get_major_version() == 0x1;
                seen_after_resume->has_v2_ |= e->get_major_version() == 0x2;
            }
        }
        return seen_after_resume->has_v1_ && seen_after_resume->has_v2_;
    }});

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    EXPECT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(30))));
}

TEST_F(test_multiversion_support_aggregate_consumption, when_only_v1_is_offered_only_v1_becomes_available) {
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    provider_v1_->offer(interface_v1_.instance_);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                                std::chrono::milliseconds(10)));
}

TEST_F(test_multiversion_support_aggregate_consumption, when_only_v2_is_offered_only_v2_becomes_available) {
    start_apps();

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    provider_v2_->offer(interface_v2_.instance_);

    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                                std::chrono::milliseconds(10)));
}

// Regression test for a latent major-scoping gap in reset_request_sent_counter(): it resets the
// FindService sent-counter for every major requested for a (service, instance), not just the one
// whose TTL expired, so a still-available major gets spuriously re-found too.
TEST_F(test_multiversion_support_aggregate_consumption, bringing_v2_provider_ecu_routing_down_triggers_a_find_only_for_v2_afterwards) {
    consumer_ecu_.config().service_discovery_.repetitions_max_ = 0;

    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, gate->get_data_pipe()));
    start_apps();

    struct seen_state {
        bool has_v1_{false};
        bool has_v2_{false};
    };

    auto seen_initial = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_initial](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_initial->has_v1_ |= e->get_major_version() == 0x1;
                seen_initial->has_v2_ |= e->get_major_version() == 0x2;
            }
        }
        return seen_initial->has_v1_ && seen_initial->has_v2_;
    }});
    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(30))));
    gate->block(false);

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    auto seen_after = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_after](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_after->has_v1_ |= e->get_major_version() == 0x1;
                seen_after->has_v2_ |= e->get_major_version() == 0x2;
            }
        }
        return seen_after->has_v1_ || seen_after->has_v2_;
    }});

    ASSERT_TRUE(provider_v2_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));

    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(30))));
    EXPECT_TRUE(seen_after->has_v2_);
    EXPECT_FALSE(seen_after->has_v1_);
}

// Mirror of bringing_v2_provider_ecu_routing_down_triggers_a_find_only_for_v2_afterwards, roles reversed.
TEST_F(test_multiversion_support_aggregate_consumption, bringing_v1_provider_ecu_routing_down_triggers_a_find_only_for_v1_afterwards) {
    consumer_ecu_.config().service_discovery_.repetitions_max_ = 0;

    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, gate->get_data_pipe()));
    start_apps();

    struct seen_state {
        bool has_v1_{false};
        bool has_v2_{false};
    };

    auto seen_initial = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_initial](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_initial->has_v1_ |= e->get_major_version() == 0x1;
                seen_initial->has_v2_ |= e->get_major_version() == 0x2;
            }
        }
        return seen_initial->has_v1_ && seen_initial->has_v2_;
    }});
    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(30))));
    gate->block(false);

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    auto seen_after = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_after](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_after->has_v1_ |= e->get_major_version() == 0x1;
                seen_after->has_v2_ |= e->get_major_version() == 0x2;
            }
        }
        return seen_after->has_v1_ || seen_after->has_v2_;
    }});

    ASSERT_TRUE(provider_v1_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));

    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(30))));
    EXPECT_TRUE(seen_after->has_v1_);
    EXPECT_FALSE(seen_after->has_v2_);
}

TEST_F(test_multiversion_support_aggregate_consumption, any_major_request_still_finds_again_after_ttl_expiry) {
    consumer_ecu_.config().service_discovery_.repetitions_max_ = 0; // ensure exactly one find_offer is send after request without offer

    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, gate->get_data_pipe()));
    start_apps();

    service_instance const any_major_v1{
            .service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_, .major_ = ANY_MAJOR};
    client_v1_->request_service(any_major_v1);
    provider_v1_->offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    gate->block_at(someip_gate::sd_predicate{[](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                return true;
            }
        }
        return false;
    }});

    ASSERT_TRUE(provider_v1_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));

    EXPECT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(5))))
            << "consumer never re-sent a FindService after v1's TTL expired -- the ANY_MAJOR-keyed request's "
               "sent-counter was never reset";
}

TEST_F(test_multiversion_support_aggregate_consumption, any_major_request_stops_finding_again_once_the_concrete_offer_arrives) {
    // Needs the FindService repetition phase to still be in flight when the offer arrives (see the
    // comment on the fixture below) -- vsomeip's own built-in repetition cadence is fast (10ms),
    // too close to the offer's own processing latency to reliably observe update_request() winning
    // the race, so this test configures its own, deliberately slower cadence instead of relying on
    // whatever ecu_config's default happens to be.
    auto const cycle_time = common::scaled_timeout(std::chrono::milliseconds(400));
    consumer_ecu_.config().service_discovery_.repetitions_base_delay_ = static_cast<uint32_t>(cycle_time.count());

    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, gate->get_data_pipe()));
    start_apps();

    // Arm from the very start, before requesting at all -- not after availability is confirmed.
    // Arming only after wait_for_last(available) races against the debounce timer: an already
    // in-flight repetition tick (scheduled before the offer's update_request() call takes effect)
    // can slip out during the gap between update_request() running and this test thread observing
    // availability, producing a flaky false failure unrelated to the fix. Counting occurrences and
    // only blocking on the *second* FindService sidesteps the race entirely: the very first Find is
    // the expected, legitimate one sent immediately upon request_service(), before any offer exists.
    auto seen_count = std::make_shared<int>(0);
    gate->block_at(someip_gate::sd_predicate{[seen_count](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                return ++(*seen_count) > 1;
            }
        }
        return false;
    }});

    service_instance const any_major_v1{
            .service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_, .major_ = ANY_MAJOR};
    client_v1_->request_service(any_major_v1);
    provider_v1_->offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    // The first (unsuppressed) repetition tick is scheduled at exactly cycle_time after the initial
    // Find (2^0 * repetitions_base_delay); waiting for 2x that gives headroom against scheduling
    // jitter while still reliably catching a regression at its actual scheduled time.
    EXPECT_FALSE(gate->wait_for_blocked(cycle_time * 2))
            << "consumer sent a second FindService after the offer arrived -- update_request()'s ANY_MAJOR-keyed "
               "request was never marked as found, so its repetition phase never stopped";
}

TEST_F(test_multiversion_support_aggregate_consumption, releasing_v1_does_not_disturb_v2_and_only_v1_becomes_available_again) {
    start_apps();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    client_v1_->release_service(interface_v1_.instance_);

    // Re-requesting v1 is the observable signal that the release actually completed: it should
    // become available again exactly as if freshly requested.
    client_v1_->request_service(interface_v1_.instance_);
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    // v2 was never touched: releasing (and re-requesting) v1 must not have produced any record for
    // it at all.
    EXPECT_TRUE(client_v2_->availability_record_.equals({})) << client_v2_->availability_record_;
}

// Mirror of releasing_v1_does_not_disturb_v2_and_only_v1_becomes_available_again with the roles of
// v1 and v2 swapped, to guard against a fix that happens to only work for whichever major sorts
// first.
TEST_F(test_multiversion_support_aggregate_consumption, releasing_v2_does_not_disturb_v1_and_only_v2_becomes_available_again) {
    start_apps();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    client_v2_->release_service(interface_v2_.instance_);

    // Re-requesting v2 is the observable signal that the release actually completed: it should
    // become available again exactly as if freshly requested.
    client_v2_->request_service(interface_v2_.instance_);
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    // v1 was never touched: releasing (and re-requesting) v2 must not have produced any record for
    // it at all.
    EXPECT_TRUE(client_v1_->availability_record_.equals({})) << client_v1_->availability_record_;
}

TEST_F(test_multiversion_support_aggregate_consumption, releasing_v1_drops_it_from_the_post_resume_find_but_not_v2) {
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    auto in_gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, in_gate->get_data_pipe()));

    start_apps();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->release_service(interface_v1_.instance_);

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    // Quiesce provider-side offers before suspend/resume so the post-resume FindService
    // observation does not race against immediate incoming OfferService.
    provider_v1_->stop_offer(interface_v1_.instance_);
    provider_v2_->stop_offer(interface_v2_.instance_);

    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(10))));

    in_gate->block(true);

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);

    auto seen_majors = std::make_shared<attribute_recorder<vsomeip::major_version_t>>();
    gate->block_at(someip_gate::sd_predicate{[seen_majors](auto const& msg) {
        bool matched = false;
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_majors->record(e->get_major_version());
                matched = true;
            }
        }
        return matched;
    }});

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(10))));
    EXPECT_TRUE(seen_majors->wait_for_any(vsomeip::major_version_t{0x2}, std::chrono::milliseconds(0)))
            << "v2 is still requested -- it must be found again after resume";
    EXPECT_FALSE(seen_majors->wait_for_any(vsomeip::major_version_t{0x1}, std::chrono::milliseconds(0)))
            << "v1 was released -- it must not be re-requested after resume";
    gate->block(false);
    in_gate->get_data_pipe()->clear();
    in_gate->block(false);

    // Restore only the still-requested major: v2 should become available again, v1 stays gone.
    provider_v2_->offer(interface_v2_.instance_);
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(10))));
    EXPECT_TRUE(client_v1_->availability_record_.equals({})) << client_v1_->availability_record_;
}

// Mirror of releasing_v1_drops_it_from_the_post_resume_find_but_not_v2 with the roles of v1 and v2
// swapped, to guard against a fix that happens to only work for whichever major sorts first.
TEST_F(test_multiversion_support_aggregate_consumption, releasing_v2_drops_it_from_the_post_resume_find_but_not_v1) {
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    auto in_gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_ep_, consumer_ecu_.router_name_}, in_gate->get_data_pipe()));

    start_apps();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v2_->release_service(interface_v2_.instance_);

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    // Quiesce provider-side offers before suspend/resume so the post-resume FindService
    // observation does not race against immediate incoming OfferService.
    provider_v1_->stop_offer(interface_v1_.instance_);
    provider_v2_->stop_offer(interface_v2_.instance_);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(10))));

    in_gate->block(true);

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);

    auto seen_majors = std::make_shared<attribute_recorder<vsomeip::major_version_t>>();
    gate->block_at(someip_gate::sd_predicate{[seen_majors](auto const& msg) {
        bool matched = false;
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_majors->record(e->get_major_version());
                matched = true;
            }
        }
        return matched;
    }});

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(10))));
    EXPECT_TRUE(seen_majors->wait_for_any(vsomeip::major_version_t{0x1}, std::chrono::milliseconds(0)))
            << "v1 is still requested -- it must be found again after resume";
    EXPECT_FALSE(seen_majors->wait_for_any(vsomeip::major_version_t{0x2}, std::chrono::milliseconds(0)))
            << "v2 was released -- it must not be re-requested after resume";
    gate->block(false);
    in_gate->get_data_pipe()->clear();
    in_gate->block(false);

    // Restore only the still-requested major: v1 should become available again, v2 stays gone.
    provider_v1_->offer(interface_v1_.instance_);
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                               common::scaled_timeout(std::chrono::seconds(10))));
    EXPECT_TRUE(client_v2_->availability_record_.equals({})) << client_v2_->availability_record_;
}

/// The router can differentiate properly between a local and a remote offering
TEST_F(test_multiversion_support_aggregate_consumption, local_consumer_of_v2_can_dispatch_request_to_v2) {
    std::string const local_v2_client_name = "local_v2_client";
    provider_v2_ecu_.add_app(local_v2_client_name);
    start_apps();
    auto* local_client = provider_v2_ecu_.apps_[local_v2_client_name];

    std::vector<unsigned char> const payload_v2{0x33, 0x44};
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);
    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    // now the service should be known to the router of ecu_provider_v2
    local_client->request_service(interface_v2_.instance_);
    ASSERT_TRUE(local_client->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    local_client->send_request(request_v2_);
    EXPECT_TRUE(local_client->wait_for_messages({payload_v2}));
}
/// The router can differentiate properly between a local and a remote offering
TEST_F(test_multiversion_support_aggregate_consumption, local_consumer_of_v1_can_dispatch_request_to_v1) {
    std::string const local_v1_client_name = "local_v1_client";
    provider_v1_ecu_.add_app(local_v1_client_name);
    start_apps();
    auto* local_client = provider_v1_ecu_.apps_[local_v1_client_name];

    std::vector<unsigned char> const payload_v1{0x33, 0x44};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);
    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    // now the service should be known to the router of ecu_provider_v2
    local_client->request_service(interface_v1_.instance_);
    ASSERT_TRUE(local_client->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    local_client->send_request(request_v1_);
    EXPECT_TRUE(local_client->wait_for_messages({payload_v1}));
}

/// The router can differentiate properly between a local and a remote offering
TEST_F(test_multiversion_support_aggregate_consumption, remote_consumer_of_v2_with_local_v1_offering_can_dispatch_request_to_v2) {
    static constexpr char const* remote_v2_client_name = "remote_v2_client";
    provider_v1_ecu_.config().apps_.push_back(application_config{
            .name_ = remote_v2_client_name, .id_ = provider_v1_ecu_.config().apps_.back().id_.value() + 1, .threads_ = std::nullopt});
    start_apps();
    auto* remote_client = provider_v1_ecu_.apps_[remote_v2_client_name];

    std::vector<unsigned char> const payload_v2{0x33, 0x44};
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);
    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    // now the service should be known to the router of ecu_provider_v2
    remote_client->request_service(interface_v2_.instance_);
    ASSERT_TRUE(remote_client->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    remote_client->send_request(request_v2_);
    EXPECT_TRUE(remote_client->wait_for_messages({payload_v2}));
}

/// The router can differentiate properly between a local and a remote offering
TEST_F(test_multiversion_support_aggregate_consumption, remote_consumer_of_v1_with_local_v2_offering_can_dispatch_request_to_v1) {
    static constexpr char const* remote_v1_client_name = "remote_v1_client";
    provider_v2_ecu_.config().apps_.push_back(application_config{
            .name_ = remote_v1_client_name, .id_ = provider_v2_ecu_.config().apps_.back().id_.value() + 1, .threads_ = std::nullopt});
    start_apps();
    auto* remote_client = provider_v2_ecu_.apps_[remote_v1_client_name];

    std::vector<unsigned char> const payload_v1{0x33, 0x44};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);
    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    // now the service should be known to the router of ecu_provider_v2
    remote_client->request_service(interface_v1_.instance_);
    ASSERT_TRUE(remote_client->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    remote_client->send_request(request_v1_);
    EXPECT_TRUE(remote_client->wait_for_messages({payload_v1}));
}

// Ensure DEFAULT_MAJOR != ANY_MAJOR
TEST_F(test_multiversion_support_aggregate_consumption, releasing_the_only_requester_of_the_default_major_still_tears_it_down) {
    start_apps();

    service_instance const major0_instance{.service_ = interface_v1_.instance_.service_,
                                           .instance_ = interface_v1_.instance_.instance_,
                                           .major_ = vsomeip::DEFAULT_MAJOR,
                                           .minor_ = vsomeip::DEFAULT_MINOR};

    provider_v1_->offer(major0_instance); // offers major 0x0, from provider_v1_ecu_
    provider_v2_->offer(interface_v2_.instance_); // offers major 0x2, from provider_v2_ecu_ (same service/instance)

    client_v1_->request_service(major0_instance); // the ONLY requester of major 0
    client_v2_->request_service(interface_v2_.instance_); // a requester of a DIFFERENT major (0x2)

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(major0_instance)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_));
    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_));

    auto drop_watch_major0 = watch_connection_drop(consumer_ecu_.router_name_, provider_v1_ecu_.router_name_);
    auto drop_watch_major2 = watch_connection_drop(consumer_ecu_.router_name_, provider_v2_ecu_.router_name_);

    client_v1_->release_service(major0_instance); // release_service() takes no major -- router must
                                                  // work out this client only ever held major 0

    // Correct behavior: major 0 has no requester left, so its client endpoint/connection to
    // provider_v1_ecu_ is torn down. Currently: has_requester_unlocked()'s query-side
    // `_major == DEFAULT_MAJOR` clause treats the release-time check for major 0 as if it meant
    // "any major", spuriously matches client_v2_'s still-live major-2 requester, and the major-0
    // teardown is skipped entirely.
    EXPECT_TRUE(drop_watch_major0.wait()) << "connection to the major-0 provider was not torn down after releasing its only "
                                             "requester, because an unrelated major's requester masked it -- see review finding 5";

    // Control: major 2's own, unrelated, still-live requester must NOT be disturbed by this release.
    EXPECT_FALSE(drop_watch_major2.wait(std::chrono::milliseconds(500)))
            << "releasing the major-0 requester incorrectly also affected the major-2 connection";
}

TEST_F(test_multiversion_support_aggregate_consumption, releasing_an_undiscovered_any_major_request_stops_find_service_after_resume) {
    GTEST_SKIP() << "Requires another sensor mechanism, as it is not guaranteed that the release is processed by the router, before the "
                    "suspend,resume cycle finishes";
    // With repetitions_max_ == 0, request_service() sends exactly ONE FindService per SD start
    // cycle -- this isolates the resume-triggered (second) burst from any natural repetition-phase
    // traffic belonging to the original, immediately-released request.
    consumer_ecu_.config().service_discovery_.repetitions_max_ = 0;

    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    start_apps(); // nobody offers anything for this (service, instance) in this test

    service_instance const any_major{
            .service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_, .major_ = ANY_MAJOR};

    // Wait for the router to have actually received (and, on the same read, processed) both the
    // request and the release before touching routing state: both are asynchronous IPC commands
    // from client_any_'s own thread, while set_routing_state() below runs synchronously on THIS
    // thread, independent of the router's IPC processing -- without this, request/release could
    // still be in flight when suspend/resume runs, making the scenario non-deterministic.
    client_any_->request_service(any_major);
    ASSERT_TRUE(wait_for_command("client_any", consumer_ecu_.router_name_, protocol::id_e::REQUEST_SERVICE_ID, socket_role::server));

    client_any_->release_service(any_major);
    ASSERT_TRUE(wait_for_command("client_any", consumer_ecu_.router_name_, protocol::id_e::RELEASE_SERVICE_ID, socket_role::server));

    // Only now arm the gate for the resume-triggered FindService -- the original request's own,
    // legitimate initial FindService (sent while processing the REQUEST_SERVICE command above) is
    // irrelevant and must not be mistaken for the bug under test.
    gate->block_at(someip_gate::sd_predicate{[service = any_major.service_, instance = any_major.instance_](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == service && e->get_instance() == instance) {
                return true;
            }
        }
        return false;
    }});

    // Trigger the suspend -> resume cycle that resets every SD requested_ entry's sent_counter to
    // 0. routing_manager_impl::set_routing_state() runs each transition synchronously (including
    // flushing SD sends on suspend) before returning, so back-to-back calls are safe here.
    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);
    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    // Correct behavior: release_service() erased the ANY_MAJOR-keyed SD requested_ entry, so
    // resume's sent_counter reset loop has nothing to find for this (service, instance), and no
    // FindService is ever emitted. Currently: find_services() returned empty (nothing was ever
    // offered), the per-major teardown loop never ran, discovery_->release_service() was never
    // called, and the orphaned ANY_MAJOR entry gets its counter reset and re-fires on resume.
    EXPECT_FALSE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(3))))
            << "a FindService was observed after resume for a service that was requested then "
               "released before ever being offered -- its SD requested_ entry was never erased -- "
               "see review finding 6";
}
// Ensure that a FIND_OFFER is only dispatched for explicit requests, i.e. if a client requests
// v2 and another ANY_MAJOR both requests should be dispatched, but if v2 is dropped only the
// FIND_OFFER for ANY_MAJOR should be dispatched later.
TEST_F(test_multiversion_support_aggregate_consumption, any_major_request_does_not_keep_explicit_major_request) {

    consumer_ecu_.config().service_discovery_.repetitions_max_ =
            0; // this guarantess that FIND_OFFERS are not leaking across suspend/resume
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    start_apps();

    struct seen_state {
        std::atomic<bool> has_v1_{false};
        std::atomic<bool> has_any_{false};
    };

    auto seen_before_suspend = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_before_suspend](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_before_suspend->has_v1_ = e->get_major_version() == 0x1 || seen_before_suspend->has_v1_.load();
                seen_before_suspend->has_any_ = e->get_major_version() == ANY_MAJOR || seen_before_suspend->has_any_.load();
            }
        }
        return seen_before_suspend->has_v1_ && seen_before_suspend->has_any_;
    }});

    service_instance any_major{
            .service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_, .major_ = ANY_MAJOR};

    client_v1_->request_service(interface_v1_.instance_);
    client_any_->request_service(any_major);

    ASSERT_TRUE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(30))));
    gate->block(false);
    client_v1_->release_service(interface_v1_.instance_);
    ASSERT_TRUE(wait_for_command(client_v1_name_, consumer_ecu_.router_name_, protocol::id_e::RELEASE_SERVICE_ID, socket_role::server));

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);

    auto seen_after_resume = std::make_shared<seen_state>();
    gate->block_at(someip_gate::sd_predicate{[seen_after_resume](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::FIND_SERVICE && e->get_service() == 0x1000 && e->get_instance() == 0x1) {
                seen_after_resume->has_v1_ = e->get_major_version() == 0x1 || seen_after_resume->has_v1_.load();
                seen_after_resume->has_any_ = e->get_major_version() == ANY_MAJOR || seen_after_resume->has_any_.load();
            }
        }
        return seen_after_resume->has_v1_ && seen_after_resume->has_any_;
    }});

    consumer_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    EXPECT_FALSE(gate->wait_for_blocked(common::scaled_timeout(std::chrono::milliseconds(100))));
    EXPECT_TRUE(seen_after_resume->has_any_.load());
    EXPECT_FALSE(seen_after_resume->has_v1_.load());
}

// Selective eventgroups carry real client ids in the SD subscription; a non-selective one holds only
// VSOMEIP_ROUTING_CLIENT, so the per-client StopSubscribe on release is observable only here.
struct test_multiversion_release_selective : public base_fake_socket_fixture {
    void start_apps() {
        consumer_ecu_.add_app(client_a_name_);
        consumer_ecu_.add_app(client_b_name_);
        provider_ecu_.add_app(server_name_);

        consumer_ecu_.prepare();
        provider_ecu_.prepare();

        consumer_ecu_.start_apps();
        provider_ecu_.start_apps();

        client_a_ = consumer_ecu_.apps_[client_a_name_];
        client_b_ = consumer_ecu_.apps_[client_b_name_];
        server_ = provider_ecu_.apps_[server_name_];

        ASSERT_TRUE(client_a_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_b_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(server_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    std::string const client_a_name_{"client_a"};
    std::string const client_b_name_{"client_b"};
    std::string const server_name_{"server"};

    // One event in its own eventgroup, as selectivity requires.
    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                            {}};
    event_ids event_{interface_v1_.instance_, interface_v1_.events_[0]};

    std::shared_ptr<attribute_recorder<vsomeip::client_t>> subscribed_ = std::make_shared<attribute_recorder<vsomeip::client_t>>();
    std::shared_ptr<attribute_recorder<vsomeip::client_t>> unsubscribed_ = std::make_shared<attribute_recorder<vsomeip::client_t>>();

    ecu_setup provider_ecu_{"provider", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* client_a_;
    app* client_b_;
    app* server_;
};

// client_b_ still requests the service, so release_service() takes the has-requester branch, whose
// only provider-visible effect is the per-client StopSubscribe for the released major.
TEST_F(test_multiversion_release_selective, releasing_one_selective_subscriber_stops_only_that_client) {
    start_apps();

    server_->register_group_subscription_handler(
            event_,
            [its_subscribed = subscribed_, its_unsubscribed = unsubscribed_](vsomeip::client_t _client, vsomeip::uid_t, vsomeip::gid_t,
                                                                             std::string const&, bool _is_subscribed) {
                (_is_subscribed ? its_subscribed : its_unsubscribed)->record(_client);
                return true;
            });

    server_->offer(interface_v1_);

    client_a_->request_service(interface_v1_.instance_);
    client_b_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_a_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_b_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    client_a_->subscribe_selective(event_);
    client_b_->subscribe_selective(event_);
    ASSERT_TRUE(subscribed_->wait_for_any(client_a_->get_client_id())) << subscribed_->to_string();
    ASSERT_TRUE(subscribed_->wait_for_any(client_b_->get_client_id())) << subscribed_->to_string();

    client_a_->release_service(interface_v1_.instance_);

    EXPECT_TRUE(unsubscribed_->wait_for_any(client_a_->get_client_id()))
            << "provider never saw the released client's StopSubscribe" << unsubscribed_->to_string();
    EXPECT_FALSE(unsubscribed_->wait_for_any(client_b_->get_client_id(), common::scaled_timeout(std::chrono::milliseconds(250))))
            << "the remaining subscriber was unsubscribed" << unsubscribed_->to_string();
}

// notify_one() to a selective remote subscriber reaches rmi::notify_one on the provider's router, which
// resolves the event by major. Major 2 is used so that any other literal is a wrong major.
TEST_F(test_multiversion_release_selective, notify_one_reaches_the_selective_subscriber_of_that_major) {
    start_apps();

    interface const its_v2{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                           {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                           {}};
    event_ids const its_event{its_v2.instance_, its_v2.events_[0]};

    server_->register_group_subscription_handler(its_event,
                                                 [its_subscribed = subscribed_](vsomeip::client_t _client, vsomeip::uid_t, vsomeip::gid_t,
                                                                                std::string const&, bool _is_subscribed) {
                                                     if (_is_subscribed) {
                                                         its_subscribed->record(_client);
                                                     }
                                                     return true;
                                                 });

    server_->offer(its_v2);

    client_a_->request_service(its_v2.instance_);
    ASSERT_TRUE(client_a_->availability_record_.wait_for_last(service_availability::available(its_v2.instance_)));
    client_a_->subscribe_selective(its_event);
    ASSERT_TRUE(subscribed_->wait_for_any(client_a_->get_client_id())) << subscribed_->to_string();

    std::vector<unsigned char> const its_payload{0x88, 0x88};
    server_->send_event_one(its_event, its_payload, client_a_->get_client_id());

    EXPECT_TRUE(client_a_->message_record_.wait_for_any(
            message_checker{std::nullopt, its_v2.instance_, its_event.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, its_payload}))
            << client_a_->message_record_;
}

// Unreliable eventgroups have no connection teardown; the SD StopSubscribe is the provider's only signal.
struct test_multiversion_release_unreliable : public base_fake_socket_fixture {
    void start_apps() {
        consumer_ecu_.add_app(client_name_);
        provider_ecu_.add_app(server_name_);

        consumer_ecu_.prepare();
        provider_ecu_.prepare();

        consumer_ecu_.start_apps();
        provider_ecu_.start_apps();

        client_ = consumer_ecu_.apps_[client_name_];
        server_ = provider_ecu_.apps_[server_name_];

        ASSERT_TRUE(client_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(server_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    std::string const client_name_{"client"};
    std::string const server_name_{"server"};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {},
                            {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_UNRELIABLE}}};
    event_ids field_{interface_v1_.instance_, interface_v1_.fields_[0]};

    // Declared before the ecu_setups: the provider's subscription handler holds a reference.
    attribute_recorder<bool> subscribed_;

    ecu_setup provider_ecu_{"provider", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* client_;
    app* server_;
};

TEST_F(test_multiversion_release_unreliable, releasing_sends_the_stop_subscribe_for_the_released_major) {
    start_apps();

    server_->register_group_subscription_handler(
            field_, [this](vsomeip::client_t, vsomeip::uid_t, vsomeip::gid_t, std::string const&, bool _is_subscribed) {
                subscribed_.record(_is_subscribed);
                return true;
            });

    server_->offer(interface_v1_);

    client_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    client_->subscribe_field(field_);
    ASSERT_TRUE(subscribed_.wait_for_any(true)) << "provider never saw the subscription";

    client_->release_service(interface_v1_.instance_);

    EXPECT_TRUE(subscribed_.wait_for_last(false)) << "provider never saw the StopSubscribe for the released major";
}

// release_service() takes no major, so the router recovers ANY_MAJOR from requested_services_, while
// remote_services_ is keyed by the offered major: the exact lookup in clear_client_endpoints() misses
// and the client endpoint is never stopped. Observable as the boardnet TCP connection staying up.
struct test_multiversion_release_any_major_reliable : public base_fake_socket_fixture {
    void start_apps() {
        consumer_ecu_.add_app(client_name_);
        consumer_ecu_.add_app(other_client_name_);
        provider_ecu_.add_app(server_name_);

        consumer_ecu_.prepare();
        provider_ecu_.prepare();

        consumer_ecu_.start_apps();
        provider_ecu_.start_apps();

        client_ = consumer_ecu_.apps_[client_name_];
        other_client_ = consumer_ecu_.apps_[other_client_name_];
        server_ = provider_ecu_.apps_[server_name_];
    }

    std::string const client_name_{"client"};
    std::string const other_client_name_{"other_client"};
    std::string const server_name_{"server"};

    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_RELIABLE}},
                            {}};
    event_ids event_{interface_v2_.instance_, interface_v2_.events_[0]};

    ecu_setup provider_ecu_{"provider", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v2_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* client_;
    app* other_client_;
    app* server_;
};

TEST_F(test_multiversion_release_any_major_reliable, releasing_an_any_major_request_drops_the_reliable_boardnet_connection) {
    start_apps();

    server_->offer(interface_v2_);

    service_instance const any_major{.service_ = interface_v2_.instance_.service_, .instance_ = interface_v2_.instance_.instance_};
    client_->request_service(any_major);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    client_->subscribe_event(event_);
    ASSERT_TRUE(client_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(event_)));
    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_ecu_.router_name_));

    auto drop_watch = watch_connection_drop(consumer_ecu_.router_name_, provider_ecu_.router_name_);
    client_->release_service(any_major);

    EXPECT_TRUE(drop_watch.wait()) << "reliable boardnet connection was never dropped after releasing an ANY_MAJOR "
                                      "request -- clear_client_endpoints() exact-misses the ANY_MAJOR-keyed request "
                                      "against the concrete-major remote_services_ entry";
}
TEST_F(test_multiversion_release_any_major_reliable,
       releasing_an_any_major_request_does_not_drop_the_connection_while_another_client_still_holds_the_concrete_major) {
    start_apps();

    server_->offer(interface_v2_);

    service_instance const any_major{.service_ = interface_v2_.instance_.service_, .instance_ = interface_v2_.instance_.instance_};
    client_->request_service(any_major);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    client_->subscribe_event(event_);
    ASSERT_TRUE(client_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(event_)));
    ASSERT_TRUE(await_connection(consumer_ecu_.router_name_, provider_ecu_.router_name_));

    other_client_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(other_client_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    auto drop_watch = watch_connection_drop(consumer_ecu_.router_name_, provider_ecu_.router_name_);
    client_->release_service(any_major);

    EXPECT_FALSE(drop_watch.wait(std::chrono::milliseconds(250)))
            << "reliable boardnet connection was dropped even though other_client_ still concretely "
               "requests major 0x2 -- releasing one client's ANY_MAJOR request must not disturb another "
               "client's still-active request for the concrete major";

    // The still-requesting client must be able to keep using the service.
    ASSERT_TRUE(other_client_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}

/**
 * This fixture provides two different ecu topologies:
 * Three-ECU topology isolating the *consuming* ECU's ability to independently track two
 * remote offers of the same (service, instance) discovered from two different hosts:
 *
 *   - provider_ecu_ offers major v1 and v2.
 *   - consumer_v1_ecu_ only ever requests/consumes, never offers v1.
 *   - consumer_v2_ecu_ only ever requests/consumes, never offers v2.
 *
 * Two-ECU topology one ECU is the provider the other one the consumer
 * remote offers of the same (service, instance) discovered from two different hosts:
 *
 *   - provider_ecu_ offers major v1 and v2.
 *   - consumer_v1_ecu_ only ever requests/consumes, never offers v1 and v2.
 *
 */
struct test_multiversion_support_aggregate_offering : public base_fake_socket_fixture {
    // does not start the _any_ client
    void start_three_ecu_setup() {
        provider_ecu_.add_app(provider_v1_name_);
        provider_ecu_.add_app(provider_v2_name_);
        consumer_v1_ecu_.add_app(client_v1_name_);
        consumer_v2_ecu_.add_app(client_v2_name_);

        provider_ecu_.prepare();
        consumer_v1_ecu_.prepare();
        consumer_v2_ecu_.prepare();

        provider_ecu_.start_apps();
        consumer_v1_ecu_.start_apps();
        consumer_v2_ecu_.start_apps();

        provider_v1_ = provider_ecu_.apps_[provider_v1_name_];
        provider_v2_ = provider_ecu_.apps_[provider_v2_name_];
        client_v1_ = consumer_v1_ecu_.apps_[client_v1_name_];
        client_v2_ = consumer_v2_ecu_.apps_[client_v2_name_];

        ASSERT_TRUE(provider_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(provider_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

        ASSERT_TRUE(set_udp_data_pipe({std::nullopt, provider_ecu_.router_name_}, udp_party::any(), provider_send_gate_->get_data_pipe()));
        ASSERT_TRUE(set_udp_data_pipe({std::nullopt, consumer_v1_ecu_.router_name_}, udp_party::any(),
                                      consumer_v1_send_gate_->get_data_pipe()));
        ASSERT_TRUE(set_udp_data_pipe({std::nullopt, consumer_v2_ecu_.router_name_}, udp_party::any(),
                                      consumer_v2_send_gate_->get_data_pipe()));
    }
    void start_two_ecu_setup() {
        provider_ecu_.add_app(provider_v1_name_);
        provider_ecu_.add_app(provider_v2_name_);
        consumer_v1_ecu_.add_app(client_v1_name_);
        consumer_v1_ecu_.add_app(client_v2_name_);
        consumer_v1_ecu_.add_app(client_any_name_);

        provider_ecu_.prepare();
        consumer_v1_ecu_.prepare();

        provider_ecu_.start_apps();
        consumer_v1_ecu_.start_apps();

        provider_v1_ = provider_ecu_.apps_[provider_v1_name_];
        provider_v2_ = provider_ecu_.apps_[provider_v2_name_];
        client_v1_ = consumer_v1_ecu_.apps_[client_v1_name_];
        client_v2_ = consumer_v1_ecu_.apps_[client_v2_name_];
        client_any_ = consumer_v1_ecu_.apps_[client_any_name_];

        ASSERT_TRUE(provider_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(provider_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_any_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

        ASSERT_TRUE(set_udp_data_pipe({std::nullopt, provider_ecu_.router_name_}, udp_party::any(), provider_send_gate_->get_data_pipe()));
        ASSERT_TRUE(
                set_udp_data_pipe({std::nullopt, consumer_v1_ecu_.router_name_}, udp_party::any(), consumer_send_gate_->get_data_pipe()));
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};
    std::string const client_any_name_{"client_any"};
    std::string const local_client_v1_name_{"local_client_v1"};
    std::string const local_client_v2_name_{"local_client_v2"};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {},
                            {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_RELIABLE}}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {},
                            {event_spec{0x8002, {0x8002}, vsomeip::reliability_type_e::RT_RELIABLE}}};

    vsomeip::method_t const method_{0x1111};
    request request_v1_{interface_v1_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {}};
    request request_v2_{interface_v2_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {}};

    // The SD multicast group address (VSOMEIP_SD_DEFAULT_MULTICAST); none of the ecu_configs used
    // here override it. FindService/OfferService sent to this address still go out as an ordinary
    // sendto() on the sender's own unicast SD socket -- joining the group is only needed to
    // *receive* multicast traffic, so gating on this as the receiver party does not depend on
    // whether the multicast join has completed.
    boost::asio::ip::udp::endpoint const sd_multicast_ep_{boost::asio::ip::make_address("224.224.224.0"), 30490};

    // provider_v1_ecu_/provider_v2_ecu_ each offer only their own major -- reusing the
    // client-only boardnet::ecu_one_config/ecu_two_config bases (stripped of the unrelated
    // service_3344 baked into ecu_two_config) purely for their distinct unicast IP/router
    // name/network; consumer_ecu_ never offers anything.
    //
    // Each ECU gets its own diagnosis address so their auto-assigned client ids don't collide --
    // utility::request_client_id() reads diagnosis/mask fresh from _config per call (no longer
    // function-local-static-cached), so this now gives each ECU its own non-overlapping range.
    ecu_setup provider_ecu_{
            "provider", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}),
            *socket_manager_}; // configuration is version independent. Adding one interface enables offering for both versions.
    ecu_setup consumer_v1_ecu_{"consumer_v1", boardnet::ecu_two_config, *socket_manager_};
    // consumer_ecu_ never offers anything.
    ecu_setup consumer_v2_ecu_{"consumer_v2", boardnet::ecu_three_config, *socket_manager_};

    app* provider_v1_;
    app* provider_v2_;
    app* client_v1_;
    app* client_v2_;
    app* client_any_;
    std::shared_ptr<someip_gate> provider_send_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> consumer_send_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> consumer_v1_send_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> consumer_v2_send_gate_ = someip_gate::create();
};

TEST_F(test_multiversion_support_aggregate_offering, both_majors_offered_from_a_single_ecu_become_independently_available) {
    start_three_ecu_setup();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));

    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}
// Both majors are offered from one ECU; a remote subscription to v2 must reach v2's provider, not
// whichever local offerer find_local_client() returns first.
TEST_F(test_multiversion_support_aggregate_offering, a_remote_subscription_reaches_the_provider_of_that_major) {
    start_two_ecu_setup();

    event_ids const field_v2{interface_v2_.instance_, interface_v2_.fields_[0]};
    auto const its_subscribed = std::make_shared<attribute_recorder<bool>>();
    provider_v2_->register_group_subscription_handler(
            field_v2, [its_subscribed](vsomeip::client_t, vsomeip::uid_t, vsomeip::gid_t, std::string const&, bool _is_subscribed) {
                its_subscribed->record(_is_subscribed);
                return true;
            });

    auto const v2_available = std::make_shared<attribute_recorder<bool>>();
    client_v2_->get_application()->register_availability_handler(
            interface_v2_.instance_.service_, interface_v2_.instance_.instance_,
            [v2_available](vsomeip::service_t, vsomeip::instance_t, vsomeip::availability_state_e _state) {
                v2_available->record(_state == vsomeip::availability_state_e::AS_AVAILABLE);
            },
            interface_v2_.instance_.major_, vsomeip::ANY_MINOR);

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(v2_available->wait_for_last(true));

    client_v2_->subscribe_field(field_v2);

    EXPECT_TRUE(its_subscribed->wait_for_any(true)) << "provider_v2 never saw the remote subscription";
}

TEST_F(test_multiversion_support_aggregate_offering, releasing_v2_then_any_major_eventually_drops_v2s_connection_but_never_v1s) {
    consumer_v2_ecu_.add_app(client_any_name_);
    start_three_ecu_setup();
    client_any_ = consumer_v2_ecu_.apps_[client_any_name_];

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    service_instance const any_major{.service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_};
    client_any_->request_service(any_major);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    ASSERT_TRUE(client_any_->availability_record_.wait_for_last(service_availability::available(any_major)));

    ASSERT_TRUE(await_connection(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_));
    ASSERT_TRUE(await_connection(consumer_v2_ecu_.router_name_, provider_ecu_.router_name_));

    auto drop_watch_v1 = watch_connection_drop(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_);
    auto drop_watch_v2 = watch_connection_drop(consumer_v2_ecu_.router_name_, provider_ecu_.router_name_);

    client_v2_->release_service(interface_v2_.instance_);
    EXPECT_FALSE(drop_watch_v2.wait(std::chrono::milliseconds(500)));
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(0)));

    client_any_->release_service(any_major);
    EXPECT_TRUE(drop_watch_v2.wait(std::chrono::milliseconds(500)));
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(0)));
}
TEST_F(test_multiversion_support_aggregate_offering, releasing_v1_then_any_major_eventually_drops_v1s_connection_but_never_v2s) {
    consumer_v1_ecu_.add_app(client_any_name_);
    start_three_ecu_setup();
    client_any_ = consumer_v1_ecu_.apps_[client_any_name_];

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    service_instance const any_major{.service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_};
    client_any_->request_service(any_major);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    ASSERT_TRUE(client_any_->availability_record_.wait_for_last(service_availability::available(any_major)));

    ASSERT_TRUE(await_connection(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_));
    ASSERT_TRUE(await_connection(consumer_v2_ecu_.router_name_, provider_ecu_.router_name_));

    auto drop_watch_v1 = watch_connection_drop(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_);
    auto drop_watch_v2 = watch_connection_drop(consumer_v2_ecu_.router_name_, provider_ecu_.router_name_);

    client_v1_->release_service(interface_v1_.instance_);
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(500)));
    EXPECT_FALSE(drop_watch_v2.wait(std::chrono::milliseconds(0)));

    client_any_->release_service(any_major);
    EXPECT_TRUE(drop_watch_v1.wait(std::chrono::milliseconds(500)));
    EXPECT_FALSE(drop_watch_v2.wait(std::chrono::milliseconds(0)));
}

TEST_F(test_multiversion_support_aggregate_offering, drop_connection_on_last_release) {
    start_two_ecu_setup();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    service_instance const any_major{.service_ = interface_v1_.instance_.service_, .instance_ = interface_v1_.instance_.instance_};
    client_any_->request_service(any_major);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    ASSERT_TRUE(client_any_->availability_record_.wait_for_last(service_availability::available(any_major)));

    ASSERT_TRUE(await_connection(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_));

    auto drop_watch_v1 = watch_connection_drop(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_);

    client_v1_->release_service(interface_v1_.instance_);
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(500)));

    client_any_->release_service(any_major);
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(500)));
    client_v2_->release_service(interface_v2_.instance_);
    EXPECT_TRUE(drop_watch_v1.wait(std::chrono::milliseconds(500)));
}
TEST_F(test_multiversion_support_aggregate_offering, drop_connection_on_last_stop_offer) {
    // ensure the consumer doesn't tear down the connection because of missing sends
    provider_ecu_.config().service_discovery_.ttl_ = 60;
    start_two_ecu_setup();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    ASSERT_TRUE(await_connection(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_));
    // ensure that stop messages don't reach the consumer - ensuring that the provider needs to tear down the connection
    provider_send_gate_->block(true);

    auto drop_watch_v1 = watch_connection_drop(consumer_v1_ecu_.router_name_, provider_ecu_.router_name_);

    provider_v1_->stop_offer(interface_v1_.instance_);
    EXPECT_FALSE(drop_watch_v1.wait(std::chrono::milliseconds(500)));

    provider_v2_->stop_offer(interface_v2_.instance_);
    EXPECT_TRUE(drop_watch_v1.wait(std::chrono::milliseconds(500)));
}

TEST_F(test_multiversion_support_aggregate_offering, stopping_v1_offer_does_not_disturb_v2_in_aggregate_setup) {
    start_three_ecu_setup();

    std::vector<unsigned char> const payload_v2{0xA1, 0xB2};
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    provider_v1_->stop_offer(interface_v1_.instance_);

    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                                std::chrono::milliseconds(250)));

    client_v2_->send_request(request_v2_);
    EXPECT_TRUE(client_v2_->wait_for_messages({payload_v2}));
}

TEST_F(test_multiversion_support_aggregate_offering, stopping_v2_offer_does_not_disturb_v1_in_aggregate_setup) {
    start_three_ecu_setup();

    std::vector<unsigned char> const payload_v1{0xC3, 0xD4};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->availability_record_.clear();
    client_v2_->availability_record_.clear();

    provider_v2_->stop_offer(interface_v2_.instance_);

    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_),
                                                                std::chrono::milliseconds(250)));

    client_v1_->send_request(request_v1_);
    EXPECT_TRUE(client_v1_->wait_for_messages({payload_v1}));
}
TEST_F(test_multiversion_support_aggregate_offering, request_to_each_major_is_routed_to_the_matching_remote_provider_three_ecus) {
    start_three_ecu_setup();

    std::vector<unsigned char> const payload_v1{0x01, 0x02};
    std::vector<unsigned char> const payload_v2{0x03, 0x04};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->send_request(request_v1_);
    client_v2_->send_request(request_v2_);

    EXPECT_TRUE(client_v1_->wait_for_messages({payload_v1}));
    EXPECT_TRUE(client_v2_->wait_for_messages({payload_v2}));
}
TEST_F(test_multiversion_support_aggregate_offering, request_to_each_major_is_routed_to_the_matching_remote_provider_two_ecus) {
    start_two_ecu_setup();

    std::vector<unsigned char> const payload_v1{0x01, 0x02};
    std::vector<unsigned char> const payload_v2{0x03, 0x04};
    provider_v1_->answer_request(request_v1_, [payload_v1] { return payload_v1; });
    provider_v2_->answer_request(request_v2_, [payload_v2] { return payload_v2; });

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->send_request(request_v1_);
    client_v2_->send_request(request_v2_);

    EXPECT_TRUE(client_v1_->wait_for_messages({payload_v1}));
    EXPECT_TRUE(client_v2_->wait_for_messages({payload_v2}));
}
TEST_F(test_multiversion_support_aggregate_offering, v1_is_reoffered_after_debounce) {
    std::shared_ptr<command_gate> client_gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(provider_v1_name_, provider_ecu_.router_name_, socket_role::server, client_gate->get_data_pipe()));

    start_two_ecu_setup();
    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    provider_send_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::OFFER_SERVICE, 3});
    client_gate->block_at(protocol::id_e::STOP_OFFER_SERVICE_ID);

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_gate->wait_for_blocked());
    provider_v1_->offer(interface_v1_.instance_);

    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());
    // re-arm to await the stop-offer processing (releases all the prior offerings)
    provider_send_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::OFFER_SERVICE, 0});
    client_gate->block(false);
    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());
    // any offer after the wait above is a debounced offer -> Now we can assert that the debouncing is preserving the proper major version
    provider_send_gate_->block_at(someip_gate::sd_predicate{[interface = interface_v1_.instance_](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::OFFER_SERVICE && e->get_ttl() == 3 && e->get_service() == interface.service_
                && e->get_instance() == interface.instance_ && e->get_major_version() == interface.major_) {
                return true;
            }
        }
        return false;
    }});

    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());
}
TEST_F(test_multiversion_support_aggregate_offering, v2_is_reoffered_after_debounce) {
    std::shared_ptr<command_gate> client_gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(provider_v2_name_, provider_ecu_.router_name_, socket_role::server, client_gate->get_data_pipe()));

    start_two_ecu_setup();
    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    provider_send_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::OFFER_SERVICE, 3});
    client_gate->block_at(protocol::id_e::STOP_OFFER_SERVICE_ID);

    provider_v2_->stop_offer(interface_v2_.instance_);
    ASSERT_TRUE(client_gate->wait_for_blocked());
    provider_v2_->offer(interface_v2_.instance_);

    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());
    // re-arm to await the stop-offer processing (releases all the prior offerings)
    provider_send_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::OFFER_SERVICE, 0});
    client_gate->block(false);
    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());
    // any offer after the wait above is a debounced offer -> Now we can assert that the debouncing is preserving the proper major version
    provider_send_gate_->block_at(someip_gate::sd_predicate{[interface = interface_v2_.instance_](auto const& msg) {
        for (auto const& e : msg->get_entries()) {
            if (e->get_type() == sd::entry_type_e::OFFER_SERVICE && e->get_ttl() == 3 && e->get_service() == interface.service_
                && e->get_instance() == interface.instance_ && e->get_major_version() == interface.major_) {
                return true;
            }
        }
        return false;
    }});

    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());
}

TEST_F(test_multiversion_support_aggregate_offering, v1_is_reoffered_after_resume_race) {
    std::shared_ptr<command_gate> client_gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(provider_v1_name_, provider_ecu_.router_name_, socket_role::server, client_gate->get_data_pipe()));

    start_two_ecu_setup();
    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_gate->block_at(protocol::id_e::STOP_OFFER_SERVICE_ID);

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_gate->wait_for_blocked());
    provider_v1_->offer(interface_v1_.instance_);
    provider_send_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::OFFER_SERVICE, 0});
    client_gate->block(false);
    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());

    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);
    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);
    provider_send_gate_->block(false);

    client_v1_->availability_record_.clear();
    client_v1_->request_service(interface_v2_.instance_);
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}
TEST_F(test_multiversion_support_aggregate_offering, v2_is_reoffered_after_resume_race) {
    std::shared_ptr<command_gate> client_gate = command_gate::create();
    ASSERT_TRUE(setup_data_pipe(provider_v2_name_, provider_ecu_.router_name_, socket_role::server, client_gate->get_data_pipe()));

    start_two_ecu_setup();
    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_gate->block_at(protocol::id_e::STOP_OFFER_SERVICE_ID);

    provider_v2_->stop_offer(interface_v2_.instance_);
    ASSERT_TRUE(client_gate->wait_for_blocked());
    provider_v2_->offer(interface_v2_.instance_);
    provider_send_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::OFFER_SERVICE, 0});
    client_gate->block(false);
    ASSERT_TRUE(provider_send_gate_->wait_for_blocked());

    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);
    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);
    provider_send_gate_->block(false);

    client_v2_->availability_record_.clear();
    client_v2_->request_service(interface_v2_.instance_);
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}

TEST_F(test_multiversion_support_aggregate_offering, v1_and_v2_are_offered_after_routing_is_ready) {
    provider_ecu_.add_app(local_client_v1_name_);
    provider_ecu_.add_app(local_client_v2_name_);

    start_two_ecu_setup();
    auto* lcv1 = provider_ecu_.apps_[local_client_v1_name_];
    auto* lcv2 = provider_ecu_.apps_[local_client_v2_name_];

    ASSERT_TRUE(provider_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));
    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    lcv1->request_service(interface_v1_.instance_);
    lcv2->request_service(interface_v2_.instance_);
    ASSERT_TRUE(lcv1->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(lcv2->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    // since the local clients are informed the remote offering was already stashed (whitebox knowledge of the offering order in
    // routing_manager_impl)

    ASSERT_TRUE(provider_ecu_.set_routing(fake_netlink_connector::state_e::UP));
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}
TEST_F(test_multiversion_support_aggregate_offering, v1_are_offered_after_routing_is_ready_with_v2_stop_offered) {
    provider_ecu_.add_app(local_client_v1_name_);
    provider_ecu_.add_app(local_client_v2_name_);

    start_two_ecu_setup();
    auto* lcv1 = provider_ecu_.apps_[local_client_v1_name_];
    auto* lcv2 = provider_ecu_.apps_[local_client_v2_name_];

    ASSERT_TRUE(provider_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));
    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    lcv1->request_service(interface_v1_.instance_);
    lcv2->request_service(interface_v2_.instance_);
    ASSERT_TRUE(lcv1->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(lcv2->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    provider_v2_->stop_offer(interface_v2_.instance_);
    ASSERT_TRUE(lcv2->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));
    // since the local clients are informed the remote offering was already stashed (whitebox knowledge of the offering order in
    // routing_manager_impl)

    ASSERT_TRUE(provider_ecu_.set_routing(fake_netlink_connector::state_e::UP));
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_),
                                                                std::chrono::milliseconds(100)));
}
TEST_F(test_multiversion_support_aggregate_offering, v2_are_offered_after_routing_is_ready_with_v2_stop_offered) {
    provider_ecu_.add_app(local_client_v1_name_);
    provider_ecu_.add_app(local_client_v2_name_);

    start_two_ecu_setup();
    auto* lcv1 = provider_ecu_.apps_[local_client_v1_name_];
    auto* lcv2 = provider_ecu_.apps_[local_client_v2_name_];

    ASSERT_TRUE(provider_ecu_.set_routing(fake_netlink_connector::state_e::DOWN));
    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    lcv1->request_service(interface_v1_.instance_);
    lcv2->request_service(interface_v2_.instance_);
    ASSERT_TRUE(lcv1->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(lcv2->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(lcv1->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    // since the local clients are informed the remote offering was already stashed (whitebox knowledge of the offering order in
    // routing_manager_impl)

    ASSERT_TRUE(provider_ecu_.set_routing(fake_netlink_connector::state_e::UP));
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    EXPECT_FALSE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_),
                                                                std::chrono::milliseconds(100)));
}

TEST_F(test_multiversion_support_aggregate_offering, v1_becomes_available_via_unicast_find_service_reply) {
    provider_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(60);
    // only blocking multicast
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({provider_ecu_.sd_endpoint(), provider_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    gate->block(true);

    start_two_ecu_setup();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    // due to the very big "main_phase_timer" it should be guaranteed that the find_service request is received before half the timer
    // elapsed
    client_v1_->request_service(interface_v1_.instance_);
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
}

TEST_F(test_multiversion_support_aggregate_offering, v2_becomes_available_via_unicast_find_service_reply) {
    provider_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(60);
    // only blocking multicast
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({provider_ecu_.sd_endpoint(), provider_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    gate->block(true);

    start_two_ecu_setup();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    // due to the very big "main_phase_timer" it should be guaranteed that the find_service request is received before half the timer
    // elapsed
    client_v2_->request_service(interface_v2_.instance_);
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}
TEST_F(test_multiversion_support_aggregate_offering, v1_with_any_instance_becomes_available_via_unicast_find_service_reply) {
    provider_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(60);
    // only blocking multicast
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({provider_ecu_.sd_endpoint(), provider_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    gate->block(true);

    start_two_ecu_setup();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    // due to the very big "main_phase_timer" it should be guaranteed that the find_service request is received before half the timer
    // elapsed
    auto v1 = interface_v1_.instance_;
    v1.instance_ = ANY_INSTANCE;
    client_v1_->request_service(v1);
    EXPECT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
}

TEST_F(test_multiversion_support_aggregate_offering, v2_with_any_instance_becomes_available_via_unicast_find_service_reply) {
    provider_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(60);
    // only blocking multicast
    auto gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({provider_ecu_.sd_endpoint(), provider_ecu_.router_name_}, {sd_multicast_ep_}, gate->get_data_pipe()));
    gate->block(true);

    start_two_ecu_setup();

    provider_v1_->offer(interface_v1_.instance_);
    provider_v2_->offer(interface_v2_.instance_);

    // due to the very big "main_phase_timer" it should be guaranteed that the find_service request is received before half the timer
    // elapsed
    auto v2 = interface_v2_.instance_;
    v2.instance_ = ANY_INSTANCE;
    client_v2_->request_service(v2);
    EXPECT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
}

// Three-ECU topology of test_multiversion_support_aggregate_consumption, with both majors reusing
// the same event id and the same eventgroup id.
struct test_multiversion_support_aggregate_events : public base_fake_socket_fixture {
    void start_apps() {
        provider_v1_ecu_.add_app(provider_v1_name_);
        provider_v2_ecu_.add_app(provider_v2_name_);
        consumer_ecu_.add_app(client_v1_name_);
        consumer_ecu_.add_app(client_v2_name_);

        provider_v1_ecu_.prepare();
        provider_v2_ecu_.prepare();
        consumer_ecu_.prepare();

        provider_v1_ecu_.start_apps();
        provider_v2_ecu_.start_apps();
        consumer_ecu_.start_apps();

        provider_v1_ = provider_v1_ecu_.apps_[provider_v1_name_];
        provider_v2_ = provider_v2_ecu_.apps_[provider_v2_name_];
        client_v1_ = consumer_ecu_.apps_[client_v1_name_];
        client_v2_ = consumer_ecu_.apps_[client_v2_name_];

        ASSERT_TRUE(provider_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(provider_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    void offer_and_subscribe_both() {
        record_remote_subscriptions(provider_v1_, field_v1_, subscribed_at_v1_);
        record_remote_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);

        provider_v1_->offer(interface_v1_);
        provider_v2_->offer(interface_v2_);

        client_v1_->request_service(interface_v1_.instance_);
        client_v2_->request_service(interface_v2_.instance_);
        ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
        ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

        client_v1_->subscribe_field(field_v1_);
        client_v2_->subscribe_field(field_v2_);
        ASSERT_TRUE(subscribed_at_v1_.wait_for_any(true)) << "provider_v1 never saw client_v1's subscription";
        ASSERT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw client_v2's subscription";
    }

    std::shared_ptr<someip_gate> sd_gate(udp_party _sender, udp_party _receiver) {
        auto its_gate = someip_gate::create();
        EXPECT_TRUE(set_udp_data_pipe(std::move(_sender), std::move(_receiver), its_gate->get_data_pipe()));
        return its_gate;
    }

    // A cached value stops the StopSubscribe/Subscribe each offer sends for a field without one.
    void publish_v1_field() {
        provider_v1_->send_event(field_v1_, payload_v1_);
        ASSERT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
                << client_v1_->message_record_;
    }

    void expect_v2_still_notified() {
        provider_v2_->send_event(field_v2_, payload_v2_);
        EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
                << client_v2_->message_record_;
    }

    someip_sd_record_message const stop_subscribe_{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0};
    someip_sd_record_message const nack_{sd::entry_type_e::SUBSCRIBE_EVENTGROUP_ACK, 0};

    // Each provider ECU hosts exactly one major, so a record here proves the subscription reached
    // that major's provider. The consumer-side callback cannot be used: the local SUBSCRIBE_ACK is
    // sent even when SD dropped the subscription.
    void record_remote_subscriptions(app* _provider, event_ids const& _ei, attribute_recorder<bool>& _record) {
        _provider->register_group_subscription_handler(_ei, [&_record](client_t, uid_t, gid_t, std::string const&, bool _is_subscribed) {
            _record.record(_is_subscribed);
            return true;
        });
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};

    static constexpr vsomeip::event_t shared_event_{0x8001};
    static constexpr vsomeip::eventgroup_t shared_eventgroup_{0x8001};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {},
                            {event_spec{shared_event_, {shared_eventgroup_}, vsomeip::reliability_type_e::RT_RELIABLE}}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {},
                            {event_spec{shared_event_, {shared_eventgroup_}, vsomeip::reliability_type_e::RT_RELIABLE}}};

    event_ids field_v1_{interface_v1_.instance_, interface_v1_.fields_[0]};
    event_ids field_v2_{interface_v2_.instance_, interface_v2_.fields_[0]};

    std::vector<unsigned char> const payload_v1_{0x11, 0x11};
    std::vector<unsigned char> const payload_v2_{0x22, 0x22};

    // The recorded major is the received message's SOME/IP interface version (@see app::on_message).
    message_checker notification(service_instance const& _si, std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, _si, shared_event_, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    // Declared before the ecu_setups: the provider's subscription handler holds a reference, so
    // these must outlive the apps.
    attribute_recorder<bool> subscribed_at_v1_;
    attribute_recorder<bool> subscribed_at_v2_;

    ecu_setup provider_v1_ecu_{"provider_v1", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup provider_v2_ecu_{"provider_v2", ecu_config{boardnet::ecu_two_config}.add_interface({interface_v2_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* provider_v1_;
    app* provider_v2_;
    app* client_v1_;
    app* client_v2_;
};

// Both majors use the same eventgroup id, so each subscription has to survive
// service_discovery_impl::subscribed_. offer_and_subscribe_both() asserts that each one reached its
// own provider.
TEST_F(test_multiversion_support_aggregate_events, both_majors_can_subscribe_to_the_same_eventgroup_id) {
    start_apps();
    offer_and_subscribe_both();
}

// A client that calls request_service() without a major registers its events under ANY_MAJOR,
// so the host really does hold an ANY_MAJOR bucket. A StopOffer arrives with the concrete major
// and must still clear that bucket's subscribers, otherwise the re-subscribe after the re-offer
// is swallowed as "already subscribed".
// PROBE: both majors are offered. A client that requested ANY_MAJOR registers its event before it
// subscribes, so the registration major is deduced from what is offered. It must end up on the major
// the client then subscribes with, not on whichever offer the lookup happened to visit first.
TEST_F(test_multiversion_support_aggregate_events, any_major_requester_registers_the_major_it_subscribes_with) {
    start_apps();

    record_remote_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    auto const v2_available = std::make_shared<attribute_recorder<bool>>();
    client_v2_->get_application()->register_availability_handler(
            interface_v2_.instance_.service_, interface_v2_.instance_.instance_,
            [v2_available](vsomeip::service_t, vsomeip::instance_t, vsomeip::availability_state_e _state) {
                v2_available->record(_state == vsomeip::availability_state_e::AS_AVAILABLE);
            },
            interface_v2_.instance_.major_, vsomeip::ANY_MINOR);

    client_v2_->request_service(service_instance{.service_ = interface_v2_.instance_.service_,
                                                 .instance_ = interface_v2_.instance_.instance_,
                                                 .major_ = vsomeip::ANY_MAJOR,
                                                 .minor_ = vsomeip::ANY_MINOR});
    // The availability callback carries no major, so only a major-scoped handler identifies v2.
    ASSERT_TRUE(v2_available->wait_for_last(true));

    client_v2_->subscribe_field(field_v2_);
    EXPECT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw the ANY_MAJOR requester's subscription";

    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

// A StopOffer must clear the subscribers of the re-offered major, or the re-subscribe is swallowed.
TEST_F(test_multiversion_support_aggregate_events, any_major_requester_resubscribes_after_a_stop_offer) {
    start_apps();

    provider_v1_->offer(interface_v1_);

    client_v1_->get_application()->request_service(0x1000, 0x1);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    client_v1_->subscribe_field(field_v1_);
    provider_v1_->send_event(field_v1_, payload_v1_);
    ASSERT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;

    client_v1_->availability_record_.clear();
    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    provider_v1_->offer(interface_v1_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    client_v1_->subscribe_field(field_v1_);
    client_v1_->message_record_.clear();
    provider_v1_->send_event(field_v1_, payload_v1_, true);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
}

// A released client left in the subscriber set keeps eg_has_subscribers true, so the last real
// unsubscribe no longer unsets the payload and the next subscriber gets the stale value replayed.
TEST_F(test_multiversion_support_aggregate_events, releasing_lets_the_last_unsubscribe_unset_the_payload) {
    start_apps();

    provider_v1_->offer(interface_v1_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    client_v1_->subscribe_field(field_v1_);
    client_v2_->subscribe_field(field_v1_);

    provider_v1_->send_event(field_v1_, payload_v1_);
    ASSERT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v2_->message_record_;

    client_v1_->release_service(interface_v1_.instance_);
    client_v2_->unsubscribe_from_group(field_v1_);

    std::vector<unsigned char> const its_later_payload{0x55, 0x55};
    provider_v1_->send_event(field_v1_, its_later_payload, true);

    client_v2_->message_record_.clear();
    client_v2_->subscribe_field(field_v1_);

    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v1_.instance_, its_later_payload)))
            << client_v2_->message_record_;
    EXPECT_FALSE(client_v2_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_),
                                                          common::scaled_timeout(std::chrono::milliseconds(250))))
            << "the stale payload was replayed" << client_v2_->message_record_;
}

// rmi::unsubscribe picks the eventgroup by major; v2 shares the eventgroup id.
TEST_F(test_multiversion_support_aggregate_events, unsubscribing_from_v1_leaves_v2_subscribed) {
    start_apps();
    offer_and_subscribe_both();

    subscribed_at_v1_.clear();
    client_v1_->unsubscribe_from_group(field_v1_);

    EXPECT_TRUE(subscribed_at_v1_.wait_for_last(false)) << "provider_v1 never saw the StopSubscribe";

    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << "unsubscribing from v1 disturbed v2" << client_v2_->message_record_;
}

// A second subscriber to an already acknowledged eventgroup is acked from remote_subscription_state_,
// which is keyed by major.
TEST_F(test_multiversion_support_aggregate_events, a_second_subscriber_to_an_acknowledged_eventgroup_is_acked) {
    start_apps();

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    client_v1_->subscribe_field(field_v1_);
    ASSERT_TRUE(client_v1_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v1_)))
            << client_v1_->subscription_record_;

    client_v2_->subscribe_field(field_v1_);

    EXPECT_TRUE(client_v2_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v1_)))
            << client_v2_->subscription_record_;
}

// on_endpoint_connected() for v1's endpoint must not resend v2's subscription; v2 is offered once, as each offer resubscribes.
TEST_F(test_multiversion_support_aggregate_events, connecting_one_majors_endpoint_does_not_resubscribe_the_other_major) {
    provider_v2_ecu_.config().service_discovery_.repetitions_max_ = 0;
    provider_v2_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(30);
    provider_v2_ecu_.config().service_discovery_.ttl_ = 60;
    start_apps();
    boost::asio::ip::udp::endpoint const sd_multicast{boost::asio::ip::make_address("224.224.224.0"), 30490};
    auto const v2_offers = someip_gate::create("v2_offers");
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast, consumer_ecu_.router_name_}, v2_offers->get_data_pipe()));
    auto const to_v2 = someip_gate::create("to_v2");
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {provider_v2_ecu_.sd_endpoint()},
                                  to_v2->get_data_pipe()));
    record_remote_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);
    ASSERT_TRUE(v2_offers->sd_record_.wait_for_any({sd::entry_type_e::OFFER_SERVICE, 60}));

    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    client_v2_->subscribe_field(field_v2_);
    ASSERT_TRUE(subscribed_at_v2_.wait_for_any(true));
    to_v2->sd_record_.clear();

    client_v1_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    EXPECT_FALSE(to_v2->sd_record_.wait_for_any({sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}, std::chrono::milliseconds(100)))
            << to_v2->sd_record_.to_string();
}

// A failed connect to v1 must keep v2's acked state; single v2 offer and cached field avoid a resubscribe restoring it.
TEST_F(test_multiversion_support_aggregate_events, a_failed_connect_to_one_major_keeps_the_other_majors_subscription_acked) {
    provider_v2_ecu_.config().service_discovery_.repetitions_max_ = 0;
    provider_v2_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(30);
    provider_v2_ecu_.config().service_discovery_.ttl_ = 60;
    std::string const second_v2_name{"second_client_v2"};
    consumer_ecu_.add_app(second_v2_name);
    start_apps();
    auto* second_v2 = consumer_ecu_.apps_[second_v2_name];
    ASSERT_TRUE(second_v2->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    boost::asio::ip::udp::endpoint const sd_multicast{boost::asio::ip::make_address("224.224.224.0"), 30490};
    auto const v2_offers = someip_gate::create("v2_offers");
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast, consumer_ecu_.router_name_}, v2_offers->get_data_pipe()));

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);
    ASSERT_TRUE(v2_offers->sd_record_.wait_for_any({sd::entry_type_e::OFFER_SERVICE, 60}));

    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    client_v2_->subscribe_field(field_v2_);
    ASSERT_TRUE(client_v2_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v2_)));
    provider_v2_->send_event(field_v2_, payload_v2_);
    ASSERT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)));

    report_on_connect(provider_v1_ecu_.router_name_ + "_auxiliary_context_", {boost::asio::error::connection_refused});
    client_v1_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    second_v2->request_service(interface_v2_.instance_);
    ASSERT_TRUE(second_v2->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    second_v2->subscribe_field(field_v2_);

    EXPECT_TRUE(second_v2->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v2_)))
            << second_v2->subscription_record_;
}

// A NACK from v1's provider restarts v1's connection only, which resubscribes v1.
TEST_F(test_multiversion_support_aggregate_events, a_nack_of_v1_resubscribes_only_v1) {
    // Offered once, so only the NACK path resubscribes.
    provider_v1_ecu_.config().service_discovery_.repetitions_max_ = 0;
    provider_v1_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(30);
    provider_v1_ecu_.config().service_discovery_.ttl_ = 60;
    start_apps();
    auto const v1_answers = sd_gate({provider_v1_ecu_.sd_endpoint(), provider_v1_ecu_.router_name_}, {consumer_ecu_.sd_endpoint()});
    provider_v1_->register_group_subscription_handler(field_v1_, [this](client_t, uid_t, gid_t, std::string const&, bool _is_subscribed) {
        subscribed_at_v1_.record(_is_subscribed);
        return false;
    });
    record_remote_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);
    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    client_v1_->subscribe_field(field_v1_);
    client_v2_->subscribe_field(field_v2_);

    EXPECT_TRUE(v1_answers->sd_record_.wait_for_any(nack_)) << v1_answers->sd_record_.to_string();
    EXPECT_TRUE(subscribed_at_v1_.wait_for_count(true, 2)) << "v1 was not resubscribed after its NACK";
    EXPECT_TRUE(client_v2_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v2_)));
    expect_v2_still_notified();
}

// Losing v1's TCP connection stop-subscribes v1 only.
TEST_F(test_multiversion_support_aggregate_events, losing_v1s_connection_stop_subscribes_only_v1) {
    start_apps();
    offer_and_subscribe_both();
    publish_v1_field();
    auto const to_v1 = sd_gate({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {provider_v1_ecu_.sd_endpoint()});

    ASSERT_TRUE(disconnect(consumer_ecu_.router_name_, boost::asio::error::connection_reset, provider_v1_ecu_.router_name_,
                           boost::asio::error::connection_reset));

    EXPECT_TRUE(to_v1->sd_record_.wait_for_any(stop_subscribe_)) << to_v1->sd_record_.to_string();
    expect_v2_still_notified();
}

// The v1 offer's TTL expiry stop-subscribes v1 only; v2's offer and subscription outlive the blocked offers.
TEST_F(test_multiversion_support_aggregate_events, v1s_offer_expiry_stop_subscribes_only_v1) {
    provider_v2_ecu_.config().service_discovery_.ttl_ = 60;
    consumer_ecu_.config().service_discovery_.ttl_ = 60;
    start_apps();
    offer_and_subscribe_both();
    auto const to_v1 = sd_gate({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {provider_v1_ecu_.sd_endpoint()});
    auto const offers = sd_gate(
            udp_party::any(),
            udp_party{boost::asio::ip::udp::endpoint{boost::asio::ip::make_address("224.224.224.0"), 30490}, consumer_ecu_.router_name_});
    offers->block(true);

    EXPECT_TRUE(to_v1->sd_record_.wait_for_any(stop_subscribe_, common::scaled_timeout(std::chrono::seconds(10))))
            << to_v1->sd_record_.to_string();
    expect_v2_still_notified();
    offers->block(false);
}

// client_v1 losing its router connection stop-subscribes v1 only; v1 is offered once and kept requested, so only remove_local() can.
TEST_F(test_multiversion_support_aggregate_events, losing_the_v1_client_stop_subscribes_only_v1) {
    provider_v1_ecu_.config().service_discovery_.repetitions_max_ = 0;
    provider_v1_ecu_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(30);
    provider_v1_ecu_.config().service_discovery_.ttl_ = 60;
    std::string const v1_requester_name{"v1_requester"};
    consumer_ecu_.add_app(v1_requester_name);
    start_apps();
    auto* v1_requester = consumer_ecu_.apps_[v1_requester_name];
    ASSERT_TRUE(v1_requester->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    offer_and_subscribe_both();
    v1_requester->request_service(interface_v1_.instance_);
    ASSERT_TRUE(v1_requester->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    publish_v1_field();
    auto const to_v1 = sd_gate({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {provider_v1_ecu_.sd_endpoint()});

    ASSERT_TRUE(disconnect(client_v1_name_, boost::asio::error::connection_reset, consumer_ecu_.router_name_,
                           boost::asio::error::connection_reset));

    EXPECT_TRUE(to_v1->sd_record_.wait_for_any(stop_subscribe_)) << to_v1->sd_record_.to_string();
    expect_v2_still_notified();
}

// An expired remote subscription is reported to v1's provider only.
TEST_F(test_multiversion_support_aggregate_events, an_expired_v1_subscription_reaches_only_v1s_provider) {
    start_apps();
    offer_and_subscribe_both();
    auto const to_v1 = sd_gate({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {provider_v1_ecu_.sd_endpoint()});
    to_v1->block(true);

    EXPECT_TRUE(subscribed_at_v1_.wait_for_last(false, common::scaled_timeout(std::chrono::seconds(10))))
            << "provider_v1 was not told about the expiry";
    expect_v2_still_notified();
    to_v1->block(false);
}

// Each provider ECU also consumes the other major. find_local_client() ignores the major, so the local
// offer of one major must not make the router treat a subscription to the remote other major as local.
TEST_F(test_multiversion_support_aggregate_events, an_ecu_offering_one_major_can_subscribe_to_the_other) {
    std::string const v2_consumer_name{"v2_consumer_on_v1_ecu"};
    std::string const v1_consumer_name{"v1_consumer_on_v2_ecu"};
    provider_v1_ecu_.add_app(v2_consumer_name);
    provider_v2_ecu_.add_app(v1_consumer_name);
    start_apps();

    auto* v2_consumer = provider_v1_ecu_.apps_[v2_consumer_name];
    auto* v1_consumer = provider_v2_ecu_.apps_[v1_consumer_name];
    ASSERT_TRUE(v2_consumer->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    ASSERT_TRUE(v1_consumer->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

    // The availability callback carries no major, so only a major-scoped handler identifies one.
    auto const await_major = [](app* _app, service_instance const& _si) {
        auto its_available = std::make_shared<attribute_recorder<bool>>();
        _app->get_application()->register_availability_handler(
                _si.service_, _si.instance_,
                [its_available](vsomeip::service_t, vsomeip::instance_t, vsomeip::availability_state_e _state) {
                    its_available->record(_state == vsomeip::availability_state_e::AS_AVAILABLE);
                },
                _si.major_, vsomeip::ANY_MINOR);
        return its_available;
    };
    auto const v1_available = await_major(v1_consumer, interface_v1_.instance_);
    auto const v2_available = await_major(v2_consumer, interface_v2_.instance_);

    record_remote_subscriptions(provider_v1_, field_v1_, subscribed_at_v1_);
    record_remote_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    v1_consumer->request_service(interface_v1_.instance_);
    v2_consumer->request_service(interface_v2_.instance_);
    ASSERT_TRUE(v1_available->wait_for_last(true));
    ASSERT_TRUE(v2_available->wait_for_last(true));

    v1_consumer->subscribe_field(field_v1_);
    v2_consumer->subscribe_field(field_v2_);

    EXPECT_TRUE(subscribed_at_v1_.wait_for_any(true)) << "provider_v1 never saw the subscription from the v2 ECU";
    EXPECT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw the subscription from the v1 ECU";
}

// release_service() must unset the released major's cached field values; notify_one_current_value()
// replays a stale one to the next subscriber.
TEST_F(test_multiversion_support_aggregate_events, a_released_majors_cached_field_value_is_not_replayed) {
    start_apps();
    offer_and_subscribe_both();

    std::vector<unsigned char> const its_later_payload{0x33, 0x33};

    provider_v1_->send_event(field_v1_, payload_v1_);
    ASSERT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;

    client_v1_->release_service(interface_v1_.instance_);

    provider_v1_->send_event(field_v1_, its_later_payload, true);

    client_v1_->message_record_.clear();
    client_v1_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    client_v1_->subscribe_field(field_v1_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, its_later_payload)))
            << client_v1_->message_record_;
    EXPECT_FALSE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_),
                                                          common::scaled_timeout(std::chrono::milliseconds(250))))
            << "the released major's stale field value was replayed" << client_v1_->message_record_;
}

// release_service() must clear the released major's subscriber sets: a stale entry makes
// event::add_subscriber() return false, so the re-subscribe never reaches SD.
TEST_F(test_multiversion_support_aggregate_events, resubscribing_after_a_release_reaches_the_provider_again) {
    start_apps();
    offer_and_subscribe_both();

    provider_v1_->send_event(field_v1_, payload_v1_);
    ASSERT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;

    client_v1_->release_service(interface_v1_.instance_);

    client_v1_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    client_v1_->subscribe_field(field_v1_);

    client_v1_->message_record_.clear();
    provider_v1_->send_event(field_v1_, payload_v1_, true);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
}

TEST_F(test_multiversion_support_aggregate_events, notification_from_v1_reaches_only_client_v1) {
    start_apps();
    offer_and_subscribe_both();

    provider_v1_->send_event(field_v1_, payload_v1_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_last(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
    EXPECT_FALSE(
            client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v1_), std::chrono::milliseconds(250)))
            << client_v2_->message_record_;
}

// Mirror of notification_from_v1_reaches_only_client_v1; ANY_MAJOR resolves to the lowest offered major.
TEST_F(test_multiversion_support_aggregate_events, notification_from_v2_reaches_only_client_v2) {
    start_apps();
    offer_and_subscribe_both();

    provider_v2_->send_event(field_v2_, payload_v2_);

    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
    EXPECT_FALSE(
            client_v1_->message_record_.wait_for_last(notification(interface_v1_.instance_, payload_v2_), std::chrono::milliseconds(250)))
            << client_v1_->message_record_;
}

TEST_F(test_multiversion_support_aggregate_events, each_major_gets_its_own_initial_field_value) {
    start_apps();

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);
    provider_v1_->send_event(field_v1_, payload_v1_);
    provider_v2_->send_event(field_v2_, payload_v2_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->subscribe_field(field_v1_);
    client_v2_->subscribe_field(field_v2_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_last(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_support_aggregate_events, stopping_v1_offer_keeps_v2_notifications_flowing) {
    start_apps();
    offer_and_subscribe_both();

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);

    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

// Same three-ECU topology, but each major defines an event the other one does not. No eventgroup id
// is shared, so the two subscriptions never meet in service_discovery_impl::subscribed_.
struct test_multiversion_support_exclusive_events : public base_fake_socket_fixture {
    void start_apps() {
        provider_v1_ecu_.add_app(provider_v1_name_);
        provider_v2_ecu_.add_app(provider_v2_name_);
        consumer_ecu_.add_app(client_v1_name_);
        consumer_ecu_.add_app(client_v2_name_);

        provider_v1_ecu_.prepare();
        provider_v2_ecu_.prepare();
        consumer_ecu_.prepare();

        provider_v1_ecu_.start_apps();
        provider_v2_ecu_.start_apps();
        consumer_ecu_.start_apps();

        provider_v1_ = provider_v1_ecu_.apps_[provider_v1_name_];
        provider_v2_ = provider_v2_ecu_.apps_[provider_v2_name_];
        client_v1_ = consumer_ecu_.apps_[client_v1_name_];
        client_v2_ = consumer_ecu_.apps_[client_v2_name_];

        ASSERT_TRUE(provider_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(provider_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v1_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_v2_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    // Each provider ECU hosts exactly one major, so a record here proves the subscription reached
    // that major's provider. The consumer-side callback cannot be used: the local SUBSCRIBE_ACK is
    // sent even when the subscription was dropped or NACKed.
    void record_remote_subscriptions(app* _provider, event_ids const& _ei, attribute_recorder<bool>& _record) {
        _provider->register_group_subscription_handler(_ei, [&_record](client_t, uid_t, gid_t, std::string const&, bool _is_subscribed) {
            _record.record(_is_subscribed);
            return true;
        });
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};

    static constexpr vsomeip::event_t exclusive_event_v1_{0x9001};
    static constexpr vsomeip::eventgroup_t exclusive_group_v1_{0x9001};
    static constexpr vsomeip::event_t exclusive_event_v2_{0x9002};
    static constexpr vsomeip::eventgroup_t exclusive_group_v2_{0x9002};
    static constexpr auto reliable_{vsomeip::reliability_type_e::RT_RELIABLE};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {event_spec{exclusive_event_v1_, {exclusive_group_v1_}, reliable_}},
                            {}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {event_spec{exclusive_event_v2_, {exclusive_group_v2_}, reliable_}},
                            {}};

    event_ids only_v1_{interface_v1_.instance_, exclusive_event_v1_, exclusive_group_v1_};
    event_ids only_v2_{interface_v2_.instance_, exclusive_event_v2_, exclusive_group_v2_};

    std::vector<unsigned char> const payload_v1_{0x11, 0x11};
    std::vector<unsigned char> const payload_v2_{0x22, 0x22};

    // A plain event only reaches subscribers that exist at notify() time, so the notification tests
    // must wait for the subscription to be established before sending. The provider handler runs
    // before its routing host accepts the subscription; the consumer ACK only follows that accept.
    [[nodiscard]] bool subscribe_and_await_provider(app* _client, app* _provider, event_ids const& _ei, attribute_recorder<bool>& _record) {
        record_remote_subscriptions(_provider, _ei, _record);
        _client->subscribe_event(_ei);
        return _record.wait_for_any(true)
                && _client->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(_ei));
    }

    // Pins the major, so a match also asserts the notification carried the right interface version.
    message_checker notification(event_ids const& _ei, std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, _ei.si_, _ei.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    // Ignores service/instance/major: a leak carrying the *other* major must still be caught.
    message_checker any_notification(vsomeip::event_t _event, std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, std::nullopt, _event, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    // Declared before the ecu_setups: the provider's subscription handler holds a reference, so
    // these must outlive the apps.
    attribute_recorder<bool> subscribed_at_v1_;
    attribute_recorder<bool> subscribed_at_v2_;

    ecu_setup provider_v1_ecu_{"provider_v1", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup provider_v2_ecu_{"provider_v2", ecu_config{boardnet::ecu_two_config}.add_interface({interface_v2_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* provider_v1_;
    app* provider_v2_;
    app* client_v1_;
    app* client_v2_;
};

TEST_F(test_multiversion_support_exclusive_events, subscription_to_an_exclusive_event_reaches_only_its_own_provider) {
    start_apps();
    record_remote_subscriptions(provider_v1_, only_v1_, subscribed_at_v1_);
    record_remote_subscriptions(provider_v2_, only_v2_, subscribed_at_v2_);

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->subscribe_event(only_v1_);
    client_v2_->subscribe_event(only_v2_);

    EXPECT_TRUE(subscribed_at_v1_.wait_for_any(true)) << "provider_v1 never saw a subscription to its exclusive event";
    EXPECT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw a subscription to its exclusive event";
}

TEST_F(test_multiversion_support_exclusive_events, exclusive_event_notification_reaches_only_its_own_major) {
    start_apps();

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    ASSERT_TRUE(subscribe_and_await_provider(client_v1_, provider_v1_, only_v1_, subscribed_at_v1_));
    ASSERT_TRUE(subscribe_and_await_provider(client_v2_, provider_v2_, only_v2_, subscribed_at_v2_));

    provider_v1_->send_event(only_v1_, payload_v1_);
    provider_v2_->send_event(only_v2_, payload_v2_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(only_v1_, payload_v1_))) << client_v1_->message_record_;
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(only_v2_, payload_v2_))) << client_v2_->message_record_;

    EXPECT_FALSE(
            client_v2_->message_record_.wait_for_any(any_notification(exclusive_event_v1_, payload_v1_), std::chrono::milliseconds(250)))
            << client_v2_->message_record_;
    EXPECT_FALSE(
            client_v1_->message_record_.wait_for_any(any_notification(exclusive_event_v2_, payload_v2_), std::chrono::milliseconds(250)))
            << client_v1_->message_record_;
}

TEST_F(test_multiversion_support_exclusive_events, subscribing_to_v1_exclusive_event_keeps_v2_working) {
    start_apps();

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    ASSERT_TRUE(subscribe_and_await_provider(client_v2_, provider_v2_, only_v2_, subscribed_at_v2_));
    provider_v2_->send_event(only_v2_, payload_v2_);
    ASSERT_TRUE(client_v2_->message_record_.wait_for_any(notification(only_v2_, payload_v2_))) << client_v2_->message_record_;

    ASSERT_TRUE(subscribe_and_await_provider(client_v1_, provider_v1_, only_v1_, subscribed_at_v1_));

    client_v2_->message_record_.clear();
    provider_v2_->send_event(only_v2_, payload_v2_, true);

    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(only_v2_, payload_v2_))) << client_v2_->message_record_;
}

TEST_F(test_multiversion_support_exclusive_events, unsubscribing_from_v1_exclusive_event_keeps_v2_working) {
    start_apps();

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);

    client_v1_->request_service(interface_v1_.instance_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    ASSERT_TRUE(subscribe_and_await_provider(client_v1_, provider_v1_, only_v1_, subscribed_at_v1_));
    ASSERT_TRUE(subscribe_and_await_provider(client_v2_, provider_v2_, only_v2_, subscribed_at_v2_));

    provider_v2_->send_event(only_v2_, payload_v2_);
    ASSERT_TRUE(client_v2_->message_record_.wait_for_any(notification(only_v2_, payload_v2_))) << client_v2_->message_record_;

    client_v1_->unsubscribe(only_v1_);

    client_v2_->message_record_.clear();
    provider_v2_->send_event(only_v2_, payload_v2_, true);

    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(only_v2_, payload_v2_))) << client_v2_->message_record_;
}

// "must not require anything from the other major": major 2 is never offered and never consumed.
TEST_F(test_multiversion_support_exclusive_events, an_exclusive_event_is_consumable_while_the_other_major_is_absent) {
    start_apps();

    provider_v1_->offer(interface_v1_);

    client_v1_->request_service(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

    ASSERT_TRUE(subscribe_and_await_provider(client_v1_, provider_v1_, only_v1_, subscribed_at_v1_));

    provider_v1_->send_event(only_v1_, payload_v1_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(only_v1_, payload_v1_))) << client_v1_->message_record_;
}

// One provider, one consumer that requests without naming a major (so its events land in the
// ANY_MAJOR bucket). Two fields share an eventgroup; the consumer subscribes to the group with
// ANY_EVENT but only ever registers field_a_, so a notification for field_b_ arrives for an event
// the host does not know -- deliver_notification()'s caching branch, gated by
// has_subscribed_eventgroup().
struct test_multiversion_any_major_requester : public base_fake_socket_fixture {
    void start_apps() {
        provider_ecu_.add_app(provider_name_);
        consumer_ecu_.add_app(client_name_);

        provider_ecu_.prepare();
        consumer_ecu_.prepare();

        provider_ecu_.start_apps();
        consumer_ecu_.start_apps();

        provider_ = provider_ecu_.apps_[provider_name_];
        client_ = consumer_ecu_.apps_[client_name_];

        ASSERT_TRUE(provider_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    std::string const provider_name_{"provider_v1"};
    std::string const client_name_{"client_v1"};

    static constexpr vsomeip::event_t event_a_{0x8001};
    static constexpr vsomeip::event_t event_b_{0x8002};
    static constexpr vsomeip::eventgroup_t shared_group_{0x8001};
    static constexpr auto reliable_{vsomeip::reliability_type_e::RT_RELIABLE};

    interface interface_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                         {},
                         {event_spec{event_a_, {shared_group_}, reliable_}, event_spec{event_b_, {shared_group_}, reliable_}}};

    event_ids field_a_{interface_.instance_, event_a_, shared_group_};
    event_ids field_b_{interface_.instance_, event_b_, shared_group_};

    std::vector<unsigned char> const payload_b_{0xbb, 0xbb};

    message_checker notification(vsomeip::event_t _event, std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, std::nullopt, _event, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    ecu_setup provider_ecu_{"provider_v1", ecu_config{boardnet::ecu_one_config}.add_interface({interface_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* provider_;
    app* client_;
};

TEST_F(test_multiversion_any_major_requester, unregistered_event_is_cached_for_an_any_major_requester) {
    start_apps();

    provider_->offer(interface_);

    client_->get_application()->request_service(0x1000, 0x1);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::available(interface_.instance_)));

    // Registers only field_a_, subscribes the whole eventgroup (ANY_EVENT).
    client_->subscribe_eventgroup_field(field_a_);
    ASSERT_TRUE(client_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_a_)));

    // field_b_ is unknown to the consumer host at this point.
    provider_->send_event(field_b_, payload_b_);
    ASSERT_FALSE(client_->message_record_.wait_for_any(notification(event_b_, payload_b_), std::chrono::milliseconds(250)))
            << "the caching branch must not forward" << client_->message_record_;

    // Registering field_b_ must surface the cached value at once. Without the cache the value is
    // not lost -- SD notices the missing initial field value and forces a StopSubscribe/Subscribe --
    // but that repair costs time (SD traffic)
    client_->get_application()->request_event(0x1000, 0x1, event_b_, {shared_group_}, vsomeip::event_type_e::ET_FIELD, reliable_);
    client_->get_application()->subscribe(0x1000, 0x1, shared_group_, 0x1, event_b_);

    EXPECT_TRUE(client_->message_record_.wait_for_any(notification(event_b_, payload_b_),
                                                      common::scaled_timeout(std::chrono::milliseconds(250))))
            << client_->message_record_;
}
}
