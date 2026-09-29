// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "sample_configurations.hpp"

#include "../../../implementation/service_discovery/include/eventgroupentry_impl.hpp"
#include "../../../implementation/service_discovery/include/selective_option_impl.hpp"

#include "helpers/app.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/command_gate.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/message_checker.hpp"
#include "helpers/someip_gate.hpp"

#include "common/timeout_scale.hpp"

#include <vsomeip/vsomeip.hpp>
#include <gtest/gtest.h>

#include <cstdlib>

namespace vsomeip_v3::testing {

std::string const router_one_name_ = "router_one";
std::string const router_two_name_ = "router_two";
std::string const ecu_two_server_name_ = "ecu_two_server";
std::string const ecu_one_client_name_ = "ecu_one_client";
std::string const ecu_one_server_name_ = "ecu_one_server";

struct test_field_resubscribe : public base_fake_socket_fixture {
    std::vector<event_spec> const events_{{0x8010, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE},
                                          {0x8011, {0x1, 0x5}, vsomeip::reliability_type_e::RT_UNRELIABLE},
                                          {0x8018, {0x4}, vsomeip::reliability_type_e::RT_UNRELIABLE}};
    std::vector<event_spec> const fields_{{0x8012, {0x2}, vsomeip::reliability_type_e::RT_UNRELIABLE},
                                          {0x8013, {0x3}, vsomeip::reliability_type_e::RT_RELIABLE},
                                          {0x8014, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE},
                                          {0x8015, {0x1, 0x5}, vsomeip::reliability_type_e::RT_UNRELIABLE},
                                          {0x8016, {0x1, 0x5}, vsomeip::reliability_type_e::RT_UNRELIABLE}};
    interface iface_{0x3348, events_, fields_};

    ecu_config ecu_one_cfg_{boardnet::ecu_one_config};
    ecu_config ecu_two_cfg_{boardnet::ecu_two_config};

    ecu_setup ecu_one_{"ecu_one", ecu_one_cfg_.add_interface({iface_}), *socket_manager_};
    ecu_setup ecu_two_{"ecu_two", ecu_two_cfg_.add_interface({iface_}), *socket_manager_};

    event_ids udp_field_{iface_.instance_, iface_.fields_[0]};
    event_ids tcp_field_{iface_.instance_, iface_.fields_[1]};
    event_ids mix_event_group_{iface_.instance_, iface_.fields_[2]};
    event_ids shared_event_group_field_{iface_.instance_, iface_.fields_[4]};
    event_ids plain_event_{iface_.instance_, iface_.events_[2]};

    std::vector<unsigned char> const payload_{0x5, 0x3};

    std::shared_ptr<someip_gate> ecu_one_sd_send_gate_ = someip_gate::create("ecu_one_sd_send_gate");
    std::shared_ptr<someip_gate> ecu_two_sd_send_gate_ = someip_gate::create("ecu_two_sd_send_gate");
    std::shared_ptr<someip_gate> notify_gate_ = someip_gate::create("notify_gate_");

    boost::asio::ip::udp::endpoint client_endpoint() { return {boardnet::ecu_one_config.unicast_ip_, 30491}; }

    void prepare_ecus_and_apps() {
        ecu_one_.add_app(ecu_one_client_name_);
        ecu_one_.add_app(ecu_one_server_name_);
        ecu_two_.add_app(ecu_two_server_name_);

        ecu_one_.prepare();
        ecu_two_.prepare();

        ASSERT_TRUE(
                set_udp_data_pipe({ecu_one_.sd_endpoint(), router_one_name_}, udp_party::any(), ecu_one_sd_send_gate_->get_data_pipe()));
        ASSERT_TRUE(
                set_udp_data_pipe({ecu_two_.sd_endpoint(), router_two_name_}, udp_party::any(), ecu_two_sd_send_gate_->get_data_pipe()));

        ecu_one_.start_apps();
        ecu_two_.start_apps();
    }
};

TEST_F(test_field_resubscribe, field_resubscribe_recovers_after_lost_initial_notification_udp) {
    // If a field's initial notification is lost, the first cyclic offer arriving at least half a cycle after
    // the subscribe must make the client force a StopSubscribe(ttl=0)+Subscribe pair, which prompts the
    // server to resend the initial value.
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {client_endpoint(), router_one_name_}, notify_gate_->get_data_pipe()));

    server->offer(iface_);
    server->send_event(udp_field_, payload_);

    // Arm the gate before subscribing: the subscription triggers the initial field
    // delivery, which the gate must intercept.
    notify_gate_->block_at(
            {.service_ = iface_.instance_.service_, .method_ = udp_field_.event_id_, .type_ = vsomeip::message_type_e::MT_NOTIFICATION});

    router_one->request_service(iface_.instance_);
    router_one->subscribe_field({udp_field_});
    ASSERT_TRUE(notify_gate_->wait_for_blocked());

    // The first cyclic offer arriving at least half a cycle after the subscribe must force a
    // StopSubscribe(ttl=0) immediately followed by Subscribe.
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));

    // Release the withheld notification — recovery should complete.
    notify_gate_->block(false);
    message_checker checker{std::nullopt, iface_.instance_, udp_field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, payload_};
    EXPECT_TRUE(router_one->message_record_.wait_for(checker));
}

TEST_F(test_field_resubscribe, field_resubscribe_recovers_after_lost_initial_notification_tcp) {
    // Same scenario as the UDP variant above, but for a TCP-transported field. The
    // initial notification travels over the TCP boardnet connection between the two
    // routers, so it must be intercepted there.
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    ASSERT_TRUE(setup_data_pipe(router_one_name_, router_two_name_, socket_role::client, notify_gate_->get_data_pipe()));

    server->offer(iface_);
    server->send_event(tcp_field_, payload_);

    // Arm the gate before subscribing: the subscription triggers the initial field
    // delivery, which the gate must intercept.
    notify_gate_->block_at(
            {.service_ = iface_.instance_.service_, .method_ = tcp_field_.event_id_, .type_ = vsomeip::message_type_e::MT_NOTIFICATION});

    router_one->request_service(iface_.instance_);
    router_one->subscribe_field({tcp_field_});
    ASSERT_TRUE(notify_gate_->wait_for_blocked());

    // The first cyclic offer arriving at least half a cycle after the subscribe must force a
    // StopSubscribe(ttl=0) immediately followed by Subscribe.
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));

    // Release the withheld notification — recovery should complete.
    notify_gate_->block(false);
    message_checker tcp_checker{std::nullopt, iface_.instance_, tcp_field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, payload_};
    EXPECT_TRUE(router_one->message_record_.wait_for(tcp_checker));
}

TEST_F(test_field_resubscribe, field_resubscribe_recovers_after_lost_initial_notification_mix_event_group) {
    // Same scenario as the variants above, though for a field in a mixed event group.
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {client_endpoint(), router_one_name_}, notify_gate_->get_data_pipe()));

    server->offer(iface_);
    server->send_event(mix_event_group_, payload_);

    // Arm the gate before subscribing: the subscription triggers the initial field
    // delivery, which the gate must intercept.
    notify_gate_->block_at({.service_ = iface_.instance_.service_,
                            .method_ = mix_event_group_.event_id_,
                            .type_ = vsomeip::message_type_e::MT_NOTIFICATION});

    router_one->request_service(iface_.instance_);
    router_one->subscribe_eventgroup_field({mix_event_group_});
    ASSERT_TRUE(notify_gate_->wait_for_blocked());

    // The first cyclic offer arriving at least half a cycle after the subscribe must force a
    // StopSubscribe(ttl=0) immediately followed by Subscribe.
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));

    // Release the withheld notification — recovery should complete.
    notify_gate_->block(false);
    message_checker checker{std::nullopt, iface_.instance_, mix_event_group_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, payload_};
    EXPECT_TRUE(router_one->message_record_.wait_for(checker));
}

TEST_F(test_field_resubscribe, field_resubscribe_recovers_after_lost_initial_notification_shared_event_group) {
    // Same scenario as the variants above, though for fields in the same shared event groups.
    prepare_ecus_and_apps();

    auto* client_one = ecu_one_.apps_[ecu_one_client_name_];
    auto* client_two = ecu_one_.apps_[ecu_one_server_name_];
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {client_endpoint(), router_one_name_}, notify_gate_->get_data_pipe()));

    server->offer(iface_);
    server->send_event(shared_event_group_field_, payload_);

    // Arm the gate before subscribing: the subscription triggers the initial field
    // delivery, which the gate must intercept.
    notify_gate_->block_at({.service_ = iface_.instance_.service_,
                            .method_ = shared_event_group_field_.event_id_,
                            .type_ = vsomeip::message_type_e::MT_NOTIFICATION});

    client_one->request_service(iface_.instance_);
    client_two->request_service(iface_.instance_);
    client_one->subscribe_field({shared_event_group_field_});
    client_one->subscribe_field({iface_.instance_, iface_.fields_[3]});
    client_one->subscribe_field({iface_.instance_, iface_.events_[1]});
    client_two->subscribe_field({shared_event_group_field_});
    client_two->subscribe_field({iface_.instance_, iface_.fields_[3]});
    client_two->subscribe_field({iface_.instance_, iface_.events_[1]});

    ASSERT_TRUE(notify_gate_->wait_for_blocked());

    // The first cyclic offer arriving at least half a cycle after the subscribe must force a
    // StopSubscribe(ttl=0) immediately followed by Subscribe.
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));

    // Release the withheld notification — recovery should complete.
    notify_gate_->block(false);
    message_checker checker{std::nullopt, iface_.instance_, shared_event_group_field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION,
                            payload_};
    EXPECT_TRUE(client_one->message_record_.wait_for(checker));
    EXPECT_TRUE(client_two->message_record_.wait_for(checker));

    // A later cyclic offer shall still trigger a StopSubscribe+Subscribe pair, because one of the fields is
    // still missing the initial notification.
    ecu_one_sd_send_gate_->sd_record_.clear();
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));
}

TEST_F(test_field_resubscribe, field_resubscribe_repeats_while_value_still_missing) {
    // Retries are unbounded by design: as long as the field's value never arrives,
    // subsequent cyclic offers must keep forcing the stop+subscribe pair.
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {client_endpoint(), router_one_name_}, notify_gate_->get_data_pipe()));

    server->offer(iface_);
    server->send_event(udp_field_, payload_);

    notify_gate_->block_at(
            {.service_ = iface_.instance_.service_, .method_ = udp_field_.event_id_, .type_ = vsomeip::message_type_e::MT_NOTIFICATION});

    router_one->request_service(iface_.instance_);
    router_one->subscribe_field({udp_field_});
    ASSERT_TRUE(notify_gate_->wait_for_blocked());

    // First forced resubscribe, once half a cycle has passed without the initial value.
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}));

    // The notification stays blocked — a later cyclic offer must trigger it again.
    ecu_one_sd_send_gate_->sd_record_.clear();
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}));
}

TEST_F(test_field_resubscribe, no_forced_resubscribe_for_plain_event) {
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    server->offer(iface_);
    server->send_event(plain_event_, payload_);

    router_one->request_service(iface_.instance_);
    router_one->subscribe_event({plain_event_});

    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}, std::chrono::seconds(2)));
    ASSERT_FALSE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, std::chrono::seconds(2)));
}

TEST_F(test_field_resubscribe, no_forced_resubscribe_once_initial_value_delivered) {
    // Regression guard for the healthy path: once the initial notification is
    // delivered normally, subsequent cyclic offers must not force a resubscribe.
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    server->offer(iface_);
    server->send_event(udp_field_, payload_);

    router_one->request_service(iface_.instance_);
    router_one->subscribe_field({udp_field_});

    message_checker checker{std::nullopt, iface_.instance_, udp_field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, payload_};
    ASSERT_TRUE(router_one->message_record_.wait_for(checker));

    ecu_one_sd_send_gate_->sd_record_.clear();
    EXPECT_FALSE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}));
}

TEST_F(test_field_resubscribe, field_resubscribe_late_field_registration) {
    // Ensure the initial notification is properly tracked even if the field
    // is registered after a notification has been sent for another field of the eventgroup.
    prepare_ecus_and_apps();

    auto* client_one = ecu_one_.apps_[ecu_one_client_name_];
    auto* client_two = ecu_one_.apps_[ecu_one_server_name_];
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {client_endpoint(), router_one_name_}, notify_gate_->get_data_pipe()));

    server->offer(iface_);
    server->send_event(shared_event_group_field_, payload_);

    client_one->request_service(iface_.instance_);
    client_two->request_service(iface_.instance_);
    client_one->subscribe_field({shared_event_group_field_});
    client_two->subscribe_field({shared_event_group_field_});

    message_checker checker{std::nullopt, iface_.instance_, shared_event_group_field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION,
                            payload_};
    EXPECT_TRUE(client_one->message_record_.wait_for(checker));
    EXPECT_TRUE(client_two->message_record_.wait_for(checker));

    ecu_one_sd_send_gate_->sd_record_.clear();

    // Clients now subscribe to another field in the same shared event group, which has not yet been notified.
    client_one->subscribe_field({iface_.instance_, iface_.fields_[3]});
    client_two->subscribe_field({iface_.instance_, iface_.fields_[3]});

    // A cyclic offer arriving at least half a cycle after the subscribe must force a StopSubscribe(ttl=0)
    // immediately followed by Subscribe, because the newly registered field has not yet been notified.
    ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));
}

constexpr auto half_cycle_ = std::chrono::milliseconds(VSOMEIP_SD_DEFAULT_CYCLIC_OFFER_DELAY / 2);

// The repetition phase doubles its delay each round, so pinning the base delay puts the offers at
// known times (~200/600/1400/3000ms) and lands the 600ms one inside the grace period the tests
// observe. Left unset, vsomeip's own 10ms default would finish the whole burst before the tests
// start looking.
constexpr uint32_t repetitions_base_delay_{200};

struct test_forced_resubscribe_timing : public base_fake_socket_fixture {

    std::vector<event_spec> const events_{{0x8010, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE},
                                          {0x8017, {0x6}, vsomeip::reliability_type_e::RT_UNRELIABLE}};
    std::vector<event_spec> const fields_{{0x8012, {0x2}, vsomeip::reliability_type_e::RT_UNRELIABLE}};
    interface iface_{0x334a, events_, fields_};

    ecu_config ecu_one_cfg_{boardnet::ecu_one_config};
    ecu_config ecu_two_cfg_{boardnet::ecu_two_config};

    ecu_setup ecu_one_{"ecu_one", ecu_one_cfg_.add_interface({iface_}), *socket_manager_};
    ecu_setup ecu_two_{"ecu_two", ecu_two_cfg_.add_interface({iface_}), *socket_manager_};

    event_ids udp_field_{iface_.instance_, iface_.fields_[0]};
    event_ids plain_only_event_{iface_.instance_, iface_.events_[1]};

    std::vector<unsigned char> const payload_{0x5, 0x3};

    std::shared_ptr<someip_gate> ecu_one_sd_send_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> ecu_two_sd_multicast_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> ecu_two_sd_unicast_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> notify_gate_ = someip_gate::create();

    boost::asio::ip::udp::endpoint const multicast_ep_{boost::asio::ip::make_address("224.224.224.0"), 30490};

    boost::asio::ip::udp::endpoint client_endpoint() { return {boardnet::ecu_one_config.unicast_ip_, 30491}; }

    void prepare_ecus_and_apps() {
        for (auto* its_ecu : {&ecu_one_, &ecu_two_}) {
            its_ecu->config().service_discovery_.repetitions_base_delay_ = repetitions_base_delay_;
        }

        ecu_two_.add_app(ecu_two_server_name_);

        ecu_one_.prepare();
        ecu_two_.prepare();

        ASSERT_TRUE(
                set_udp_data_pipe({ecu_one_.sd_endpoint(), router_one_name_}, udp_party::any(), ecu_one_sd_send_gate_->get_data_pipe()));
        // SubscribeAcks are unicast and cyclic offers are multicast. someip_gate blocking stalls the
        // whole pipe, so they need separate pipes: stalling the acks must not hold back the offers.
        ASSERT_TRUE(set_udp_data_pipe({ecu_two_.sd_endpoint(), router_two_name_}, {ecu_one_.sd_endpoint(), router_one_name_},
                                      ecu_two_sd_unicast_gate_->get_data_pipe()));
        ASSERT_TRUE(set_udp_data_pipe({ecu_two_.sd_endpoint(), router_two_name_}, {multicast_ep_},
                                      ecu_two_sd_multicast_gate_->get_data_pipe()));

        ecu_one_.start_apps();
        ecu_two_.start_apps();
    }

    // Subscribes to the field and withholds its initial notification.
    // Returns with the subscribe just sent, so the half-cycle window starts here.
    void subscribe_field_with_initial_value_withheld() {
        auto* server = ecu_two_.apps_[ecu_two_server_name_];

        ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {client_endpoint(), router_one_name_}, notify_gate_->get_data_pipe()));

        server->offer(iface_);
        server->send_event(udp_field_, payload_);

        notify_gate_->block_at({.service_ = iface_.instance_.service_,
                                .method_ = udp_field_.event_id_,
                                .type_ = vsomeip::message_type_e::MT_NOTIFICATION});

        ecu_one_.router_->request_service(iface_.instance_);
        ecu_one_.router_->subscribe_field({udp_field_});
        ASSERT_TRUE(notify_gate_->wait_for_blocked());
    }
};

// The negative windows, where we expect the StopSubscribe/Subscribe to not happen,
// should not be scaled. If we scale the windows, we could receive a cyclic offer
// outside the grace period which would trigger a StopSubscribe/Subscribe.
TEST_F(test_forced_resubscribe_timing, no_forced_resubscribe_within_half_cycle_of_subscribe_when_initial_field_notification_withheld) {
    prepare_ecus_and_apps();
    // Don't send the initial notification to the field event, which should trigger the StopSubscribe/Subscribe
    subscribe_field_with_initial_value_withheld();

    ecu_one_sd_send_gate_->sd_record_.clear();

    // The repetition-phase offer at ~200ms after the subscribe lands inside this window and must be
    // answered with a plain Subscribe only.
    // Check that the service consumer doesn't send a StopSubscribe within the grace period.
    // Use a slightly smaller window to ensure that we are really within the grace period, and it didn't elapse already.
    // Check that the client sent a subscribe, which ensures that the service consumer has received an offer
    EXPECT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}, half_cycle_ * 9 / 10));
    EXPECT_FALSE(
            ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, std::chrono::milliseconds(1)));

    // Once half a cycle has passed without the initial value, the pair must go out.
    EXPECT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));
}

TEST_F(test_forced_resubscribe_timing, forced_resubscribe_when_subscribe_ack_withheld) {
    prepare_ecus_and_apps();

    auto* server = ecu_two_.apps_[ecu_two_server_name_];
    server->offer(iface_);

    // Stall the SubscribeAck on the unicast pipe. Offers keep arriving over multicast, so the
    // service stays available and its TTL keeps being refreshed.
    ecu_two_sd_unicast_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::SUBSCRIBE_EVENTGROUP_ACK, 3});

    ecu_one_.router_->request_service(iface_.instance_);
    ecu_one_.router_->subscribe_event({plain_only_event_});
    ASSERT_TRUE(ecu_two_sd_unicast_gate_->wait_for_blocked());

    ecu_one_sd_send_gate_->sd_record_.clear();

    // Check that the service consumer doesn't send a StopSubscribe within the grace period.
    // Use a slightly smaller window to ensure that we are really within the grace period, and it didn't elapse already.
    EXPECT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}, half_cycle_ * 9 / 10));
    EXPECT_FALSE(
            ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, std::chrono::milliseconds(1)));

    // No field is involved, so this pair can only be driven by the missing SubscribeAck.
    EXPECT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_sequence(
            {{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}}));
}

// Tests for the selective subscriptions.
// An eventgroup is selective only when it holds exactly one event and that event was registered as
// ET_SELECTIVE_EVENT, so the interface below carries a single event in its own eventgroup and both
// subscribers must go through subscribe_selective. Only ecu_two offers the service.
struct test_selective_subscription : public base_fake_socket_fixture {
    std::string const subscriber_name_ = "selective_subscriber";

    interface iface_{0x334b, {event_spec{0x8018, {0x7}, vsomeip::reliability_type_e::RT_UNRELIABLE}}, {}};

    ecu_config ecu_one_cfg_{boardnet::ecu_one_config};
    ecu_config ecu_two_cfg_{boardnet::ecu_two_config};

    ecu_setup ecu_one_{"ecu_one", ecu_one_cfg_.add_interface({iface_}), *socket_manager_};
    ecu_setup ecu_two_{"ecu_two", ecu_two_cfg_.add_interface({iface_}), *socket_manager_};

    event_ids selective_event_{iface_.instance_, iface_.events_[0]};

    std::shared_ptr<someip_gate> ecu_two_sd_unicast_gate_ = someip_gate::create();

    std::shared_ptr<attribute_recorder<someip_sd_record_message>> sd_record_ =
            std::make_shared<attribute_recorder<someip_sd_record_message>>();
    std::shared_ptr<attribute_recorder<std::set<vsomeip::client_t>>> selective_clients_ =
            std::make_shared<attribute_recorder<std::set<vsomeip::client_t>>>();
    std::shared_ptr<attribute_recorder<vsomeip::client_t>> subscribing_clients_ = std::make_shared<attribute_recorder<vsomeip::client_t>>();

    std::shared_ptr<data_pipe> sd_send_pipe_ = std::make_shared<data_pipe>(static_cast<data_pipe::external_message_checker_t>(
            [its_sd_record = sd_record_, its_selective_clients = selective_clients_](someip_message const& _message) {
                if (_message.sd_) {
                    for (auto const& its_entry : _message.sd_->get_entries()) {
                        its_sd_record->record(someip_sd_record_message{its_entry->get_type(), its_entry->get_ttl()});
                        if (its_entry->get_type() == sd::entry_type_e::SUBSCRIBE_EVENTGROUP) {
                            auto const its_eventgroup_entry = std::static_pointer_cast<sd::eventgroupentry_impl>(its_entry);
                            if (auto const its_option = its_eventgroup_entry->get_selective_option()) {
                                its_selective_clients->record(its_option->get_clients());
                            }
                        }
                    }
                }
                return data_pipe_state::OPEN;
            }));

    void prepare_ecus_and_apps() {
        for (auto* its_ecu : {&ecu_one_, &ecu_two_}) {
            its_ecu->config().service_discovery_.repetitions_base_delay_ = repetitions_base_delay_;
        }

        ecu_one_.add_app(subscriber_name_);
        ecu_two_.add_app(ecu_two_server_name_);

        ecu_one_.prepare();
        ecu_two_.prepare();

        ASSERT_TRUE(set_udp_data_pipe({ecu_one_.sd_endpoint(), router_one_name_}, udp_party::any(), sd_send_pipe_));
        // SubscribeAcks are unicast and cyclic offers are multicast. someip_gate blocking stalls the
        // whole pipe, so they need separate pipes: stalling the acks must not hold back the offers.
        ASSERT_TRUE(set_udp_data_pipe({ecu_two_.sd_endpoint(), router_two_name_}, {ecu_one_.sd_endpoint(), router_one_name_},
                                      ecu_two_sd_unicast_gate_->get_data_pipe()));

        ecu_one_.start_apps();
        ecu_two_.start_apps();

        ASSERT_TRUE(ecu_one_.router_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(ecu_one_.apps_[subscriber_name_]->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }
};

TEST_F(test_selective_subscription, selective_subscription_grows_the_client_option) {
    prepare_ecus_and_apps();

    auto* client_one = ecu_one_.router_;
    auto* client_two = ecu_one_.apps_[subscriber_name_];
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    server->register_group_subscription_handler(selective_event_,
                                                [its_subscribing_clients = subscribing_clients_](vsomeip::client_t _client, vsomeip::uid_t,
                                                                                                 vsomeip::gid_t, std::string const&,
                                                                                                 bool _is_subscribed) {
                                                    if (_is_subscribed) {
                                                        its_subscribing_clients->record(_client);
                                                    }
                                                    return true;
                                                });

    server->offer(iface_);

    client_one->request_service(iface_.instance_);
    client_two->request_service(iface_.instance_);

    std::set<vsomeip::client_t> const its_first_client{client_one->get_client_id()};
    std::set<vsomeip::client_t> const its_clients{client_one->get_client_id(), client_two->get_client_id()};

    client_one->subscribe_selective(selective_event_);
    ASSERT_TRUE(selective_clients_->wait_for_last(its_first_client))
            << "the first Subscribe carried no selective option, expected client " << client_one->get_client_id()
            << ", recorded: " << selective_clients_->to_string();
    ASSERT_TRUE(client_one->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(selective_event_)))
            << "the first client was never acknowledged";
    ASSERT_TRUE(subscribing_clients_->wait_for_last(client_one->get_client_id()))
            << "the provider was not told about the first client, expected " << client_one->get_client_id()
            << ", recorded: " << subscribing_clients_->to_string();

    // The acknowledgement answering the joining client lists every client of the eventgroup, so only
    // the ones still waiting may be reported upwards. Subscribing the second client once the first one
    // is acknowledged is what makes that observable.
    client_one->subscription_record_.clear();
    subscribing_clients_->clear();

    client_two->subscribe_selective(selective_event_);
    EXPECT_TRUE(selective_clients_->wait_for_last(its_clients))
            << "the Subscribe for the joining client did not carry both clients, expected " << client_one->get_client_id() << " and "
            << client_two->get_client_id() << ", recorded: " << selective_clients_->to_string();

    EXPECT_TRUE(subscribing_clients_->wait_for_last(client_two->get_client_id()))
            << "the provider was not told about the joining client, expected " << client_two->get_client_id()
            << ", recorded: " << subscribing_clients_->to_string();
    EXPECT_FALSE(subscribing_clients_->wait_for_any(client_one->get_client_id(), std::chrono::seconds(1)))
            << "the provider was told about the established client again, recorded: " << subscribing_clients_->to_string();

    EXPECT_TRUE(client_two->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(selective_event_)));
    EXPECT_FALSE(client_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(selective_event_),
                                                               std::chrono::seconds(1)))
            << "the established client was acknowledged a second time, recorded: " << client_one->subscription_record_.to_string();

    // Both clients are acknowledged and no field is involved, so nothing is ever waiting and the pair
    // must never be forced.
    EXPECT_FALSE(sd_record_->wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, std::chrono::seconds(3)));
}

TEST_F(test_selective_subscription, forced_resubscribe_covers_every_selective_client) {
    prepare_ecus_and_apps();

    auto* client_one = ecu_one_.router_;
    auto* client_two = ecu_one_.apps_[subscriber_name_];
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    server->offer(iface_);

    // Stall the SubscribeAck on the unicast pipe, so both clients stay unacknowledged and both end up
    // in the waiting set. Offers keep arriving over multicast, so the service stays available.
    ecu_two_sd_unicast_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::SUBSCRIBE_EVENTGROUP_ACK, 3});

    client_one->request_service(iface_.instance_);
    client_two->request_service(iface_.instance_);

    // Subscribe the clients one after the other.
    client_one->subscribe_selective(selective_event_);
    std::set<vsomeip::client_t> const its_first_client{client_one->get_client_id()};
    ASSERT_TRUE(selective_clients_->wait_for_last(its_first_client))
            << "expected client " << client_one->get_client_id() << ", recorded: " << selective_clients_->to_string();

    client_two->subscribe_selective(selective_event_);
    std::set<vsomeip::client_t> const its_clients{client_one->get_client_id(), client_two->get_client_id()};
    ASSERT_TRUE(selective_clients_->wait_for_last(its_clients))
            << "both clients were never part of the same subscription, expected " << client_one->get_client_id() << " and "
            << client_two->get_client_id() << ", recorded: " << selective_clients_->to_string();

    sd_record_->clear();
    selective_clients_->clear();

    // Check that the service consumer doesn't send a StopSubscribe within the grace period.
    EXPECT_TRUE(sd_record_->wait_for_any({sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}, half_cycle_ * 9 / 10));
    EXPECT_FALSE(sd_record_->wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, std::chrono::milliseconds(1)));

    // One pair for the eventgroup, not one per client, and it must still address both of them.
    EXPECT_TRUE(
            sd_record_->wait_for_sequence({{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}},
                                          common::scaled_timeout(std::chrono::seconds(5))));
    EXPECT_TRUE(selective_clients_->wait_for_last(its_clients))
            << "the forced Subscribe dropped a client, expected " << client_one->get_client_id() << " and " << client_two->get_client_id()
            << ", recorded: " << selective_clients_->to_string();
}

// The acknowledgement state is the one thing tracked per client, so it is the only way two clients of
// the same selective subscription can diverge: the field-missing condition is per eventgroup, and this
// interface has no field at all, so a withheld ack is the only thing that can force the pair.
TEST_F(test_selective_subscription, unacknowledged_client_forces_a_resubscribe_for_the_whole_eventgroup) {
    prepare_ecus_and_apps();

    auto* client_one = ecu_one_.router_;
    auto* client_two = ecu_one_.apps_[subscriber_name_];
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    server->offer(iface_);

    client_one->request_service(iface_.instance_);
    client_two->request_service(iface_.instance_);

    client_one->subscribe_selective(selective_event_);
    ASSERT_TRUE(client_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(selective_event_)))
            << "the first client was never acknowledged";

    // Stall the unicast pipe only once the first client is acknowledged, so every later ack is held
    // and the second client can never be acknowledged. Offers keep arriving over multicast.
    ecu_two_sd_unicast_gate_->block_at(someip_gate::sd_trigger{sd::entry_type_e::SUBSCRIBE_EVENTGROUP_ACK, 3});

    client_two->subscribe_selective(selective_event_);
    ASSERT_TRUE(ecu_two_sd_unicast_gate_->wait_for_blocked()) << "no acknowledgement was held back";

    EXPECT_FALSE(client_two->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(selective_event_),
                                                               std::chrono::seconds(1)))
            << "the second client was acknowledged even though its ack was held";

    sd_record_->clear();
    selective_clients_->clear();

    // The pair is built per eventgroup, so the unacknowledged client makes it tear down and
    // re-establish the acknowledged client's subscription as well.
    EXPECT_TRUE(
            sd_record_->wait_for_sequence({{sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}, {sd::entry_type_e::SUBSCRIBE_EVENTGROUP, 3}},
                                          common::scaled_timeout(std::chrono::seconds(5))));
    std::set<vsomeip::client_t> const its_clients{client_one->get_client_id(), client_two->get_client_id()};
    EXPECT_TRUE(selective_clients_->wait_for_any(its_clients))
            << "the forced Subscribe did not address both clients, expected " << client_one->get_client_id() << " and "
            << client_two->get_client_id() << ", recorded: " << selective_clients_->to_string();
}

// Ensure that when a second selective subscription is received while the first is still pending,
// the subscription does not expire when its TTL is reached and the service consumer does not re-subscribe.
// Previously, if this situation happened a child subscription would be created and never deleted or updated.
// Eventually, this subscription would expire and the service consumer would send a Subscribe NACK to the client.
TEST_F(test_selective_subscription, joining_client_while_the_first_is_pending_keeps_the_subscription_alive) {
    prepare_ecus_and_apps();

    auto* client_one = ecu_one_.router_;
    auto* client_two = ecu_one_.apps_[subscriber_name_];
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    // Hold the acceptance of the first subscription until the provider receives the second one.
    // The child subscription is created before the subscribe is answered, so the first client is still
    // pending exactly when the child copies its state.
    auto accept_mutex = std::make_shared<std::mutex>();
    auto held_accept = std::make_shared<std::function<void(bool)>>();
    server->get_application()->register_async_subscription_handler(
            selective_event_.si_.service_, selective_event_.si_.instance_, selective_event_.eventgroup_id_,
            [its_subscribing_clients = subscribing_clients_, accept_mutex,
             held_accept](vsomeip::client_t _client, const vsomeip_sec_client_t*, std::string const&, bool _is_subscribed,
                          std::function<void(bool)> _accept) {
                if (!_is_subscribed) {
                    _accept(true);
                    return;
                }

                its_subscribing_clients->record(_client);

                std::function<void(bool)> its_first_accept;
                {
                    std::scoped_lock its_lock{*accept_mutex};
                    if (!*held_accept) {
                        *held_accept = _accept;
                        return;
                    }
                    its_first_accept = *held_accept;
                }

                its_first_accept(true);
                _accept(true);
            });

    server->offer(iface_);

    client_one->request_service(iface_.instance_);
    client_two->request_service(iface_.instance_);

    client_one->subscribe_selective(selective_event_);
    std::set<vsomeip::client_t> const its_first_client{client_one->get_client_id()};
    ASSERT_TRUE(selective_clients_->wait_for_last(its_first_client))
            << "the first Subscribe carried no selective option, expected client " << client_one->get_client_id()
            << ", recorded: " << selective_clients_->to_string();

    client_two->subscribe_selective(selective_event_);
    std::set<vsomeip::client_t> const its_clients{client_one->get_client_id(), client_two->get_client_id()};
    ASSERT_TRUE(selective_clients_->wait_for_last(its_clients))
            << "both clients were never part of the same subscription, expected " << client_one->get_client_id() << " and "
            << client_two->get_client_id() << ", recorded: " << selective_clients_->to_string();

    // A second notification for a client means its subscription was torn down and re-established.
    EXPECT_FALSE(subscribing_clients_->wait_for_count(client_one->get_client_id(), 2, std::chrono::seconds(4)))
            << "the provider was told about the first client twice, so the subscription expired and was re-established, recorded: "
            << subscribing_clients_->to_string();
}

const interface service_3344_instance_2{0x3344,
                                        {event_spec{0x8001, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                                        {event_spec{0x8002, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                                        0x2};

struct increased_initial_delay_with_multiple_instances : public base_fake_socket_fixture {

    void start_apps() {
        auto udp_traffic = someip_gate::create("udp_traffic");
        ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party(), udp_traffic->get_data_pipe()));
        ecu_one_.add_guest({"guest_client", 0x1338});
        ecu_two_.add_guest({"guest_server", 0x1337});

        ecu_one_.prepare();
        ecu_two_.prepare();

        ecu_one_.start_apps();
        ecu_two_.start_apps();

        client_ = ecu_one_.apps_["guest_client"];
        server_ = ecu_two_.apps_["guest_server"];
    }
    ecu_setup ecu_one_{"ecu_one", boardnet::ecu_one_config, *socket_manager_};
    ecu_setup ecu_two_{"ecu_two",
                       ecu_config{boardnet::ecu_two_config}
                               .with_initial_delay(10000, 10000)
                               .add_interface({interfaces::boardnet::service_3344, service_3344_instance_2}, 30502),
                       *socket_manager_};

    // Default SD multicast group address (VSOMEIP_SD_DEFAULT_MULTICAST); not overridden here.
    boost::asio::ip::udp::endpoint const multicast_ep_{boost::asio::ip::make_address("224.224.224.0"), 30490};

    app* client_{};
    app* server_{};
};

// This test verifies whether vSomeIP properly sents StopOffer messages during the initial_phase_wait or not.
// To ensure we never leave the initial_phase, we configure the initial_delay to 10 seconds.
TEST_F(increased_initial_delay_with_multiple_instances, sends_stop_offer_after_find_triggered_offer) {
    start_apps();
    // Offer and request the service
    server_->offer(interfaces::boardnet::service_3344.instance_);
    client_->request_service(interfaces::boardnet::service_3344.instance_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::available(interfaces::boardnet::service_3344.instance_),
                                                            common::scaled_timeout(std::chrono::seconds(2))))
            << client_->availability_record_;

    // Prepare Service Discovery Gate
    std::shared_ptr<someip_gate> router_one_sd_gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {multicast_ep_, router_one_name_}, router_one_sd_gate->get_data_pipe()));
    router_one_sd_gate->block_at({sd::entry_type_e::OFFER_SERVICE, 0}, 1);

    // Stop offering the service
    server_->stop_offer(interfaces::boardnet::service_3344.instance_);

    // Verify whether a StopService message was blocked or not, if it wasn't, we can safely assume it was not sent either.
    EXPECT_TRUE(router_one_sd_gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(2))));
}

// This test verifies whether vSomeIP properly sents StopOffer messages during the initial_phase_wait for each service-instance.
TEST_F(increased_initial_delay_with_multiple_instances, sends_stop_offer_for_each_service_instance) {
    start_apps();

    const auto first_instance = interfaces::boardnet::service_3344.instance_;
    const auto second_instance = service_3344_instance_2.instance_;

    server_->offer(interfaces::boardnet::service_3344);
    server_->offer(service_3344_instance_2);
    client_->request_service(first_instance);
    client_->request_service(second_instance);
    ASSERT_TRUE(client_->availability_record_.wait_for_any(service_availability::available(first_instance)));
    ASSERT_TRUE(client_->availability_record_.wait_for_any(service_availability::available(second_instance)));

    std::shared_ptr<someip_gate> router_one_sd_gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {multicast_ep_, router_one_name_}, router_one_sd_gate->get_data_pipe()));

    router_one_sd_gate->block_at({sd::entry_type_e::OFFER_SERVICE, 0}, 1);
    server_->stop_offer(first_instance);
    ASSERT_TRUE(router_one_sd_gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(2))));
    router_one_sd_gate->block(false);
    EXPECT_TRUE(client_->availability_record_.wait_for_last(service_availability::unavailable(first_instance)));

    router_one_sd_gate->block_at({sd::entry_type_e::OFFER_SERVICE, 0}, 1);
    server_->stop_offer(second_instance);
    ASSERT_TRUE(router_one_sd_gate->wait_for_blocked(common::scaled_timeout(std::chrono::seconds(2))));
    router_one_sd_gate->block(false);
    EXPECT_TRUE(client_->availability_record_.wait_for_last(service_availability::unavailable(second_instance)));
}

struct sd_header_validation : public base_fake_socket_fixture {
    ecu_setup ecu_one_{"ecu_one", boardnet::ecu_one_config, *socket_manager_};
    ecu_setup ecu_two_{"ecu_two", boardnet::ecu_two_config, *socket_manager_};

    void prepare_ecus_and_apps() {
        ecu_one_.add_guest({"guest_client", 0x1338});
        ecu_two_.add_guest({"guest_server", 0x1337});

        ecu_one_.prepare();
        ecu_two_.prepare();

        ecu_one_.start_apps();
        ecu_two_.start_apps();
    }
};

TEST_F(sd_header_validation, prs_someipsd_00154_sd_offer_with_nonzero_client_id_is_rejected) {
    // SD messages shall have a Client-ID set to 0x0000.
    // Verify that an incoming SD OFFER message carrying a non-zero Client-ID is silently
    // discarded by the receiver (ECU one), while an identical message with Client-ID = 0x0000
    // is accepted and causes normal service availability signalling.

    prepare_ecus_and_apps();

    auto* client = ecu_one_.apps_["guest_client"];
    client->request_service(interfaces::boardnet::service_3344.instance_);

    // construct_offer() builds a well-formed SD OFFER with Client-ID = 0x0000.
    // We then overwrite bytes 8–9 (VSOMEIP_CLIENT_POS_MIN) with a non-zero value to
    // simulate a non-compliant sender.
    auto malformed_offer = construct_offer({interfaces::boardnet::service_3344.instance_, interfaces::boardnet::service_3344.events_[0]},
                                           boardnet::ecu_two_config.unicast_ip_, 30501);
    // SOME/IP header: bytes 8–9 are the Client-ID (big-endian).
    malformed_offer[VSOMEIP_CLIENT_POS_MIN] = 0xDE;
    malformed_offer[VSOMEIP_CLIENT_POS_MIN + 1] = 0xAD;

    send_someip_sd_message(malformed_offer, ecu_two_.sd_endpoint(), ecu_one_.sd_endpoint());

    // ECU one must NOT process the offer; service availability must NOT be reported.
    EXPECT_FALSE(client->availability_record_.wait_for_last(service_availability::available(interfaces::boardnet::service_3344.instance_)))
            << "SD OFFER with non-zero Client-ID (0xDEAD) was incorrectly accepted";
    ;

    // --- Valid offer: Client-ID = 0x0000 ---
    // The identical offer with the correct Client-ID must be accepted and trigger
    // service availability on ECU one.
    auto valid_offer = construct_offer({interfaces::boardnet::service_3344.instance_, interfaces::boardnet::service_3344.events_[0]},
                                       boardnet::ecu_two_config.unicast_ip_, 30501);

    send_someip_sd_message(valid_offer, ecu_two_.sd_endpoint(), ecu_one_.sd_endpoint());

    EXPECT_TRUE(client->availability_record_.wait_for_last(service_availability::available(interfaces::boardnet::service_3344.instance_)))
            << "SD OFFER with Client-ID = 0x0000 was not accepted, recorded: " << client->availability_record_;
}

struct test_sd_acceptance : public base_fake_socket_fixture {

    // Acceptance rules for a single remote device: the optional (never-secure) ranges are dropped.
    // Only the default secure ranges remain subject to SD acceptance_required.
    acceptance_config secure_only_acceptance_for(boost::asio::ip::address const& _address) {
        acceptance_config rule;
        rule.address_ = _address;
        rule.unreliable_ = {acceptance_port_range{.ports_ = std::nullopt, .type_ = port_type::optional}};
        rule.reliable_ = {acceptance_port_range{.ports_ = std::nullopt, .type_ = port_type::optional}};
        return rule;
    }

    interface const secure_iface{0x3348, {event_spec{0x8010, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE}}, {}};

    ecu_config ecu_one_cfg_ = [this] {
        ecu_config cfg{boardnet::ecu_one_config};
        cfg.client_ports_ = client_port_config{.remote_ports_ = {default_ports::secure_server.first_, default_ports::secure_server.last_},
                                               .client_ports_ = {default_ports::secure_client.first_, default_ports::secure_client.last_}};
        cfg.add_secure_service(secure_iface.instance_);
        cfg.add_acceptance(secure_only_acceptance_for(boardnet::ecu_two_config.unicast_ip_));
        return cfg;
    }();

    ecu_config ecu_two_cfg_ = [this] {
        ecu_config cfg{boardnet::ecu_two_config};
        cfg.client_ports_ = client_port_config{.remote_ports_ = {default_ports::secure_server.first_, default_ports::secure_server.last_},
                                               .client_ports_ = {default_ports::secure_client.first_, default_ports::secure_client.last_}};
        cfg.add_secure_service(secure_iface.instance_);
        cfg.add_acceptance(secure_only_acceptance_for(boardnet::ecu_one_config.unicast_ip_));
        return cfg;
    }();

    ecu_setup ecu_one_{"ecu_one", ecu_one_cfg_.add_interface({secure_iface}, default_ports::secure_server.first_), *socket_manager_};
    ecu_setup ecu_two_{"ecu_two", ecu_two_cfg_.add_interface({secure_iface}, default_ports::secure_server.first_), *socket_manager_};

    event_ids udp_event_{secure_iface.instance_, secure_iface.events_[0]};

    std::shared_ptr<someip_gate> ecu_one_sd_send_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> ecu_two_sd_send_gate_ = someip_gate::create();

    void prepare_ecus_and_apps() {
        ecu_one_.prepare();
        ecu_two_.prepare();

        ASSERT_TRUE(
                set_udp_data_pipe({ecu_one_.sd_endpoint(), router_one_name_}, udp_party::any(), ecu_one_sd_send_gate_->get_data_pipe()));
        ASSERT_TRUE(
                set_udp_data_pipe({ecu_two_.sd_endpoint(), router_two_name_}, udp_party::any(), ecu_two_sd_send_gate_->get_data_pipe()));

        ecu_one_.start_apps();
        ecu_two_.start_apps();
    }
};

TEST_F(test_sd_acceptance, expire_offer) {
    // Enabling SD acceptance for a remote device must expire everything that was established with
    // that device on the port ranges its acceptance rules cover. Here the service is offered on a
    // secure port, so switching acceptance on for ecu_one must drop the client's availability.
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* router_two = ecu_two_.router_;

    router_two->offer(secure_iface);
    router_one->request_service(secure_iface.instance_);
    router_one->subscribe_event({udp_event_});
    ASSERT_TRUE(router_one->availability_record_.wait_for_last(service_availability::available(secure_iface.instance_)));

    router_one->availability_record_.clear();
    remote_info_t enable_acceptance{{.address_ = {boardnet::ecu_two_config.unicast_ip_.to_v4().to_bytes()}, .is_v4_ = true},
                                    std::numeric_limits<port_t>::max(),
                                    0,
                                    true,
                                    false};
    // Enable SD acceptance for router_one on router_two. This will not cause expiration, will only enable ipsec rules for secure ports of
    // remote partner.
    router_one->register_sd_acceptance_handler([&](const remote_info_t& _remote) -> bool {
        if (_remote.ip_.is_v4_ && _remote.ip_.address_.v4_ == boardnet::ecu_two_config.unicast_ip_.to_v4().to_bytes()) {
            return false;
        }

        return true;
    });
    // Mimick ipsec plugin allow_offer.
    router_one->set_sd_acceptance_required(enable_acceptance, "", true);

    // Force scenario of an IKE SA toggle, forcing expiration of services and subscriptions.
    enable_acceptance.last_ = std::numeric_limits<port_t>::max();
    router_one->set_sd_acceptance_required(enable_acceptance);
    ASSERT_TRUE(router_one->availability_record_.wait_for_last(service_availability::unavailable(secure_iface.instance_)));
    ASSERT_FALSE(router_one->availability_record_.wait_for_last(service_availability::available(secure_iface.instance_),
                                                                common::scaled_timeout(std::chrono::milliseconds(500))));
}

TEST_F(test_field_resubscribe, multiple_clients_partial_unsubscribe) {
    prepare_ecus_and_apps();

    auto* client_one = ecu_one_.apps_[ecu_one_client_name_];
    auto* client_two = ecu_one_.apps_[ecu_one_server_name_];
    auto* server = ecu_two_.apps_[ecu_two_server_name_];

    server->offer(iface_);
    server->send_event(udp_field_, payload_);

    client_one->request_service(iface_.instance_);
    client_two->request_service(iface_.instance_);

    client_one->subscribe_field({udp_field_});
    client_two->subscribe_field({udp_field_});

    message_checker checker{std::nullopt, iface_.instance_, udp_field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, payload_};
    ASSERT_TRUE(client_one->message_record_.wait_for(checker));
    ASSERT_TRUE(client_two->message_record_.wait_for(checker));

    ecu_one_sd_send_gate_->sd_record_.clear();
    client_one->message_record_.clear();
    client_two->message_record_.clear();

    client_one->release_service(iface_.instance_);

    EXPECT_FALSE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0},
                                                                common::scaled_timeout(std::chrono::milliseconds(100))));

    std::vector<unsigned char> const updated_payload{0x8, 0x9};
    server->send_event(udp_field_, updated_payload);

    message_checker updated_checker{std::nullopt, iface_.instance_, udp_field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION,
                                    updated_payload};
    EXPECT_TRUE(client_two->message_record_.wait_for(updated_checker));
    EXPECT_FALSE(client_one->message_record_.wait_for(updated_checker, common::scaled_timeout(std::chrono::milliseconds(100))));

    client_two->release_service(iface_.instance_);

    EXPECT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP, 0}));
}

TEST_F(test_sd_acceptance, expire_remote_subscription) {
    // Enabling SD acceptance for a remote device must expire everything that was established with
    // that device on the port ranges its acceptance rules cover. Here the service is subscribed on a
    // secure port, so switching acceptance on for ecu_two must drop the remote subscriptions and reject following ones.
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* router_two = ecu_two_.router_;

    router_two->offer(secure_iface);
    router_one->request_service(secure_iface.instance_);
    router_one->subscribe_event({udp_event_});
    ASSERT_TRUE(router_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(udp_event_)));

    router_one->subscription_record_.clear();
    ecu_two_sd_send_gate_->sd_record_.clear();
    remote_info_t enable_acceptance{{.address_ = {boardnet::ecu_one_config.unicast_ip_.to_v4().to_bytes()}, .is_v4_ = true},
                                    std::numeric_limits<port_t>::max(),
                                    0,
                                    true,
                                    false};
    // Enable SD acceptance for router_two on router_one. This will not cause expiration, will only enable ipsec rules for secure ports of
    // remote partner.
    router_two->register_sd_acceptance_handler([&](const remote_info_t& _remote) -> bool {
        if (_remote.ip_.is_v4_ && _remote.ip_.address_.v4_ == boardnet::ecu_one_config.unicast_ip_.to_v4().to_bytes()) {
            return false;
        }

        return true;
    });
    // Mimick ipsec plugin allow_offer.
    router_two->set_sd_acceptance_required(enable_acceptance, "", true);

    // Force scenario of an IKE SA toggle, forcing expiration of services and subscriptions.
    enable_acceptance.last_ = std::numeric_limits<port_t>::max();
    router_two->set_sd_acceptance_required(enable_acceptance);
    ASSERT_FALSE(router_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(udp_event_),
                                                               common::scaled_timeout(std::chrono::milliseconds(500))));
    ASSERT_TRUE(ecu_two_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::STOP_SUBSCRIBE_EVENTGROUP_ACK, 0}));
}

struct duplicate_remote_subscribe : public base_fake_socket_fixture {
    std::vector<event_spec> const fields_{{0x8020, {0x3}, vsomeip::reliability_type_e::RT_UNRELIABLE},
                                          {0x8021, {0x4}, vsomeip::reliability_type_e::RT_UNRELIABLE}};
    interface iface_{0x3349, {}, fields_};

    ecu_config ecu_one_cfg_{boardnet::ecu_one_config};
    ecu_setup ecu_one_{"ecu_one", ecu_one_cfg_.add_interface({iface_}), *socket_manager_};

    event_ids pending_field_{iface_.instance_, iface_.fields_[0]};
    event_ids fresh_field_{iface_.instance_, iface_.fields_[1]};

    std::vector<unsigned char> const field_payload_{0x42, 0x13};

    void prepare_offering_ecu() {
        ecu_one_.add_app(ecu_one_server_name_);
        ecu_one_.prepare();

        ASSERT_TRUE(
                set_udp_data_pipe({ecu_one_.sd_endpoint(), router_one_name_}, udp_party::any(), ecu_one_sd_send_gate_->get_data_pipe()));
        ASSERT_TRUE(
                setup_data_pipe(ecu_one_server_name_, ecu_one_.router_name_, socket_role::server, subscribe_ack_gate_->get_data_pipe()));

        ecu_one_.start_apps();

        ecu_one_.apps_[ecu_one_server_name_]->offer(iface_);
        // The eventgroups only exist once the offer has been processed; subscribing earlier is NACKed.
        ASSERT_TRUE(ecu_one_sd_send_gate_->sd_record_.wait_for_any({sd::entry_type_e::OFFER_SERVICE, 3}))
                << "service was never offered, recorded: " << ecu_one_sd_send_gate_->sd_record_.to_string();
        // Records SUBSCRIBE_EVENTGROUP_ACK entries into acked_eventgroups_ without ever blocking -- reuses the
        // already-registered gate instead of a second set_udp_data_pipe() for the same (sender, receiver).
        ecu_one_sd_send_gate_->block_at(someip_gate::sd_predicate{[this](std::shared_ptr<sd::message_impl> const& _msg) {
            for (auto const& its_entry : _msg->get_entries()) {
                if (its_entry->get_type() == sd::entry_type_e::SUBSCRIBE_EVENTGROUP_ACK && its_entry->get_ttl() > 0) {
                    acked_eventgroups_.record(std::static_pointer_cast<sd::eventgroupentry_impl>(its_entry)->get_eventgroup());
                }
            }
            return false;
        }});

        ASSERT_TRUE(set_udp_data_pipe({boost::asio::ip::udp::endpoint{ecu_one_.address(), 30501}, router_one_name_}, udp_party::any(),
                                      initial_notify_pipe_));

        ecu_one_.apps_[ecu_one_server_name_]->send_event(pending_field_, field_payload_);
        ecu_one_.apps_[ecu_one_server_name_]->send_event(fresh_field_, field_payload_);
    }

    void subscribe_from_peer(std::vector<event_ids> const& _events) {
        send_someip_sd_message(construct_subscription(_events, subscriber_endpoint().address(), subscriber_endpoint().port()),
                               remote_sd_endpoint(), ecu_one_.sd_endpoint());
    }

    /// Eventgroups ecu_one acknowledged on the wire.
    attribute_recorder<vsomeip::eventgroup_t> acked_eventgroups_;
    attribute_recorder<vsomeip::event_t> initial_notifications_;

    std::shared_ptr<data_pipe> initial_notify_pipe_ =
            std::make_shared<data_pipe>(static_cast<data_pipe::external_message_checker_t>([this](someip_message const& _message) {
                if (_message.msg_ && _message.msg_->get_message_type() == vsomeip::message_type_e::MT_NOTIFICATION) {
                    initial_notifications_.record(_message.msg_->get_method());
                }
                return data_pipe_state::OPEN;
            }));

    /// Records the SD entries ecu_one puts on the wire.
    std::shared_ptr<someip_gate> ecu_one_sd_send_gate_ = someip_gate::create();
    /// Withholds the offering app's answer to the remote subscriptions.
    std::shared_ptr<command_gate> subscribe_ack_gate_ = command_gate::create();

    /// Unicast endpoint the injected subscriptions come from.
    boost::asio::ip::udp::endpoint subscriber_endpoint() { return {boardnet::ecu_two_config.unicast_ip_, 30501}; }
    boost::asio::ip::udp::endpoint remote_sd_endpoint() { return {boardnet::ecu_two_config.unicast_ip_, 30490}; }
};

TEST_F(duplicate_remote_subscribe, duplicate_entry_does_not_suppress_the_acknowledgement_of_its_neighbours) {
    // A peer may repeat a SubscribeEventgroup it has not been answered yet, batched in the same SD
    // message as a subscription the offering side has never seen. The repeated entry carries no new
    // client and is merged into the still pending subscription. That must neither cost the message's
    // other entry its SubscribeEventgroupAck, nor make the offering side re-send the merged field's
    // initial event (the still-pending parent already accounts for it).
    prepare_offering_ecu();

    // Hold the app's answers, so the repeated entry is processed while the first one is pending.
    subscribe_ack_gate_->block_at(protocol::id_e::SUBSCRIBE_ACK_ID);

    subscribe_from_peer({pending_field_});
    ASSERT_TRUE(subscribe_ack_gate_->wait_for_blocked()) << "offering app never answered the first remote subscription";

    clear_command_record(ecu_one_server_name_, ecu_one_.router_name_);
    subscribe_from_peer({pending_field_, fresh_field_});
    ASSERT_TRUE(wait_for_command(ecu_one_server_name_, ecu_one_.router_name_, protocol::id_e::SUBSCRIBE_ID, socket_role::client))
            << "the fresh subscription never reached the offering app";

    subscribe_ack_gate_->block(false);

    EXPECT_TRUE(acked_eventgroups_.wait_for_any(fresh_field_.eventgroup_id_))
            << "eventgroup " << std::hex << fresh_field_.eventgroup_id_
            << " was never acknowledged, acknowledged: " << acked_eventgroups_.to_string();

    // Each field's initial value must reach the subscriber exactly once ...
    ASSERT_TRUE(initial_notifications_.wait_for_count(fresh_field_.event_id_, 1))
            << "fresh field initial value was never delivered, seen: " << initial_notifications_.to_string();
    ASSERT_TRUE(initial_notifications_.wait_for_count(pending_field_.event_id_, 1))
            << "pending field initial value was never delivered, seen: " << initial_notifications_.to_string();
    // ... and the merged duplicate must not produce a second one for the repeated field.
    EXPECT_FALSE(initial_notifications_.wait_for_count(pending_field_.event_id_, 2, common::scaled_timeout(std::chrono::milliseconds(500))))
            << "the merged duplicate re-sent the initial event, seen: " << initial_notifications_.to_string();
}
}
