// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/attribute_recorder.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/ecu_config.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/message_checker.hpp"
#include "helpers/service_state.hpp"
#include "helpers/someip_gate.hpp"

#include "sample_configurations.hpp"

#include "common/timeout_scale.hpp"

#include <vsomeip/vsomeip.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace vsomeip_v3::testing {

struct test_multicast_eventgroup : public base_fake_socket_fixture {
    void start_apps() {
        provider_ecu_.add_app(provider_name_);
        consumer_ecu_.add_app(client_a_name_);
        consumer_ecu_.add_app(client_b_name_);

        provider_ecu_.prepare();
        consumer_ecu_.prepare();

        provider_ecu_.start_apps();
        consumer_ecu_.start_apps();

        provider_ = provider_ecu_.apps_[provider_name_];
        client_a_ = consumer_ecu_.apps_[client_a_name_];
        client_b_ = consumer_ecu_.apps_[client_b_name_];

        ASSERT_TRUE(provider_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_a_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_b_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    // Offered once and with long TTLs: a later offer resubscribes and would re-join the group.
    static ecu_config quiet_sd(ecu_config _config) {
        _config.service_discovery_.repetitions_max_ = 0;
        _config.service_discovery_.cyclic_offer_delay_ = std::chrono::seconds(30);
        _config.service_discovery_.ttl_ = 60;
        return _config;
    }

    message_checker notification(std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, interface_.instance_, event_id_, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    std::string const provider_name_{"provider"};
    std::string const client_a_name_{"client_a"};
    std::string const client_b_name_{"client_b"};

    static constexpr vsomeip::event_t event_id_{0x8001};
    static constexpr vsomeip::eventgroup_t eventgroup_id_{0x8001};

    interface interface_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                         {event_spec{event_id_, {eventgroup_id_}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                         {}};
    event_ids event_{interface_.instance_, interface_.events_[0]};

    boost::asio::ip::address const group_{boost::asio::ip::make_address("239.0.0.1")};
    boost::asio::ip::udp::endpoint const sd_multicast_{boost::asio::ip::make_address("224.224.224.0"), 30490};

    ecu_setup provider_ecu_{"provider",
                            quiet_sd(ecu_config{boardnet::ecu_one_config}
                                             .add_interface({interface_})
                                             .with_eventgroup_multicast(interface_.instance_, eventgroup_id_, "239.0.0.1", 30600)),
                            *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", quiet_sd(boardnet::ecu_three_config), *socket_manager_};

    app* provider_{};
    app* client_a_{};
    app* client_b_{};
};

// An unsubscribe of an eventgroup the client is no longer subscribed to reaches the router as
// ANY_MAJOR and must not tear down the multicast endpoint another subscriber still uses.
TEST_F(test_multicast_eventgroup, a_redundant_unsubscribe_keeps_the_other_subscribers_multicast) {
    start_apps();
    auto const offers = someip_gate::create("offers");
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_, consumer_ecu_.router_name_}, offers->get_data_pipe()));

    provider_->offer(interface_);
    ASSERT_TRUE(offers->sd_record_.wait_for_any({sd::entry_type_e::OFFER_SERVICE, 60}));

    for (auto* client : {client_a_, client_b_}) {
        client->request_service(interface_.instance_);
        ASSERT_TRUE(client->availability_record_.wait_for_last(service_availability::available(interface_.instance_)));
        client->subscribe_event(event_);
    }
    // An event has no cached value: a subscriber registered after the send would miss it.
    for (auto* client : {client_a_, client_b_}) {
        ASSERT_TRUE(client->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(event_)))
                << client->subscription_record_;
    }
    ASSERT_TRUE(await_multicast_join(group_));
    std::vector<unsigned char> const first{0x01};
    provider_->send_event(event_, first);
    ASSERT_TRUE(client_b_->message_record_.wait_for_any(notification(first))) << client_b_->message_record_;

    client_a_->unsubscribe_from_group(event_);
    client_a_->unsubscribe_from_group(event_);

    EXPECT_FALSE(await_multicast_leave(group_, 0, common::scaled_timeout(std::chrono::milliseconds(500))));
    std::vector<unsigned char> const second{0x02};
    provider_->send_event(event_, second);
    EXPECT_TRUE(client_b_->message_record_.wait_for_any(notification(second))) << client_b_->message_record_;
}

struct test_multicast_eventgroup_two_majors : public base_fake_socket_fixture {
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

    // Subscribes v1 before v2, so an (s,i)-keyed multicast record would hold v2's group.
    void offer_and_subscribe_both() {
        auto const offers = someip_gate::create("offers");
        ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {sd_multicast_, consumer_ecu_.router_name_}, offers->get_data_pipe()));
        provider_v1_->offer(interface_v1_);
        provider_v2_->offer(interface_v2_);
        ASSERT_TRUE(offers->sd_record_.wait_for_count({sd::entry_type_e::OFFER_SERVICE, 60}, 2));

        client_v1_->request_service(interface_v1_.instance_);
        client_v2_->request_service(interface_v2_.instance_);
        ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
        ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

        client_v1_->subscribe_event(event_v1_);
        ASSERT_TRUE(await_multicast_join(group_v1_));
        client_v2_->subscribe_event(event_v2_);
        ASSERT_TRUE(await_multicast_join(group_v2_));

        provider_v2_->send_event(event_v2_, first_);
        ASSERT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, first_))) << client_v2_->message_record_;
    }

    void expect_v2_multicast_kept() {
        EXPECT_FALSE(await_multicast_leave(group_v2_, 0, common::scaled_timeout(std::chrono::milliseconds(500))));
        provider_v2_->send_event(event_v2_, second_);
        EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, second_)))
                << client_v2_->message_record_;
    }

    static interface make_interface(vsomeip::major_version_t _major) {
        return interface{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = _major, .minor_ = 0x0},
                         {event_spec{event_id_, {eventgroup_id_}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                         {}};
    }

    message_checker notification(service_instance const& _si, std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, _si, event_id_, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};

    static constexpr vsomeip::event_t event_id_{0x8001};
    static constexpr vsomeip::eventgroup_t eventgroup_id_{0x8001};

    interface interface_v1_{make_interface(0x1)};
    interface interface_v2_{make_interface(0x2)};
    event_ids event_v1_{interface_v1_.instance_, interface_v1_.events_[0]};
    event_ids event_v2_{interface_v2_.instance_, interface_v2_.events_[0]};

    boost::asio::ip::address const group_v1_{boost::asio::ip::make_address("239.0.0.1")};
    boost::asio::ip::address const group_v2_{boost::asio::ip::make_address("239.0.0.2")};
    boost::asio::ip::udp::endpoint const sd_multicast_{boost::asio::ip::make_address("224.224.224.0"), 30490};
    std::vector<unsigned char> const first_{0x21};
    std::vector<unsigned char> const second_{0x22};

    ecu_setup provider_v1_ecu_{"provider_v1",
                               test_multicast_eventgroup::quiet_sd(
                                       ecu_config{boardnet::ecu_one_config}
                                               .add_interface({interface_v1_})
                                               .with_eventgroup_multicast(interface_v1_.instance_, eventgroup_id_, "239.0.0.1", 30600)),
                               *socket_manager_};
    ecu_setup provider_v2_ecu_{"provider_v2",
                               test_multicast_eventgroup::quiet_sd(
                                       ecu_config{boardnet::ecu_two_config}
                                               .add_interface({interface_v2_})
                                               .with_eventgroup_multicast(interface_v2_.instance_, eventgroup_id_, "239.0.0.2", 30601)),
                               *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", test_multicast_eventgroup::quiet_sd(boardnet::ecu_three_config), *socket_manager_};

    app* provider_v1_{};
    app* provider_v2_{};
    app* client_v1_{};
    app* client_v2_{};
};

// v1's last unsubscribe leaves v1's multicast group only.
TEST_F(test_multicast_eventgroup_two_majors, unsubscribing_v1_keeps_v2_multicast) {
    start_apps();
    offer_and_subscribe_both();

    client_v1_->unsubscribe_from_group(event_v1_);

    EXPECT_TRUE(await_multicast_leave(group_v1_)) << "v1's multicast group was never left";
    expect_v2_multicast_kept();
}

// v1's stop offer leaves v1's multicast group only.
TEST_F(test_multicast_eventgroup_two_majors, stopping_v1_keeps_v2_multicast) {
    start_apps();
    offer_and_subscribe_both();

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    EXPECT_TRUE(await_multicast_leave(group_v1_)) << "v1's multicast group was never left";
    expect_v2_multicast_kept();
}
}
