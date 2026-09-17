// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "sample_configurations.hpp"
#include "sample_interfaces.hpp"

#include "helpers/app.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/message_checker.hpp"
#include "helpers/service_state.hpp"
#include "helpers/someip_gate.hpp"

#include <vsomeip/vsomeip.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <vector>

namespace vsomeip_v3::testing {

/*
 * Restart of the routing manager (host) on the providing ECU.
 *
 * Scenario (mirrors initial_event_boardnet_test):
 *   ECU one: client host, requests + subscribes to a reliable (TCP) field and sends a reliable request
 *   ECU two: provider host, offers the service and the field
 *
 * The provider host is stopped without the client ever receiving a StopOffer and is then started
 * again. The client learns about the restart from the broken TCP connection and, later, from the
 * first SD message of the restarted host, whose session id / reboot flag trigger the reboot detection.
 *
 * The client must treat the broken connection as the end of the offer: it must NOT report the
 * service as available again just because its TCP endpoint managed to reconnect to the restarted
 * host's listening socket. Doing so lets the application subscribe and send requests on a
 * connection that the reboot detection will tear down as soon as the first SD message from the
 * restarted host arrives -- and the queued data is lost with it. Only an SD offer from the
 * restarted host may make the service available again.
 */
struct remote_host_restart : public base_fake_socket_fixture {
    // TCP-only interface: one reliable field.
    std::vector<event_spec> const fields_{{0x8002, {0x1}, vsomeip::reliability_type_e::RT_RELIABLE}};
    interface const interface_{0x3366, /*events*/ {}, fields_};
    service_instance const service_{interface_.instance_};
    event_ids const field_{service_, fields_[0]};
    static constexpr vsomeip::method_t method_{0x0421};
    static constexpr vsomeip::port_t tcp_port_{30501};

    ecu_config client_cfg_{boardnet::ecu_one_config};
    ecu_config provider_cfg_ = [this] {
        ecu_config cfg{boardnet::ecu_two_config};
        cfg.add_interface({interface_}, tcp_port_);
        // A long offer TTL: the client may only forget the offer because of the broken connection or
        // the reboot detection, never because the TTL ran out while the provider was restarting.
        cfg.service_discovery_.ttl_ = 60;
        return cfg;
    }();

    ecu_setup client_ecu_{"ecu_one", client_cfg_, *socket_manager_};
    ecu_setup provider_ecu_{"ecu_two", provider_cfg_, *socket_manager_};

    [[nodiscard]] message_checker notification(std::vector<unsigned char> _payload) const {
        return message_checker{std::nullopt, service_, field_.event_id_, vsomeip::message_type_e::MT_NOTIFICATION, std::move(_payload)};
    }

    [[nodiscard]] message_checker received_request() const {
        return message_checker{std::nullopt, service_, method_, vsomeip::message_type_e::MT_REQUEST_NO_RETURN, std::nullopt};
    }

    void send_request(app* _client) {
        _client->send_request(request{service_, method_, vsomeip::message_type_e::MT_REQUEST_NO_RETURN, true, {0x42}});
    }

    // Gate on everything the provider host sends via SD. Installing a new gate replaces the previous
    // one, and whatever the previous gate still held back is discarded with it.
    [[nodiscard]] std::shared_ptr<someip_gate> hold_back_provider_sd() {
        auto gate = someip_gate::create();
        EXPECT_TRUE(set_udp_data_pipe({provider_ecu_.sd_endpoint(), provider_ecu_.router_name_}, udp_party::any(), gate->get_data_pipe()));
        gate->block(true);
        return gate;
    }

    // A SOME/IP header whose length field exceeds the maximum TCP message size. The receiving TCP client
    // endpoint cannot resync on such a stream and restarts its connection.
    [[nodiscard]] std::vector<unsigned char> broken_tcp_message(app* _client) const {
        return construct_someip_raw_message(static_cast<uint16_t>(service_.service_), // service id
                                            static_cast<uint16_t>(method_), // method id
                                            static_cast<uint32_t>(0x04000000), // spoofed length
                                            static_cast<uint16_t>(_client->get_client()), // client id
                                            static_cast<uint16_t>(0x0001), // session id
                                            static_cast<uint8_t>(0x01), // proto version
                                            static_cast<uint8_t>(0x01), // iface version
                                            static_cast<uint8_t>(0x00), // message type
                                            static_cast<uint8_t>(0x00) // return code
        );
    }
};

TEST_F(remote_host_restart, service_is_only_available_again_after_sd_offer_from_restarted_host) {
    client_ecu_.prepare();
    provider_ecu_.prepare();
    client_ecu_.start_router();
    provider_ecu_.start_router();

    auto* client = client_ecu_.router_;
    auto* provider = provider_ecu_.router_;
    ASSERT_NE(client, nullptr);
    ASSERT_NE(provider, nullptr);

    // 1. Regular start: offer, subscribe, initial event, request.
    provider->offer(interface_);
    provider->send_event(field_, {0x01});

    client->request_service(service_);
    ASSERT_TRUE(client->availability_record_.wait_for_last(service_availability::available(service_)));
    client->subscribe_field(field_);
    ASSERT_TRUE(client->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(field_)));
    ASSERT_TRUE(client->message_record_.wait_for(notification({0x01}))) << "Initial event missing\n" << client->message_record_;

    send_request(client);
    ASSERT_TRUE(provider->message_record_.wait_for_any(received_request())) << "Request missing\n" << provider->message_record_;

    // 2. Stop the provider host. Its StopOffer must not reach the client (like a SIGINT that does not
    //    get its SD messages out in time), so the broken TCP connection is the only hint that the
    //    provider is gone.
    auto pre_stop_gate = hold_back_provider_sd();
    client->availability_record_.clear();
    client->subscription_record_.clear();
    client->message_record_.clear();

    provider_ecu_.stop_apps();
    ASSERT_TRUE(client->availability_record_.wait_for_last(service_availability::unavailable(service_)))
            << "Broken connection must make the service unavailable\n"
            << client->availability_record_;

    // 3. Start the provider host again while holding back every SD message it sends. The service is
    //    offered again and its TCP server socket is listening, so a client endpoint that kept
    //    reconnecting on its own would succeed now.
    auto post_restart_gate = hold_back_provider_sd();
    client->availability_record_.clear();

    provider_ecu_.start_router();
    provider = provider_ecu_.router_;
    ASSERT_NE(provider, nullptr);
    provider->offer(interface_);
    provider->send_event(field_, {0x02});
    ASSERT_TRUE(socket_manager_->await_acceptor(boost::asio::ip::tcp::endpoint{provider_ecu_.address(), tcp_port_}))
            << "Restarted provider did not start listening on its TCP service port";

    // 4. Without an SD offer from the restarted host, the service must stay unavailable.
    EXPECT_FALSE(client->availability_record_.wait_for_any(service_availability::available(service_), std::chrono::milliseconds(500)))
            << "Service reported available without any SD message from the restarted host\n"
            << client->availability_record_;

    // 5. Release SD: the first message from the restarted host triggers the reboot detection, the
    //    offer is processed, and the client re-subscribes, receives the initial event and can send
    //    a request. The reboot detection must not cause an availability flap.
    post_restart_gate->block(false);
    ASSERT_TRUE(client->availability_record_.wait_for_last(service_availability::available(service_)))
            << "Service did not become available after the SD offer of the restarted host\n"
            << client->availability_record_;
    ASSERT_TRUE(client->subscription_record_.wait_for_any(event_subscription::successfully_subscribed_to(field_)))
            << "Subscription was not restored\n"
            << client->subscription_record_;
    ASSERT_TRUE(client->message_record_.wait_for(notification({0x02}))) << "Initial event after restart missing\n"
                                                                        << client->message_record_;
    EXPECT_FALSE(client->availability_record_.wait_for_any(service_availability::unavailable(service_), std::chrono::milliseconds(300)))
            << "Reboot detection caused an availability flap\n"
            << client->availability_record_;

    send_request(client);
    ASSERT_TRUE(provider->message_record_.wait_for_any(received_request())) << "Request after restart missing\n"
                                                                            << provider->message_record_;
}

/*
 * Counterpart of the test above for the other reasons a TCP client endpoint reports a disconnect:
 * only a broken established connection means the remote is gone. When the endpoint restarts its
 * connection to resync after broken data, the remote is still there and the offer must survive:
 * the service is unavailable while the endpoint reconnects and available again as soon as it did,
 * without any SD message from the provider.
 */
TEST_F(remote_host_restart, restart_after_broken_data_keeps_the_offer) {
    client_ecu_.prepare();
    provider_ecu_.prepare();
    client_ecu_.start_router();
    provider_ecu_.start_router();

    auto* client = client_ecu_.router_;
    auto* provider = provider_ecu_.router_;
    ASSERT_NE(client, nullptr);
    ASSERT_NE(provider, nullptr);

    provider->offer(interface_);
    client->request_service(service_);
    ASSERT_TRUE(client->availability_record_.wait_for_last(service_availability::available(service_)));
    send_request(client);
    ASSERT_TRUE(provider->message_record_.wait_for_any(received_request())) << "Request missing\n" << provider->message_record_;

    // From here on nothing the provider sends via SD reaches the client: only the client's own TCP
    // endpoint can make the service available again.
    auto sd_gate = hold_back_provider_sd();
    client->availability_record_.clear();
    provider->message_record_.clear();

    // Deliver a message with an oversized length field to the client's TCP socket. Its endpoint
    // restarts the connection, which is not a broken connection: the provider is still listening.
    auto broken = broken_tcp_message(client);
    ASSERT_TRUE(inject_command_tcp(client_ecu_.router_name_, provider_ecu_.router_name_, broken, socket_role::client));

    ASSERT_TRUE(client->availability_record_.wait_for_any(service_availability::unavailable(service_)))
            << "Restarting the connection must make the service unavailable\n"
            << client->availability_record_;
    ASSERT_TRUE(client->availability_record_.wait_for_last(service_availability::available(service_)))
            << "Offer was dropped: service did not become available again on the reconnect alone\n"
            << client->availability_record_;

    send_request(client);
    ASSERT_TRUE(provider->message_record_.wait_for_any(received_request())) << "Request after reconnect missing\n"
                                                                            << provider->message_record_;
    sd_gate->block(false);
}
} // namespace vsomeip_v3::testing
