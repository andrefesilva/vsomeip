// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "sample_configurations.hpp"

#include "helpers/app.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/message_checker.hpp"
#include "helpers/someip_gate.hpp"

#include "common/timeout_scale.hpp"

#include <vsomeip/vsomeip.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace vsomeip_v3::testing {

std::string const router_one_name_ = "router_one";
std::string const router_two_name_ = "router_two";

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

TEST_F(test_sd_acceptance, expiration_interleaved_with_offers) {
    // An SD acceptance expiration racing with offer processing / re-subscription could leave the service
    // half-registered, so the next toggle would not expire it. Every toggle must expire and then recover.
    // Fast re-offers to interleave expirations with offer processing.
    ecu_two_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::milliseconds(5);
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* router_two = ecu_two_.router_;

    router_two->offer(secure_iface);
    router_one->request_service(secure_iface.instance_);
    router_one->subscribe_event({udp_event_});
    ASSERT_TRUE(router_one->availability_record_.wait_for_last(service_availability::available(secure_iface.instance_)));

    remote_info_t enable_acceptance{{.address_ = {boardnet::ecu_two_config.unicast_ip_.to_v4().to_bytes()}, .is_v4_ = true},
                                    std::numeric_limits<port_t>::max(),
                                    0,
                                    true,
                                    false};
    // Accept everything: offers received after an expiration must re-establish the service.
    router_one->register_sd_acceptance_handler([](const remote_info_t&) -> bool { return true; });
    // Mimick ipsec plugin allow_offer.
    router_one->set_sd_acceptance_required(enable_acceptance, "", true);

    // IKE SA toggle, forcing expiration of services and subscriptions.
    remote_info_t toggle_acceptance{enable_acceptance};
    toggle_acceptance.last_ = std::numeric_limits<port_t>::max();

    for (int i = 0; i < 500; ++i) {
        router_one->availability_record_.clear();
        router_one->subscription_record_.clear();
        router_one->set_sd_acceptance_required(toggle_acceptance);
        // The next offer may re-establish the service right after the expiration, so only require that the
        // expiration was observed at all, and that the service and its subscription recover afterwards.
        ASSERT_TRUE(router_one->availability_record_.wait_for_any(service_availability::unavailable(secure_iface.instance_)))
                << "iteration " << i;
        ASSERT_TRUE(router_one->availability_record_.wait_for_last(service_availability::available(secure_iface.instance_)))
                << "iteration " << i;
        ASSERT_TRUE(router_one->subscription_record_.wait_for_last(event_subscription::successfully_subscribed_to(udp_event_)))
                << "iteration " << i;
    }
}

struct test_sd_acceptance_shared_endpoint : public base_fake_socket_fixture {
    // Both services are offered on the same remote port, so router_one reaches them through one shared client endpoint.
    interface const first_iface{0x3348, {event_spec{0x8010, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE}}, {}};
    interface const second_iface{0x3349, {event_spec{0x8010, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE}}, {}};

    ecu_config ecu_one_cfg_ = make_ecu_config(boardnet::ecu_one_config, boardnet::ecu_two_config.unicast_ip_);
    ecu_config ecu_two_cfg_ = make_ecu_config(boardnet::ecu_two_config, boardnet::ecu_one_config.unicast_ip_);

    ecu_setup ecu_one_{"ecu_one", ecu_one_cfg_, *socket_manager_};
    ecu_setup ecu_two_{"ecu_two", ecu_two_cfg_, *socket_manager_};

    event_ids first_event_{first_iface.instance_, first_iface.events_[0]};
    event_ids second_event_{second_iface.instance_, second_iface.events_[0]};

    std::shared_ptr<someip_gate> ecu_one_sd_send_gate_ = someip_gate::create();
    std::shared_ptr<someip_gate> ecu_two_sd_send_gate_ = someip_gate::create();

    ecu_config make_ecu_config(ecu_config _base, boost::asio::ip::address const& _remote) {
        _base.client_ports_ =
                client_port_config{.remote_ports_ = {default_ports::secure_server.first_, default_ports::secure_server.last_},
                                   .client_ports_ = {default_ports::secure_client.first_, default_ports::secure_client.last_}};
        _base.add_secure_service(first_iface.instance_);
        _base.add_secure_service(second_iface.instance_);
        // As in test_sd_acceptance: only the default secure ranges remain subject to SD acceptance_required.
        acceptance_config its_rule;
        its_rule.address_ = _remote;
        its_rule.unreliable_ = {acceptance_port_range{.ports_ = std::nullopt, .type_ = port_type::optional}};
        its_rule.reliable_ = {acceptance_port_range{.ports_ = std::nullopt, .type_ = port_type::optional}};
        _base.add_acceptance(its_rule);
        _base.add_interface({first_iface}, default_ports::secure_server.first_);
        _base.add_interface({second_iface}, default_ports::secure_server.first_);
        return _base;
    }

    /// Notifies the event from ecu_two until ecu_one receives it. A re-subscription may briefly be reported as
    /// acknowledged before the remote side holds it, as a SubscribeAck still in flight for the expired subscription
    /// can not be told apart from one for the new subscription. The remote side must hold it eventually though.
    [[nodiscard]] bool is_delivered(event_ids const& _event, std::vector<unsigned char> const& _payload) {
        message_checker const its_checker{std::nullopt, _event.si_, _event.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, _payload};
        auto const its_deadline = std::chrono::steady_clock::now() + common::scaled_timeout(std::chrono::seconds(3));
        do {
            ecu_two_.router_->send_event(_event, _payload);
            if (ecu_one_.router_->message_record_.wait_for(its_checker, common::scaled_timeout(std::chrono::milliseconds(50)))) {
                return true;
            }
        } while (std::chrono::steady_clock::now() < its_deadline);
        return false;
    }

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

TEST_F(test_sd_acceptance_shared_endpoint, expiration_keeps_remote_subscriptions_of_shared_endpoint) {
    // Two services behind the same remote port share one client endpoint. Every toggle expires both, and the fast
    // re-offers race with the cleanup of the expired subscriptions. The StopSubscribes of the expired port must never
    // stop a subscription that was already re-established, so the remote side has to keep delivering the events of
    // both services after every recovery -- the local subscription state alone would not show that.
    ecu_two_.config().service_discovery_.cyclic_offer_delay_ = std::chrono::milliseconds(5);
    prepare_ecus_and_apps();

    auto* router_one = ecu_one_.router_;
    auto* router_two = ecu_two_.router_;

    router_two->offer(first_iface);
    router_two->offer(second_iface);
    router_one->request_service(first_iface.instance_);
    router_one->request_service(second_iface.instance_);
    router_one->subscribe_event({first_event_});
    router_one->subscribe_event({second_event_});
    ASSERT_TRUE(router_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(first_event_)));
    ASSERT_TRUE(router_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(second_event_)));

    remote_info_t enable_acceptance{{.address_ = {boardnet::ecu_two_config.unicast_ip_.to_v4().to_bytes()}, .is_v4_ = true},
                                    std::numeric_limits<port_t>::max(),
                                    0,
                                    true,
                                    false};
    router_one->register_sd_acceptance_handler([](const remote_info_t&) -> bool { return true; });
    // Mimick ipsec plugin allow_offer.
    router_one->set_sd_acceptance_required(enable_acceptance, "", true);

    // IKE SA toggle, forcing expiration of services and subscriptions.
    remote_info_t toggle_acceptance{enable_acceptance};
    toggle_acceptance.last_ = std::numeric_limits<port_t>::max();

    for (int i = 0; i < 250; ++i) {
        router_one->availability_record_.clear();
        router_one->subscription_record_.clear();
        router_one->message_record_.clear();
        router_one->set_sd_acceptance_required(toggle_acceptance);
        ASSERT_TRUE(router_one->availability_record_.wait_for_any(service_availability::unavailable(first_iface.instance_)))
                << "iteration " << i;
        ASSERT_TRUE(router_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(first_event_)))
                << "iteration " << i;
        ASSERT_TRUE(router_one->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(second_event_)))
                << "iteration " << i;

        // The remote side must still hold both subscriptions.
        std::vector<unsigned char> const its_payload{static_cast<unsigned char>(i >> 8), static_cast<unsigned char>(i)};
        ASSERT_TRUE(is_delivered(first_event_, its_payload)) << "iteration " << i;
        ASSERT_TRUE(is_delivered(second_event_, its_payload)) << "iteration " << i;
    }
}
}
