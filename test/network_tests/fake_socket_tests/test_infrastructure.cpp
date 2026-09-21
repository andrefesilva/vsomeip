// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/service_state.hpp"
#include "helpers/someip_gate.hpp"

#include "sample_configurations.hpp"

#include "common/timeout_scale.hpp"

#include <boost/asio/ip/address.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace vsomeip_v3::testing {
namespace {
std::string const provider_name_ = "provider";
std::string const consumer_name_ = "consumer";
} // namespace

/**
 * Exercises socket_manager's udp_pipe_rules_ routing directly (not vsomeip application
 * behavior): registers every valid rule shape -- including more than one rule per shape, to
 * prove within-stage chaining, not just cross-stage ordering -- and asserts the exact order a
 * single datagram passes through them. Doubles as living documentation of that ordering.
 */
struct test_infrastructure : public base_fake_socket_fixture {
    std::vector<event_spec> const fields_{{0x8001, {0x1}, vsomeip::reliability_type_e::RT_UNRELIABLE}};
    interface iface_{0x4001, {}, fields_};

    ecu_config provider_cfg_{boardnet::ecu_one_config};
    ecu_config consumer_cfg_{boardnet::ecu_two_config};

    ecu_setup provider_ecu_{"provider_ecu", provider_cfg_.add_interface({iface_}), *socket_manager_};
    ecu_setup consumer_ecu_{"consumer_ecu", consumer_cfg_.add_interface({iface_}), *socket_manager_};

    boost::asio::ip::udp::endpoint const multicast_ep_{boost::asio::ip::make_address("224.224.224.0"), 30490};

    void prepare_and_start() {
        provider_ecu_.add_app(provider_name_);
        consumer_ecu_.add_app(consumer_name_);
        provider_ecu_.prepare();
        consumer_ecu_.prepare();
        provider_ecu_.start_apps();
        consumer_ecu_.start_apps();
        // Gates are keyed off live sockets/membership, so both routers must have joined the SD
        // multicast group before any rule is registered.
        ASSERT_TRUE(await_multicast_join(multicast_ep_.address(), 2));
    }
};

TEST_F(test_infrastructure, multicast_gates_apply_in_the_documented_order) {
    prepare_and_start();

    // Every valid multicast rule shape, including two distinct rules per stage to prove
    // within-stage chaining. A rule's stage is determined entirely by receiver_.app_name_:
    // unset -> "towards the group" (stage 1 if sender is also any(), stage 2 if sender is
    // constrained); set -> "per member" (stage 3, sender must be any() -- real multicast has
    // no point-to-point channel to narrow a per-recipient receiver by sender, so any pairing
    // of a constrained sender with a receiver naming one member is rejected/dead, not just the
    // ones pinning receiver_.endpoint_ to the literal multicast address too).
    auto stage1 = someip_gate::create("stage1_any_any");
    auto stage2a = someip_gate::create("stage2_sender_scoped_a");
    auto stage2b = someip_gate::create("stage2_sender_scoped_b");
    auto stage3a = someip_gate::create("stage3_member_a");
    auto stage3b = someip_gate::create("stage3_member_b");

    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party::any(), stage1->get_data_pipe()));

    // Two distinct sender shapes (app_name_ only vs endpoint_+app_name_), receiver stays any()
    // throughout -- receiver_.app_name_ must stay unset for these to remain stage 2 at all.
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(provider_ecu_.router_name_), udp_party::any(), stage2a->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe({provider_ecu_.sd_endpoint(), provider_ecu_.router_name_}, udp_party::any(), stage2b->get_data_pipe()));

    // Two distinct receiver endpoint_ shapes (unset vs pinned to the group address), sender
    // stays any() throughout -- a constrained sender here would be dead (see comment above).
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {std::nullopt, consumer_ecu_.router_name_}, stage3a->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {multicast_ep_, consumer_ecu_.router_name_}, stage3b->get_data_pipe()));

    // Arm every gate on the first SD message it sees; the cyclic offer must only ever advance one
    // gate at a time, in exactly this order, as each is released in turn.
    someip_gate::sd_predicate const any_sd_message{[](auto const&) { return true; }};
    stage1->block_at(any_sd_message);
    stage2a->block_at(any_sd_message);
    stage2b->block_at(any_sd_message);
    stage3a->block_at(any_sd_message);
    stage3b->block_at(any_sd_message);

    provider_ecu_.apps_[provider_name_]->offer(iface_);

    ASSERT_TRUE(stage1->wait_for_blocked()) << "stage 1 (any()+any()) never saw the cyclic offer";
    stage1->block(false);

    ASSERT_TRUE(stage2a->wait_for_blocked()) << "stage 2, rule A (sender-scoped, app_name_ only) never saw the cyclic offer";
    stage2a->block(false);
    ASSERT_TRUE(stage2b->wait_for_blocked()) << "stage 2, rule B (sender-scoped, endpoint_+app_name_) was never chained in";
    stage2b->block(false);

    ASSERT_TRUE(stage3a->wait_for_blocked()) << "stage 3, rule A (per member, receiver endpoint_ unset) never saw the fanned-out offer";
    stage3a->block(false);
    ASSERT_TRUE(stage3b->wait_for_blocked()) << "stage 3, rule B (per member, receiver endpoint_ pinned) was never chained in";
    stage3b->block(false);
}

TEST_F(test_infrastructure, peer_to_peer_gates_apply_in_the_documented_order) {
    prepare_and_start();

    service_instance const si{iface_.instance_};
    request const req{si, 0x1001, vsomeip::message_type_e::MT_REQUEST, /*reliability_=*/false, {0x1}};

    auto* provider = provider_ecu_.apps_[provider_name_];
    auto* consumer = consumer_ecu_.apps_[consumer_name_];

    provider->answer_request(req, [] { return std::vector<unsigned char>{0x2}; });
    provider->offer(iface_);

    consumer->request_service(si);
    ASSERT_TRUE(consumer->availability_record_.wait_for_last(service_availability::available(si)))
            << "consumer never saw the provider become available";

    // Every valid peer-to-peer rule shape. Unlike multicast, "both sides constrained" is a
    // perfectly legitimate unicast rule -- it must not be rejected at registration. Boardnet
    // UDP traffic is attributed to each ECU's *router*, not the individual client/server app --
    // only the routing manager ever owns the boardnet endpoint. Each side is exercised both
    // constrained by app_name_ only, and constrained by the literal endpoint_ + app_name_ --
    // the service's port is add_interface()'s default base_port, the client's is the first of
    // default_ports::optional_client, matching how this single-interface setup actually binds.
    boost::asio::ip::udp::endpoint const provider_ep_{provider_ecu_.address(), 30501};
    boost::asio::ip::udp::endpoint const consumer_ep_{consumer_ecu_.address(), default_ports::optional_client.first_};

    auto any_any = someip_gate::create("any_any");
    auto sender_named = someip_gate::create("sender_named");
    auto receiver_named = someip_gate::create("receiver_named");
    auto both_named = someip_gate::create("both_named");
    auto sender_with_port = someip_gate::create("sender_with_port");
    auto receiver_with_port = someip_gate::create("receiver_with_port");
    auto both_with_port = someip_gate::create("both_with_port");

    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party::any(), any_any->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(consumer_ecu_.router_name_), udp_party::any(), sender_named->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party::any(provider_ecu_.router_name_), receiver_named->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(consumer_ecu_.router_name_), udp_party::any(provider_ecu_.router_name_),
                                  both_named->get_data_pipe()))
            << "a fully-named unicast rule (both sides constrained by app_name_ only) must not be rejected -- "
               "that rejection is only meant for a per-recipient multicast receiver";
    ASSERT_TRUE(set_udp_data_pipe({consumer_ep_, consumer_ecu_.router_name_}, udp_party::any(), sender_with_port->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {provider_ep_, provider_ecu_.router_name_}, receiver_with_port->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe({consumer_ep_, consumer_ecu_.router_name_}, {provider_ep_, provider_ecu_.router_name_},
                                  both_with_port->get_data_pipe()));

    // Arm every gate on the first matching request; it must only ever advance one gate at a
    // time, in exactly this order, as each is released in turn.
    someip_gate::trigger const request_trigger{.service_ = si.service_, .method_ = req.method_, .type_ = std::nullopt};
    any_any->block_at(request_trigger);
    sender_named->block_at(request_trigger);
    receiver_named->block_at(request_trigger);
    both_named->block_at(request_trigger);
    sender_with_port->block_at(request_trigger);
    receiver_with_port->block_at(request_trigger);
    both_with_port->block_at(request_trigger);

    consumer->send_request(req);

    ASSERT_TRUE(any_any->wait_for_blocked()) << "any()+any() never saw the request";
    any_any->block(false);
    ASSERT_TRUE(sender_named->wait_for_blocked()) << "sender-named-only rule never saw the request";
    sender_named->block(false);
    ASSERT_TRUE(receiver_named->wait_for_blocked()) << "receiver-named-only rule never saw the request";
    receiver_named->block(false);
    ASSERT_TRUE(both_named->wait_for_blocked()) << "both-named rule (registration order, last link) never saw the request";
    both_named->block(false);
    ASSERT_TRUE(sender_with_port->wait_for_blocked()) << "sender-with-endpoint rule never saw the request";
    sender_with_port->block(false);
    ASSERT_TRUE(receiver_with_port->wait_for_blocked()) << "receiver-with-endpoint rule never saw the request";
    receiver_with_port->block(false);
    ASSERT_TRUE(both_with_port->wait_for_blocked()) << "both-with-endpoint rule never saw the request";
    both_with_port->block(false);
}

// Mirrors of the two tests above, registering the very same rule shapes in a shuffled/reversed order and
// asserting the shuffled order back -- proving a rule's position in the chain is purely a function of when
// it was registered, never of its shape (e.g. a sender-scoped rule registered before any()+any() now runs
// before it, where it used to be forced after regardless of registration order).
TEST_F(test_infrastructure, multicast_gates_apply_in_registration_order_even_when_shuffled) {
    prepare_and_start();

    auto sender_scoped = someip_gate::create("sender_scoped");
    auto any_any = someip_gate::create("any_any");
    auto member_b = someip_gate::create("member_b");
    auto member_a = someip_gate::create("member_a");

    // Sender-scoped registered *before* any()+any() -- and per-member rule B before rule A.
    ASSERT_TRUE(
            set_udp_data_pipe({provider_ecu_.sd_endpoint(), provider_ecu_.router_name_}, udp_party::any(), sender_scoped->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party::any(), any_any->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {multicast_ep_, consumer_ecu_.router_name_}, member_b->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {std::nullopt, consumer_ecu_.router_name_}, member_a->get_data_pipe()));

    someip_gate::sd_predicate const any_sd_message{[](auto const&) { return true; }};
    sender_scoped->block_at(any_sd_message);
    any_any->block_at(any_sd_message);
    member_b->block_at(any_sd_message);
    member_a->block_at(any_sd_message);

    provider_ecu_.apps_[provider_name_]->offer(iface_);

    ASSERT_TRUE(sender_scoped->wait_for_blocked()) << "sender-scoped rule (registered first) never saw the cyclic offer";
    sender_scoped->block(false);
    ASSERT_TRUE(any_any->wait_for_blocked()) << "any()+any() (registered second) was never chained in after the sender-scoped rule";
    any_any->block(false);
    ASSERT_TRUE(member_b->wait_for_blocked()) << "per-member rule B (registered before A) never saw the fanned-out offer";
    member_b->block(false);
    ASSERT_TRUE(member_a->wait_for_blocked()) << "per-member rule A (registered after B) was never chained in";
    member_a->block(false);
}

TEST_F(test_infrastructure, peer_to_peer_gates_apply_in_registration_order_even_when_shuffled) {
    prepare_and_start();

    service_instance const si{iface_.instance_};
    request const req{si, 0x1001, vsomeip::message_type_e::MT_REQUEST, /*reliability_=*/false, {0x1}};

    auto* provider = provider_ecu_.apps_[provider_name_];
    auto* consumer = consumer_ecu_.apps_[consumer_name_];

    provider->answer_request(req, [] { return std::vector<unsigned char>{0x2}; });
    provider->offer(iface_);

    consumer->request_service(si);
    ASSERT_TRUE(consumer->availability_record_.wait_for_last(service_availability::available(si)))
            << "consumer never saw the provider become available";

    boost::asio::ip::udp::endpoint const provider_ep_{provider_ecu_.address(), 30501};
    boost::asio::ip::udp::endpoint const consumer_ep_{consumer_ecu_.address(), default_ports::optional_client.first_};

    auto any_any = someip_gate::create("any_any");
    auto sender_named = someip_gate::create("sender_named");
    auto receiver_named = someip_gate::create("receiver_named");
    auto both_named = someip_gate::create("both_named");
    auto sender_with_port = someip_gate::create("sender_with_port");
    auto receiver_with_port = someip_gate::create("receiver_with_port");
    auto both_with_port = someip_gate::create("both_with_port");

    // The exact reverse registration order of peer_to_peer_gates_apply_in_the_documented_order.
    ASSERT_TRUE(set_udp_data_pipe({consumer_ep_, consumer_ecu_.router_name_}, {provider_ep_, provider_ecu_.router_name_},
                                  both_with_port->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), {provider_ep_, provider_ecu_.router_name_}, receiver_with_port->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe({consumer_ep_, consumer_ecu_.router_name_}, udp_party::any(), sender_with_port->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(consumer_ecu_.router_name_), udp_party::any(provider_ecu_.router_name_),
                                  both_named->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party::any(provider_ecu_.router_name_), receiver_named->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(consumer_ecu_.router_name_), udp_party::any(), sender_named->get_data_pipe()));
    ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party::any(), any_any->get_data_pipe()));

    someip_gate::trigger const request_trigger{.service_ = si.service_, .method_ = req.method_, .type_ = std::nullopt};
    both_with_port->block_at(request_trigger);
    receiver_with_port->block_at(request_trigger);
    sender_with_port->block_at(request_trigger);
    both_named->block_at(request_trigger);
    receiver_named->block_at(request_trigger);
    sender_named->block_at(request_trigger);
    any_any->block_at(request_trigger);

    consumer->send_request(req);

    ASSERT_TRUE(both_with_port->wait_for_blocked()) << "both-with-endpoint rule (registered first here) never saw the request";
    both_with_port->block(false);
    ASSERT_TRUE(receiver_with_port->wait_for_blocked()) << "receiver-with-endpoint rule was never chained in";
    receiver_with_port->block(false);
    ASSERT_TRUE(sender_with_port->wait_for_blocked()) << "sender-with-endpoint rule was never chained in";
    sender_with_port->block(false);
    ASSERT_TRUE(both_named->wait_for_blocked()) << "both-named rule was never chained in";
    both_named->block(false);
    ASSERT_TRUE(receiver_named->wait_for_blocked()) << "receiver-named-only rule was never chained in";
    receiver_named->block(false);
    ASSERT_TRUE(sender_named->wait_for_blocked()) << "sender-named-only rule was never chained in";
    sender_named->block(false);
    ASSERT_TRUE(any_any->wait_for_blocked()) << "any()+any() (registered last here) was never chained in";
}
}
