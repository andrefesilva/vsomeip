// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/attribute_recorder.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/command_message.hpp"
#include "helpers/ecu_config.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/message_checker.hpp"
#include "helpers/service_state.hpp"
#include "helpers/someip_gate.hpp"

#include "../../../implementation/service_discovery/include/entry_impl.hpp"
#include "../../../implementation/service_discovery/include/ipv4_option_impl.hpp"
#include "../../../implementation/service_discovery/include/message_impl.hpp"

#include "sample_configurations.hpp"

#include "common/timeout_scale.hpp"

#include <vsomeip/enumeration_types.hpp>
#include <vsomeip/vsomeip.hpp>

#include <gtest/gtest.h>

#include <boost/asio/error.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

namespace vsomeip_v3::testing {

// One ECU offers both majors of the same (service, instance) from two applications, with the same
// event and eventgroup id. Consumers of each major sit on a second ECU and on the provider ECU.
template<vsomeip::reliability_type_e Reliability, bool Multicast>
struct aggregate_offering_events_fixture : public base_fake_socket_fixture {
    void start_apps() {
        provider_ecu_.add_app(provider_v1_name_);
        provider_ecu_.add_app(provider_v2_name_);
        provider_ecu_.add_app(local_client_v1_name_);
        provider_ecu_.add_app(local_client_v2_name_);
        consumer_ecu_.add_app(client_v1_name_);
        consumer_ecu_.add_app(client_v2_name_);

        provider_ecu_.prepare();
        consumer_ecu_.prepare();

        provider_ecu_.start_apps();
        consumer_ecu_.start_apps();

        provider_v1_ = provider_ecu_.apps_[provider_v1_name_];
        provider_v2_ = provider_ecu_.apps_[provider_v2_name_];
        local_client_v1_ = provider_ecu_.apps_[local_client_v1_name_];
        local_client_v2_ = provider_ecu_.apps_[local_client_v2_name_];
        client_v1_ = consumer_ecu_.apps_[client_v1_name_];
        client_v2_ = consumer_ecu_.apps_[client_v2_name_];

        for (auto* its_app : {provider_v1_, provider_v2_, local_client_v1_, local_client_v2_, client_v1_, client_v2_}) {
            ASSERT_TRUE(its_app->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        }
    }

    // client_v2 on its own ECU, so a single consumer ECU's subscription can be made to expire.
    void start_apps_split() {
        provider_ecu_.add_app(provider_v1_name_);
        provider_ecu_.add_app(provider_v2_name_);
        consumer_ecu_.add_app(client_v1_name_);
        consumer_v2_ecu_.add_app(client_v2_name_);

        provider_ecu_.prepare();
        consumer_ecu_.prepare();
        consumer_v2_ecu_.prepare();

        provider_ecu_.start_apps();
        consumer_ecu_.start_apps();
        consumer_v2_ecu_.start_apps();

        provider_v1_ = provider_ecu_.apps_[provider_v1_name_];
        provider_v2_ = provider_ecu_.apps_[provider_v2_name_];
        client_v1_ = consumer_ecu_.apps_[client_v1_name_];
        client_v2_ = consumer_v2_ecu_.apps_[client_v2_name_];

        for (auto* its_app : {provider_v1_, provider_v2_, client_v1_, client_v2_}) {
            ASSERT_TRUE(its_app->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        }
    }

    void offer_both() {
        record_subscriptions(provider_v1_, field_v1_, subscribed_at_v1_);
        record_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);

        provider_v1_->offer(interface_v1_);
        provider_v2_->offer(interface_v2_);
    }

    void offer_and_subscribe_both_remotely() {
        offer_both();

        client_v1_->request_service(interface_v1_.instance_);
        client_v2_->request_service(interface_v2_.instance_);
        ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
        ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

        client_v1_->subscribe_field(field_v1_);
        client_v2_->subscribe_field(field_v2_);
        ASSERT_TRUE(subscribed_at_v1_.wait_for_any(true)) << "provider_v1 never saw client_v1's subscription";
        ASSERT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw client_v2's subscription";
    }

    void subscribe_both_locally() {
        local_client_v1_->request_service(interface_v1_.instance_);
        local_client_v2_->request_service(interface_v2_.instance_);
        ASSERT_TRUE(local_client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
        ASSERT_TRUE(local_client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

        local_client_v1_->subscribe_field(field_v1_);
        local_client_v2_->subscribe_field(field_v2_);
        ASSERT_TRUE(local_client_v1_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v1_)))
                << local_client_v1_->subscription_record_;
        ASSERT_TRUE(local_client_v2_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v2_)))
                << local_client_v2_->subscription_record_;
    }

    void record_subscriptions(app* _provider, event_ids const& _ei, attribute_recorder<bool>& _record) {
        _provider->register_group_subscription_handler(_ei, [&_record](client_t, uid_t, gid_t, std::string const&, bool _is_subscribed) {
            _record.record(_is_subscribed);
            return true;
        });
    }

    // Records the major of every _id command exchanged between _app and its router.
    std::shared_ptr<attribute_recorder<int>> record_command_majors(ecu_setup& _ecu, std::string const& _app, protocol::id_e _id) {
        auto its_majors = std::make_shared<attribute_recorder<int>>();
        auto const its_handler = [_id, its_majors](command_message const& _msg) {
            if (_msg.header_.id_ == _id) {
                if (auto const* its_data = _msg.payload_.get_if<protocol::subscribe_data>(); its_data) {
                    its_majors->record(its_data->service_key_.major);
                } else if (auto const* its_answer = _msg.payload_.get_if<protocol::subscribe_answer_data>(); its_answer) {
                    its_majors->record(its_answer->service_key_.major);
                }
            }
            return false;
        };
        set_custom_command_handler(_ecu.router_name_, _app, its_handler);
        set_custom_command_handler(_app, _ecu.router_name_, its_handler);
        return its_majors;
    }

    static void expect_only_major(attribute_recorder<int>& _majors, int _major) {
        EXPECT_TRUE(_majors.wait_for_any(_major)) << _majors.to_string();
        EXPECT_FALSE(_majors.wait_for_any(
                [_major](std::vector<int> const& _all) {
                    return std::any_of(_all.begin(), _all.end(), [_major](int _m) { return _m != _major; });
                },
                std::chrono::milliseconds(0)))
                << _majors.to_string();
    }

    message_checker notification(service_instance const& _si, std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, _si, shared_event_, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    // Matches _payload's notification whatever major it is labelled with.
    message_checker any_notification(std::vector<unsigned char> const& _payload) const {
        return message_checker{std::nullopt, std::nullopt, shared_event_, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const local_client_v1_name_{"local_client_v1"};
    std::string const local_client_v2_name_{"local_client_v2"};
    std::string const client_v1_name_{"client_v1"};
    std::string const client_v2_name_{"client_v2"};

    static constexpr vsomeip::event_t shared_event_{0x8001};
    static constexpr vsomeip::eventgroup_t shared_eventgroup_{0x8001};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {},
                            {event_spec{shared_event_, {shared_eventgroup_}, Reliability}}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {},
                            {event_spec{shared_event_, {shared_eventgroup_}, Reliability}}};

    event_ids field_v1_{interface_v1_.instance_, interface_v1_.fields_[0]};
    event_ids field_v2_{interface_v2_.instance_, interface_v2_.fields_[0]};

    std::vector<unsigned char> const payload_v1_{0x11, 0x11};
    std::vector<unsigned char> const payload_v2_{0x22, 0x22};

    // Declared before the ecu_setups: the subscription handlers hold a reference.
    attribute_recorder<bool> subscribed_at_v1_;
    attribute_recorder<bool> subscribed_at_v2_;

    // The port and multicast configuration is version independent: one entry serves both majors.
    ecu_config provider_config() const {
        auto its_config = ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_});
        if constexpr (Multicast) {
            its_config.with_eventgroup_multicast(interface_v1_.instance_, shared_eventgroup_, "239.0.0.1", 30600);
        }
        return its_config;
    }

    ecu_setup provider_ecu_{"provider", provider_config(), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};
    // Only used by start_apps_split(): hosts client_v2 so each major has its own consumer ECU.
    ecu_setup consumer_v2_ecu_{"consumer_v2", boardnet::ecu_two_config, *socket_manager_};

    app* provider_v1_;
    app* provider_v2_;
    app* local_client_v1_;
    app* local_client_v2_;
    app* client_v1_;
    app* client_v2_;
};

struct test_multiversion_aggregate_offering_events
    : public aggregate_offering_events_fixture<vsomeip::reliability_type_e::RT_RELIABLE, false> { };
struct test_multiversion_aggregate_offering_udp_events
    : public aggregate_offering_events_fixture<vsomeip::reliability_type_e::RT_UNRELIABLE, false> { };
struct test_multiversion_aggregate_offering_multicast_events
    : public aggregate_offering_events_fixture<vsomeip::reliability_type_e::RT_UNRELIABLE, true> { };

TEST_F(test_multiversion_aggregate_offering_events, remote_subscriptions_of_both_majors_reach_their_own_provider) {
    start_apps();
    offer_and_subscribe_both_remotely();
}

TEST_F(test_multiversion_aggregate_offering_events, remote_notification_from_v1_reaches_only_client_v1) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v1_->send_event(field_v1_, payload_v1_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_last(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
    EXPECT_FALSE(
            client_v2_->message_record_.wait_for_any(any_notification(payload_v1_), common::scaled_timeout(std::chrono::milliseconds(250))))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, remote_notification_from_v2_reaches_only_client_v2) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v2_->send_event(field_v2_, payload_v2_);

    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
    EXPECT_FALSE(
            client_v1_->message_record_.wait_for_any(any_notification(payload_v2_), common::scaled_timeout(std::chrono::milliseconds(250))))
            << client_v1_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, each_remote_major_gets_its_own_initial_field_value) {
    start_apps();
    offer_both();
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

TEST_F(test_multiversion_aggregate_offering_events, unsubscribing_remotely_from_v1_leaves_v2_subscribed) {
    start_apps();
    offer_and_subscribe_both_remotely();

    subscribed_at_v1_.clear();
    client_v1_->unsubscribe_from_group(field_v1_);
    EXPECT_TRUE(subscribed_at_v1_.wait_for_last(false)) << "provider_v1 never saw the StopSubscribe";

    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, stopping_v1_offer_keeps_remote_v2_notifications_flowing) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, stopping_v2_offer_keeps_remote_v1_notifications_flowing) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v2_->stop_offer(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));

    client_v1_->message_record_.clear();
    provider_v1_->send_event(field_v1_, payload_v1_);
    EXPECT_TRUE(client_v1_->message_record_.wait_for_last(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, reoffering_v1_resubscribes_remote_v1) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    subscribed_at_v1_.clear();
    provider_v1_->offer(interface_v1_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(subscribed_at_v1_.wait_for_any(true)) << "client_v1 did not resubscribe after the re-offer";

    client_v1_->message_record_.clear();
    provider_v1_->send_event(field_v1_, payload_v1_, true);
    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, both_majors_resubscribe_after_suspend_resume) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));

    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    client_v1_->message_record_.clear();
    client_v2_->message_record_.clear();
    provider_v1_->send_event(field_v1_, payload_v1_, true);
    provider_v2_->send_event(field_v2_, payload_v2_, true);
    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, local_notification_of_each_major_reaches_only_its_own_local_client) {
    start_apps();
    offer_both();
    subscribe_both_locally();

    provider_v1_->send_event(field_v1_, payload_v1_);
    provider_v2_->send_event(field_v2_, payload_v2_);

    EXPECT_TRUE(local_client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << local_client_v1_->message_record_;
    EXPECT_TRUE(local_client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << local_client_v2_->message_record_;
    EXPECT_FALSE(local_client_v1_->message_record_.wait_for_any(any_notification(payload_v2_),
                                                                common::scaled_timeout(std::chrono::milliseconds(250))))
            << local_client_v1_->message_record_;
    EXPECT_FALSE(local_client_v2_->message_record_.wait_for_any(any_notification(payload_v1_),
                                                                common::scaled_timeout(std::chrono::milliseconds(250))))
            << local_client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, each_local_major_gets_its_own_initial_field_value) {
    start_apps();
    offer_both();
    provider_v1_->send_event(field_v1_, payload_v1_);
    provider_v2_->send_event(field_v2_, payload_v2_);

    subscribe_both_locally();

    EXPECT_TRUE(local_client_v1_->message_record_.wait_for_last(notification(interface_v1_.instance_, payload_v1_)))
            << local_client_v1_->message_record_;
    EXPECT_TRUE(local_client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << local_client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, local_and_remote_consumers_of_both_majors_are_served) {
    start_apps();
    offer_and_subscribe_both_remotely();
    subscribe_both_locally();

    provider_v1_->send_event(field_v1_, payload_v1_);
    provider_v2_->send_event(field_v2_, payload_v2_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
    EXPECT_TRUE(local_client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << local_client_v1_->message_record_;
    EXPECT_TRUE(local_client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << local_client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, stopping_v1_offer_keeps_local_v2_notifications_flowing) {
    start_apps();
    offer_both();
    subscribe_both_locally();

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(local_client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    local_client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(local_client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << local_client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, stopping_the_v1_provider_keeps_v2_notifications_flowing) {
    start_apps();
    offer_and_subscribe_both_remotely();
    subscribe_both_locally();

    provider_ecu_.stop_one(provider_v1_name_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    client_v2_->message_record_.clear();
    local_client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
    EXPECT_TRUE(local_client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << local_client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, losing_the_v1_provider_connection_keeps_v2_notifications_flowing) {
    start_apps();
    offer_and_subscribe_both_remotely();

    ASSERT_TRUE(disconnect(provider_v1_name_, boost::asio::error::connection_reset, provider_ecu_.router_name_,
                           boost::asio::error::connection_reset));

    client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_events, an_expired_v1_subscription_reaches_only_v1s_provider) {
    start_apps_split();
    offer_and_subscribe_both_remotely();

    auto const its_gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {provider_ecu_.sd_endpoint()},
                                  its_gate->get_data_pipe()));
    its_gate->block(true);

    subscribed_at_v2_.clear();
    EXPECT_TRUE(subscribed_at_v1_.wait_for_last(false, common::scaled_timeout(std::chrono::seconds(10))))
            << "provider_v1 was not told about the expiry";
    EXPECT_FALSE(subscribed_at_v2_.wait_for_any(false, common::scaled_timeout(std::chrono::milliseconds(250))))
            << "provider_v2 was told about v1's expiry";

    client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
    its_gate->block(false);
}

TEST_F(test_multiversion_aggregate_offering_events, losing_the_v1_consumer_ecu_connection_keeps_v2_subscribed) {
    start_apps_split();
    offer_and_subscribe_both_remotely();

    subscribed_at_v2_.clear();
    ASSERT_TRUE(disconnect(consumer_ecu_.router_name_, boost::asio::error::connection_reset, provider_ecu_.router_name_,
                           boost::asio::error::connection_reset));

    EXPECT_FALSE(subscribed_at_v2_.wait_for_any(false, common::scaled_timeout(std::chrono::milliseconds(500))))
            << "provider_v2 was told about v1's consumer going away";
    client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_udp_events, remote_subscriptions_of_both_majors_reach_their_own_provider) {
    start_apps();
    offer_and_subscribe_both_remotely();
}

TEST_F(test_multiversion_aggregate_offering_udp_events, remote_notification_of_each_major_reaches_only_its_own_client) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v1_->send_event(field_v1_, payload_v1_);
    provider_v2_->send_event(field_v2_, payload_v2_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
    EXPECT_FALSE(
            client_v1_->message_record_.wait_for_any(any_notification(payload_v2_), common::scaled_timeout(std::chrono::milliseconds(250))))
            << client_v1_->message_record_;
    EXPECT_FALSE(
            client_v2_->message_record_.wait_for_any(any_notification(payload_v1_), common::scaled_timeout(std::chrono::milliseconds(250))))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_udp_events, each_remote_major_gets_its_own_initial_field_value) {
    start_apps();
    offer_both();
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

TEST_F(test_multiversion_aggregate_offering_udp_events, stopping_v1_offer_keeps_remote_v2_notifications_flowing) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_multicast_events, remote_subscriptions_of_both_majors_reach_their_own_provider) {
    start_apps();
    offer_and_subscribe_both_remotely();
}

TEST_F(test_multiversion_aggregate_offering_multicast_events, remote_notification_of_each_major_reaches_only_its_own_client) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v1_->send_event(field_v1_, payload_v1_);
    provider_v2_->send_event(field_v2_, payload_v2_);

    EXPECT_TRUE(client_v1_->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
            << client_v1_->message_record_;
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
    EXPECT_FALSE(
            client_v1_->message_record_.wait_for_any(any_notification(payload_v2_), common::scaled_timeout(std::chrono::milliseconds(250))))
            << client_v1_->message_record_;
    EXPECT_FALSE(
            client_v2_->message_record_.wait_for_any(any_notification(payload_v1_), common::scaled_timeout(std::chrono::milliseconds(250))))
            << client_v2_->message_record_;
}

TEST_F(test_multiversion_aggregate_offering_multicast_events, each_remote_major_gets_its_own_initial_field_value) {
    start_apps();
    offer_both();
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

TEST_F(test_multiversion_aggregate_offering_multicast_events, stopping_v1_offer_keeps_remote_v2_notifications_flowing) {
    start_apps();
    offer_and_subscribe_both_remotely();

    provider_v1_->stop_offer(interface_v1_.instance_);
    ASSERT_TRUE(client_v1_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));

    client_v2_->message_record_.clear();
    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_last(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

// Both majors share the configured group. Delivery does not depend on the membership, so the group
// is checked directly: tearing down one major keeps it, tearing down the other one leaves it.
struct test_multiversion_aggregate_offering_multicast_group : public test_multiversion_aggregate_offering_multicast_events {
    boost::asio::ip::address const group_{boost::asio::ip::make_address("239.0.0.1")};

    void tear_down_in_order(std::function<void(app*, app*, event_ids const&, service_instance const&)> const& _tear_down, bool _v1_first) {
        start_apps();
        offer_and_subscribe_both_remotely();
        // Each major's SubscribeAck records its use of the group; the first one alone already joins it.
        for (auto const& [its_client, its_field] : {std::pair{client_v1_, field_v1_}, std::pair{client_v2_, field_v2_}}) {
            ASSERT_TRUE(its_client->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(its_field)))
                    << its_client->subscription_record_;
        }
        ASSERT_TRUE(await_multicast_join(group_));

        auto* const its_first_provider = _v1_first ? provider_v1_ : provider_v2_;
        auto* const its_first_client = _v1_first ? client_v1_ : client_v2_;
        auto* const its_second_provider = _v1_first ? provider_v2_ : provider_v1_;
        auto* const its_second_client = _v1_first ? client_v2_ : client_v1_;
        auto const& its_first_field = _v1_first ? field_v1_ : field_v2_;
        auto const& its_second_field = _v1_first ? field_v2_ : field_v1_;
        auto const& its_first_si = _v1_first ? interface_v1_.instance_ : interface_v2_.instance_;
        auto const& its_second_si = _v1_first ? interface_v2_.instance_ : interface_v1_.instance_;

        _tear_down(its_first_provider, its_first_client, its_first_field, its_first_si);
        EXPECT_FALSE(await_multicast_leave(group_, 0, common::scaled_timeout(std::chrono::milliseconds(500))))
                << "the shared group was left while the other major still uses it";

        _tear_down(its_second_provider, its_second_client, its_second_field, its_second_si);
        EXPECT_TRUE(await_multicast_leave(group_)) << "the shared group was never left";
    }

    static void stop_offer(app* _provider, app* _client, event_ids const&, service_instance const& _si) {
        _provider->stop_offer(_si);
        ASSERT_TRUE(_client->availability_record_.wait_for_last(service_availability::unavailable(_si)));
    }

    static void unsubscribe(app*, app* _client, event_ids const& _field, service_instance const&) {
        _client->unsubscribe_from_group(_field);
    }
};

TEST_F(test_multiversion_aggregate_offering_multicast_group, stopping_v1_then_v2_leaves_the_shared_group_only_at_the_end) {
    tear_down_in_order(stop_offer, true);
}

TEST_F(test_multiversion_aggregate_offering_multicast_group, stopping_v2_then_v1_leaves_the_shared_group_only_at_the_end) {
    tear_down_in_order(stop_offer, false);
}

TEST_F(test_multiversion_aggregate_offering_multicast_group, unsubscribing_v1_then_v2_leaves_the_shared_group_only_at_the_end) {
    tear_down_in_order(unsubscribe, true);
}

TEST_F(test_multiversion_aggregate_offering_multicast_group, unsubscribing_v2_then_v1_leaves_the_shared_group_only_at_the_end) {
    tear_down_in_order(unsubscribe, false);
}

TEST_F(test_multiversion_aggregate_offering_events, subscribing_before_the_offers_delivers_each_majors_initial_value) {
    start_apps();
    record_subscriptions(provider_v1_, field_v1_, subscribed_at_v1_);
    record_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);

    for (auto* its_client : {client_v1_, local_client_v1_}) {
        its_client->request_service(interface_v1_.instance_);
        its_client->subscribe_field(field_v1_);
    }
    for (auto* its_client : {client_v2_, local_client_v2_}) {
        its_client->request_service(interface_v2_.instance_);
        its_client->subscribe_field(field_v2_);
    }

    provider_v1_->offer(interface_v1_);
    provider_v2_->offer(interface_v2_);
    provider_v1_->send_event(field_v1_, payload_v1_);
    provider_v2_->send_event(field_v2_, payload_v2_);

    for (auto* its_client : {client_v1_, local_client_v1_}) {
        EXPECT_TRUE(its_client->message_record_.wait_for_any(notification(interface_v1_.instance_, payload_v1_)))
                << its_client->message_record_;
    }
    for (auto* its_client : {client_v2_, local_client_v2_}) {
        EXPECT_TRUE(its_client->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
                << its_client->message_record_;
    }
}

// notify_one() of one major must not reach a client subscribed only to the other major.
TEST_F(test_multiversion_aggregate_offering_events, notify_one_of_v1_does_not_reach_a_v2_subscriber) {
    start_apps();
    offer_both();
    subscribe_both_locally();

    provider_v1_->send_event_one(field_v1_, payload_v1_, local_client_v2_->get_client_id());
    provider_v2_->send_event_one(field_v2_, payload_v2_, local_client_v2_->get_client_id());

    EXPECT_TRUE(local_client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << local_client_v2_->message_record_;
    EXPECT_FALSE(local_client_v2_->message_record_.wait_for_any(any_notification(payload_v1_),
                                                                common::scaled_timeout(std::chrono::milliseconds(250))))
            << local_client_v2_->message_record_;
}

// get_offered_service()/is_tcp_connected() must look up the subscribed major: v1 is not offered here.
TEST_F(test_multiversion_aggregate_offering_events, a_remote_subscription_to_v2_is_acked_while_only_v2_is_offered) {
    start_apps();
    record_subscriptions(provider_v2_, field_v2_, subscribed_at_v2_);
    provider_v2_->offer(interface_v2_);

    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    client_v2_->subscribe_field(field_v2_);
    ASSERT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw the subscription";

    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(notification(interface_v2_.instance_, payload_v2_)))
            << client_v2_->message_record_;
}

// check_sent_offers() must count the own offer of v2; otherwise the offer watchdog terminates the router.
TEST_F(test_multiversion_aggregate_offering_events, resuming_an_ecu_offering_only_v2_does_not_trip_the_offer_watchdog) {
    start_apps();
    provider_v2_->offer(interface_v2_);
    client_v2_->request_service(interface_v2_.instance_);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));
    provider_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                                common::scaled_timeout(std::chrono::seconds(3))));
}

TEST_F(test_multiversion_aggregate_offering_events, a_remote_unsubscribe_reaches_the_provider_with_its_major) {
    auto const its_majors = record_command_majors(provider_ecu_, provider_v2_name_, protocol::id_e::UNSUBSCRIBE_ID);
    start_apps();
    offer_and_subscribe_both_remotely();

    subscribed_at_v2_.clear();
    client_v2_->unsubscribe_from_group(field_v2_);
    ASSERT_TRUE(subscribed_at_v2_.wait_for_last(false)) << "provider_v2 never saw the StopSubscribe";

    expect_only_major(*its_majors, interface_v2_.instance_.major_);
}

TEST_F(test_multiversion_aggregate_offering_events, an_expired_subscription_reaches_the_provider_with_its_major) {
    auto const its_majors = record_command_majors(provider_ecu_, provider_v1_name_, protocol::id_e::EXPIRE_ID);
    start_apps_split();
    offer_and_subscribe_both_remotely();

    auto const its_gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {provider_ecu_.sd_endpoint()},
                                  its_gate->get_data_pipe()));
    its_gate->block(true);
    ASSERT_TRUE(subscribed_at_v1_.wait_for_last(false, common::scaled_timeout(std::chrono::seconds(10))))
            << "provider_v1 was not told about the expiry";

    expect_only_major(*its_majors, interface_v1_.instance_.major_);
    its_gate->block(false);
}

// First subscriber: acknowledged once the remote provider acks (on_subscribe_ack).
TEST_F(test_multiversion_aggregate_offering_events, the_subscribe_ack_to_a_remote_majors_subscriber_carries_its_major) {
    auto const its_majors = record_command_majors(consumer_ecu_, client_v2_name_, protocol::id_e::SUBSCRIBE_ACK_ID);
    start_apps();
    offer_and_subscribe_both_remotely();
    ASSERT_TRUE(client_v2_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v2_)))
            << client_v2_->subscription_record_;

    expect_only_major(*its_majors, interface_v2_.instance_.major_);
}

// Second subscriber: acknowledged from remote_subscription_state_ (handle_subscription_state).
TEST_F(test_multiversion_aggregate_offering_events, the_subscribe_ack_to_a_later_subscriber_of_an_acked_major_carries_its_major) {
    std::string const its_second_name{"client_v2_second"};
    consumer_ecu_.add_app(its_second_name);
    auto const its_majors = record_command_majors(consumer_ecu_, its_second_name, protocol::id_e::SUBSCRIBE_ACK_ID);
    start_apps();
    offer_and_subscribe_both_remotely();
    ASSERT_TRUE(client_v2_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v2_)))
            << client_v2_->subscription_record_;

    auto* its_second = consumer_ecu_.apps_[its_second_name];
    ASSERT_TRUE(its_second->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    its_second->request_service(interface_v2_.instance_);
    ASSERT_TRUE(its_second->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    its_second->subscribe_field(field_v2_);
    ASSERT_TRUE(its_second->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(field_v2_)))
            << its_second->subscription_record_;

    expect_only_major(*its_majors, interface_v2_.instance_.major_);
}

// The "mixed" ECU offers v2 and consumes v1 from another ECU, so it knows a remote v1 next to its
// local v2 and any lookup that does not use the major resolves to the remote v1. The eventgroup holds
// a reliable field and an unreliable event, so subscriptions carry a TCP and a UDP option.
struct test_multiversion_aggregate_offering_mixed_ecu : public base_fake_socket_fixture {
    void start_apps() {
        mixed_ecu_.add_app(provider_v2_name_);
        mixed_ecu_.add_app(v1_consumer_name_);
        remote_v1_ecu_.add_app(provider_v1_name_);
        consumer_ecu_.add_app(client_v2_name_);

        for (auto* its_ecu : {&mixed_ecu_, &remote_v1_ecu_, &consumer_ecu_}) {
            its_ecu->prepare();
        }
        for (auto* its_ecu : {&mixed_ecu_, &remote_v1_ecu_, &consumer_ecu_}) {
            its_ecu->start_apps();
        }

        provider_v2_ = mixed_ecu_.apps_[provider_v2_name_];
        v1_consumer_ = mixed_ecu_.apps_[v1_consumer_name_];
        provider_v1_ = remote_v1_ecu_.apps_[provider_v1_name_];
        client_v2_ = consumer_ecu_.apps_[client_v2_name_];

        for (auto* its_app : {provider_v2_, v1_consumer_, provider_v1_, client_v2_}) {
            ASSERT_TRUE(its_app->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        }
    }

    // Offers v2 locally, makes the remote v1 known on the mixed ECU, then makes v2 available remotely.
    void offer_and_request() {
        provider_v2_->offer(interface_v2_);
        provider_v1_->offer(interface_v1_);

        v1_consumer_->request_service(interface_v1_.instance_);
        ASSERT_TRUE(v1_consumer_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));

        client_v2_->request_service(interface_v2_.instance_);
        ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const v1_consumer_name_{"v1_consumer"};
    std::string const client_v2_name_{"client_v2"};

    static constexpr vsomeip::event_t shared_event_{0x8001};
    static constexpr vsomeip::eventgroup_t shared_eventgroup_{0x8001};

    static constexpr vsomeip::event_t udp_event_{0x8002};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {event_spec{udp_event_, {shared_eventgroup_}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                            {event_spec{shared_event_, {shared_eventgroup_}, vsomeip::reliability_type_e::RT_RELIABLE}}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {event_spec{udp_event_, {shared_eventgroup_}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                            {event_spec{shared_event_, {shared_eventgroup_}, vsomeip::reliability_type_e::RT_RELIABLE}}};
    event_ids field_v2_{interface_v2_.instance_, interface_v2_.fields_[0]};
    event_ids udp_event_v2_{interface_v2_.instance_, interface_v2_.events_[0]};
    std::vector<unsigned char> const payload_v2_{0x22, 0x22};

    // Subscribes client_v2 to both events of the eventgroup, so the subscription is RT_BOTH.
    void subscribe_v2() {
        client_v2_->request_event(udp_event_v2_, vsomeip::event_type_e::ET_EVENT);
        client_v2_->subscribe_field(field_v2_);
    }

    vsomeip::method_t const method_{0x1111};
    request request_v1_{interface_v1_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {}};
    request request_v2_{interface_v2_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {}};

    attribute_recorder<bool> subscribed_at_v2_;

    ecu_setup mixed_ecu_{"mixed", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup remote_v1_ecu_{"remote_v1", ecu_config{boardnet::ecu_two_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_three_config, *socket_manager_};

    app* provider_v1_;
    app* provider_v2_;
    app* v1_consumer_;
    app* client_v2_;
};

TEST_F(test_multiversion_aggregate_offering_mixed_ecu, a_remote_subscription_to_the_local_major_is_acked) {
    start_apps();
    provider_v2_->register_group_subscription_handler(field_v2_, [this](client_t, uid_t, gid_t, std::string const&, bool _is_subscribed) {
        subscribed_at_v2_.record(_is_subscribed);
        return true;
    });
    offer_and_request();

    subscribe_v2();
    ASSERT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw the subscription";

    provider_v2_->send_event(field_v2_, payload_v2_);
    EXPECT_TRUE(client_v2_->message_record_.wait_for_any(
            message_checker{std::nullopt, interface_v2_.instance_, shared_event_, vsomeip::message_type_e::MT_NOTIFICATION, payload_v2_}))
            << client_v2_->message_record_;
}

// is_local must reflect the major of each message: v2 is offered here, v1 is not.
TEST_F(test_multiversion_aggregate_offering_mixed_ecu, the_message_acceptance_handler_sees_each_majors_locality) {
    auto const its_seen = std::make_shared<attribute_recorder<std::pair<uint32_t, bool>>>();
    start_apps();
    mixed_ecu_.router_->get_application()->register_message_acceptance_handler([its_seen](message_acceptance_t const& _info) {
        if (_info.service_ == 0x1000) {
            its_seen->record({_info.remote_address_, _info.is_local_});
        }
        return true;
    });

    std::vector<unsigned char> const its_response_v1{0x01};
    std::vector<unsigned char> const its_response_v2{0x02};
    provider_v1_->answer_request(request_v1_, [its_response_v1] { return its_response_v1; });
    provider_v2_->answer_request(request_v2_, [its_response_v2] { return its_response_v2; });
    offer_and_request();

    v1_consumer_->send_request(request_v1_);
    client_v2_->send_request(request_v2_);
    ASSERT_TRUE(v1_consumer_->wait_for_messages({its_response_v1}));
    ASSERT_TRUE(client_v2_->wait_for_messages({its_response_v2}));

    auto const its_remote_v1 = remote_v1_ecu_.sd_endpoint().address().to_v4().to_uint();
    auto const its_consumer = consumer_ecu_.sd_endpoint().address().to_v4().to_uint();
    EXPECT_TRUE(its_seen->wait_for_any(std::make_pair(its_consumer, true))) << "the v2 request was not seen as local";
    EXPECT_TRUE(its_seen->wait_for_any(std::make_pair(its_remote_v1, false))) << "the v1 response was not seen as remote";
    EXPECT_FALSE(its_seen->wait_for_any(
            [&](auto const& _all) {
                return std::any_of(_all.begin(), _all.end(), [&](auto const& _seen) {
                    return (_seen.first == its_consumer && !_seen.second) || (_seen.first == its_remote_v1 && _seen.second);
                });
            },
            std::chrono::milliseconds(0)));
}

// check_sent_offers() must count the own v2 offer even though the remote v1 is known.
TEST_F(test_multiversion_aggregate_offering_mixed_ecu, resuming_does_not_trip_the_offer_watchdog) {
    // Longer than remote_v1's cyclic offer delay: v1 is known again before the first own offer after resume.
    mixed_ecu_.config().with_initial_delay(1500, 1500);
    start_apps();
    offer_and_request();

    mixed_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_SUSPENDED);
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_)));
    ASSERT_TRUE(v1_consumer_->availability_record_.wait_for_last(service_availability::unavailable(interface_v1_.instance_)));
    mixed_ecu_.router_->set_routing_state(vsomeip::routing_state_e::RS_RESUMED);

    // Precondition: v1 is known again before the first own v2 offer, which makes v2 available remotely.
    ASSERT_TRUE(v1_consumer_->availability_record_.wait_for_last(service_availability::available(interface_v1_.instance_)));
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                               std::chrono::milliseconds(0)))
            << "v2 was offered before v1 was known again";
    ASSERT_TRUE(client_v2_->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));

    EXPECT_FALSE(client_v2_->availability_record_.wait_for_last(service_availability::unavailable(interface_v2_.instance_),
                                                                common::scaled_timeout(std::chrono::seconds(6))));
}

// SubscribeEventgroup with its IPv4 endpoint options in the given order (true = TCP).
static std::vector<unsigned char> construct_subscription(vsomeip::session_t _session, service_instance const& _si,
                                                         vsomeip::eventgroup_t _eventgroup, vsomeip::ipv4_address_t const& _address,
                                                         std::vector<std::pair<bool, vsomeip::port_t>> const& _options) {
    std::vector<unsigned char> its_sd{0xC0, 0x00, 0x00, 0x00}; // reboot + unicast flags
    auto const push16 = [&its_sd](uint16_t _v) {
        its_sd.push_back(static_cast<unsigned char>(_v >> 8));
        its_sd.push_back(static_cast<unsigned char>(_v));
    };
    auto const push32 = [&push16](uint32_t _v) {
        push16(static_cast<uint16_t>(_v >> 16));
        push16(static_cast<uint16_t>(_v));
    };
    push32(16); // entries length
    its_sd.insert(its_sd.end(), {0x06, 0x00, 0x00, static_cast<unsigned char>(_options.size() << 4)});
    push16(_si.service_);
    push16(_si.instance_);
    its_sd.insert(its_sd.end(), {_si.major_, 0x00, 0x00, 0x03}); // major, ttl = 3
    push16(0x0000); // reserved, flags, counter
    push16(_eventgroup);
    push32(static_cast<uint32_t>(12 * _options.size())); // options length
    for (auto const& [its_reliable, its_port] : _options) {
        push16(0x0009);
        its_sd.insert(its_sd.end(), {0x04, 0x00});
        its_sd.insert(its_sd.end(), _address.begin(), _address.end());
        its_sd.insert(its_sd.end(), {0x00, static_cast<unsigned char>(its_reliable ? 0x06 : 0x11)});
        push16(its_port);
    }

    std::vector<unsigned char> its_message{0xFF, 0xFF, 0x81, 0x00};
    auto const its_length = static_cast<uint32_t>(8 + its_sd.size());
    for (int i = 3; i >= 0; --i) {
        its_message.push_back(static_cast<unsigned char>(its_length >> (8 * i)));
    }
    its_message.insert(
            its_message.end(),
            {0x00, 0x00, static_cast<unsigned char>(_session >> 8), static_cast<unsigned char>(_session), 0x01, 0x01, 0x02, 0x00});
    its_message.insert(its_message.end(), its_sd.begin(), its_sd.end());
    return its_message;
}

// handle_eventgroup_subscription() checks the TCP connection for whichever option carries it, and the
// option order of a vsomeip subscription is not fixed (message_impl::add_entry_data orders by address).
TEST_F(test_multiversion_aggregate_offering_mixed_ecu, a_subscription_is_acked_only_with_a_connected_tcp_endpoint_in_either_option) {
    struct captured_subscription {
        std::mutex mutex_;
        std::optional<vsomeip::ipv4_address_t> address_;
        std::optional<vsomeip::port_t> tcp_port_;
        std::optional<vsomeip::port_t> udp_port_;
        vsomeip::session_t session_{0};
    };
    auto const its_captured = std::make_shared<captured_subscription>();
    auto const its_complete = std::make_shared<attribute_recorder<bool>>();

    start_apps();
    provider_v2_->register_group_subscription_handler(field_v2_, [this](client_t, uid_t, gid_t, std::string const&, bool _is_subscribed) {
        subscribed_at_v2_.record(_is_subscribed);
        return true;
    });
    auto const to_mixed = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({consumer_ecu_.sd_endpoint(), consumer_ecu_.router_name_}, {mixed_ecu_.sd_endpoint()},
                                  to_mixed->get_data_pipe()));
    to_mixed->block_at(someip_gate::sd_predicate{[its_captured, its_complete](std::shared_ptr<sd::message_impl> const& _message) {
        std::scoped_lock its_lock{its_captured->mutex_};
        its_captured->session_ = std::max(its_captured->session_, _message->get_session());
        for (auto const& its_entry : _message->get_entries()) {
            if (its_entry->get_type() != sd::entry_type_e::SUBSCRIBE_EVENTGROUP || its_entry->get_major_version() != 0x2) {
                continue;
            }
            for (uint8_t its_run : {uint8_t{1}, uint8_t{2}}) {
                for (auto its_index : its_entry->get_options(its_run)) {
                    if (auto its_option = std::dynamic_pointer_cast<sd::ipv4_option_impl>(_message->get_option(its_index)); its_option) {
                        its_captured->address_ = its_option->get_address();
                        if (its_option->get_layer_four_protocol() == sd::layer_four_protocol_e::TCP) {
                            its_captured->tcp_port_ = its_option->get_port();
                        } else {
                            its_captured->udp_port_ = its_option->get_port();
                        }
                    }
                }
            }
        }
        if (its_captured->tcp_port_ && its_captured->udp_port_) {
            its_complete->record(true);
        }
        return false;
    }});
    auto const from_mixed = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({mixed_ecu_.sd_endpoint(), mixed_ecu_.router_name_}, {consumer_ecu_.sd_endpoint()},
                                  from_mixed->get_data_pipe()));

    offer_and_request();
    subscribe_v2();
    ASSERT_TRUE(subscribed_at_v2_.wait_for_any(true)) << "provider_v2 never saw the subscription";
    ASSERT_TRUE(its_complete->wait_for_any(true)) << "no subscription with a TCP and a UDP option was captured";

    auto const answered = [](std::optional<bool> _acked) {
        return [_acked](std::vector<someip_sd_record_message> const& _all) {
            return std::any_of(_all.begin(), _all.end(), [_acked](auto const& _m) {
                return _m.id_ == sd::entry_type_e::SUBSCRIBE_EVENTGROUP_ACK && (!_acked || (_m.ttl_ > 0) == *_acked);
            });
        };
    };
    // The genuine subscription's answer must not be mistaken for an injected one's.
    ASSERT_TRUE(from_mixed->sd_record_.wait_for_any(answered(true))) << from_mixed->sd_record_.to_string();

    // Only the injected subscriptions reach the mixed ECU from here on.
    to_mixed->block(true);
    std::unique_lock its_lock{its_captured->mutex_};
    auto its_session = its_captured->session_;
    auto const its_address = *its_captured->address_;
    auto const its_tcp_port = *its_captured->tcp_port_;
    auto const its_udp_port = *its_captured->udp_port_;
    its_lock.unlock();
    // A TCP port without a connection must be NACKed: the check really runs for either option order.
    auto const its_unconnected_tcp_port = static_cast<vsomeip::port_t>(its_tcp_port + 1000);
    for (bool its_connected : {true, false}) {
        for (bool its_tcp_first : {true, false}) {
            std::vector<std::pair<bool, vsomeip::port_t>> its_options{{true, its_connected ? its_tcp_port : its_unconnected_tcp_port},
                                                                      {false, its_udp_port}};
            if (!its_tcp_first) {
                std::swap(its_options[0], its_options[1]);
            }
            from_mixed->sd_record_.clear();
            send_someip_sd_message(
                    construct_subscription(++its_session, interface_v2_.instance_, shared_eventgroup_, its_address, its_options),
                    consumer_ecu_.sd_endpoint(), mixed_ecu_.sd_endpoint());
            ASSERT_TRUE(from_mixed->sd_record_.wait_for_any(answered(std::nullopt)))
                    << "connected: " << its_connected << ", tcp first: " << its_tcp_first << " " << from_mixed->sd_record_.to_string();
            // Each injection is answered exactly once; wait out the answer before judging it.
            EXPECT_FALSE(
                    from_mixed->sd_record_.wait_for_any(answered(!its_connected), common::scaled_timeout(std::chrono::milliseconds(250))))
                    << "connected: " << its_connected << ", tcp first: " << its_tcp_first << " " << from_mixed->sd_record_.to_string();
            EXPECT_TRUE(from_mixed->sd_record_.wait_for_any(answered(its_connected), std::chrono::milliseconds(0)))
                    << "connected: " << its_connected << ", tcp first: " << its_tcp_first << " " << from_mixed->sd_record_.to_string();
        }
    }
    to_mixed->block(false);
}

// The mixed ECU only consumes v1, so a remote subscription to v1 must be NACKed even though the v1
// eventgroup is known here. That eventgroup provides no events, so a UDP option alone passes the
// reliability check.
TEST_F(test_multiversion_aggregate_offering_mixed_ecu, a_remote_subscription_to_the_consumed_major_is_nacked) {
    start_apps();
    auto const from_mixed = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({mixed_ecu_.sd_endpoint(), mixed_ecu_.router_name_}, {consumer_ecu_.sd_endpoint()},
                                  from_mixed->get_data_pipe()));
    offer_and_request();

    // Creates the v1 eventgroup on the mixed ECU; without it the subscription is NACKed as unknown eventgroup.
    event_ids const its_field_v1{interface_v1_.instance_, interface_v1_.fields_[0]};
    v1_consumer_->request_event(event_ids{interface_v1_.instance_, interface_v1_.events_[0]}, vsomeip::event_type_e::ET_EVENT);
    v1_consumer_->subscribe_field(its_field_v1);
    ASSERT_TRUE(v1_consumer_->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(its_field_v1)))
            << v1_consumer_->subscription_record_;

    auto const its_address = consumer_ecu_.sd_endpoint().address().to_v4().to_bytes();
    send_someip_sd_message(construct_subscription(1, interface_v1_.instance_, shared_eventgroup_, its_address, {{false, 40000}}),
                           consumer_ecu_.sd_endpoint(), mixed_ecu_.sd_endpoint());
    EXPECT_TRUE(from_mixed->sd_record_.wait_for_any([](std::vector<someip_sd_record_message> const& _all) {
        return std::any_of(_all.begin(), _all.end(),
                           [](auto const& _m) { return _m.id_ == sd::entry_type_e::SUBSCRIBE_EVENTGROUP_ACK && _m.ttl_ == 0; });
    })) << from_mixed->sd_record_.to_string();
}
// The "low" ECU offers v1 and consumes v2 from another ECU: a lookup that ignores the major resolves to
// the local v1, so a v2 response would look like one for a locally offered service.
struct test_multiversion_aggregate_offering_low_major_ecu : public base_fake_socket_fixture {
    void start_apps() {
        low_ecu_.add_app(provider_v1_name_);
        low_ecu_.add_app(keeper_name_);
        low_ecu_.add_app(consumer_name_);
        remote_v2_ecu_.add_app(provider_v2_name_);

        for (auto* its_ecu : {&low_ecu_, &remote_v2_ecu_}) {
            its_ecu->prepare();
        }
        for (auto* its_ecu : {&low_ecu_, &remote_v2_ecu_}) {
            its_ecu->start_apps();
        }

        provider_v1_ = low_ecu_.apps_[provider_v1_name_];
        keeper_ = low_ecu_.apps_[keeper_name_];
        consumer_ = low_ecu_.apps_[consumer_name_];
        provider_v2_ = remote_v2_ecu_.apps_[provider_v2_name_];

        for (auto* its_app : {provider_v1_, keeper_, consumer_, provider_v2_}) {
            ASSERT_TRUE(its_app->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        }
    }

    // v1 offered here, v2 offered remotely and requested by the keeper, which keeps it referenced.
    void offer_and_request() {
        provider_v1_->offer(interface_v1_);
        provider_v2_->offer(interface_v2_);
        provider_v2_->answer_request(request_v2_, [this] { return response_v2_; });

        for (auto* its_client : {keeper_, consumer_}) {
            its_client->request_service(interface_v2_.instance_);
            ASSERT_TRUE(its_client->availability_record_.wait_for_last(service_availability::available(interface_v2_.instance_)));
        }
    }

    message_checker response() const {
        return message_checker{std::nullopt, interface_v2_.instance_, method_, vsomeip::message_type_e::MT_RESPONSE, response_v2_};
    }

    std::string const provider_v1_name_{"provider_v1"};
    std::string const provider_v2_name_{"provider_v2"};
    std::string const keeper_name_{"keeper"};
    std::string const consumer_name_{"consumer"};

    interface interface_v1_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                            {event_spec{0x8002, {0x8001}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                            {}};
    interface interface_v2_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x2, .minor_ = 0x0},
                            {event_spec{0x8002, {0x8001}, vsomeip::reliability_type_e::RT_UNRELIABLE}},
                            {}};

    vsomeip::method_t const method_{0x1111};
    request request_v2_{interface_v2_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {0x01}};
    std::vector<unsigned char> const response_v2_{0x24};

    ecu_setup low_ecu_{"low", ecu_config{boardnet::ecu_one_config}.add_interface({interface_v1_}), *socket_manager_};
    ecu_setup remote_v2_ecu_{"remote_v2", ecu_config{boardnet::ecu_two_config}.add_interface({interface_v1_}), *socket_manager_};

    app* provider_v1_;
    app* provider_v2_;
    app* keeper_;
    app* consumer_;
};

// Responses for clients that no longer request the remote service are dropped; the local v1
// must not exempt a response of the remote v2.
TEST_F(test_multiversion_aggregate_offering_low_major_ecu, a_remote_majors_response_after_release_is_dropped) {
    start_apps();
    offer_and_request();

    // Hold v2's response where it leaves the remote ECU's UDP service socket.
    boost::asio::ip::udp::endpoint const its_v2_socket{boardnet::ecu_two_config.unicast_ip_, 30501};
    auto const its_gate = someip_gate::create();
    ASSERT_TRUE(set_udp_data_pipe({its_v2_socket, remote_v2_ecu_.router_name_}, udp_party::any(), its_gate->get_data_pipe()));
    its_gate->block_at({.service_ = interface_v2_.instance_.service_, .method_ = method_, .type_ = vsomeip::message_type_e::MT_RESPONSE});

    consumer_->send_request(request_v2_);
    ASSERT_TRUE(its_gate->wait_for_blocked()) << "the response was not held at the provider";

    consumer_->release_service(interface_v2_.instance_);
    its_gate->block(false);

    EXPECT_FALSE(consumer_->message_record_.wait_for_any(response(), common::scaled_timeout(std::chrono::milliseconds(500))))
            << consumer_->message_record_;

    // Control: a requester still gets its response.
    keeper_->send_request(request_v2_);
    EXPECT_TRUE(keeper_->message_record_.wait_for_any(response())) << keeper_->message_record_;
}
}
