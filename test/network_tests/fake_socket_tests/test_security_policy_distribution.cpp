// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/attribute_recorder.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/command_message.hpp"
#include "helpers/ecu_setup.hpp"

#include <vsomeip/vsomeip.hpp>
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace vsomeip_v3::testing {
namespace {
std::string const routingmanager_name_{"routingmanagerd"};
std::string const client_name_{"client"};
constexpr client_t routingmanager_id_{0x0100};
constexpr client_t client_id_{0x3490};

/// Credentials the routing manager sees for an accepted local connection. The fake UDS socket does
/// not implement SO_PEERCRED, so the sec_client stays zeroed
/// (see fake_uds_socket::get_peer_credentials).
const security_credentials peer_credentials_{0, 0};

security_credentials own_credentials() {
    return {getuid(), getgid()};
}

security_credentials foreign_credentials() {
    return {static_cast<uid_t>(getuid() + 1), getgid()};
}

/// UDS routing with security in enforcing mode. Local TCP routing would not do: check_credentials()
/// passes unconditionally as soon as the sec_client carries a port. The routing manager needs a
/// fixed client id in every configuration that enables security.
ecu_config enforcing_config(std::vector<application_config> _apps, std::vector<security_credentials> _allowed) {
    ecu_config its_config;
    its_config.apps_ = std::move(_apps);
    its_config.routing_config_ = routingmanager_name_;
    its_config.sd_ = false;
    its_config.security_ = security_config{.check_credentials_ = true, .allowed_ = std::move(_allowed)};
    return its_config;
}
}

struct test_security_policy_distribution : public base_fake_socket_fixture {
    test_security_policy_distribution() {
        // registered on the connection, so the record survives the reconnects a refused
        // registration causes
        set_custom_command_handler(
                client_name_, routingmanager_name_,
                [this](command_message const& _msg) {
                    commands_to_client_.record(_msg.id_);
                    return false; // never drop a message
                },
                socket_role::server);
        // full command_message (not just the id) recorded here: the client id in its header is
        // exactly what routing_manager_stub::on_message checks against the bound connection.
        set_custom_command_handler(
                client_name_, routingmanager_name_,
                [this](command_message const& _msg) {
                    commands_from_client_.record(_msg);
                    return false; // never drop a message
                },
                socket_role::client);
    }

    /// Starts the routing manager and gives the client a configuration of its own, allowing only
    /// _client_allowed. The client itself is started by the test.
    void start_router(std::vector<security_credentials> _client_allowed) {
        ecu_.add_app(
                {client_name_, client_id_},
                enforcing_config({{routingmanager_name_, routingmanager_id_}, {client_name_, client_id_}}, std::move(_client_allowed)));
        ecu_.prepare();
        ecu_.start_router();
        ASSERT_NE(ecu_.router_, nullptr);
        ASSERT_TRUE(ecu_.router_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    }

    /// Mimics a security manager handing a policy for _credentials to the routing manager at
    /// runtime: it is applied locally and cached for every client registering afterwards.
    void distribute_policy(security_credentials const& _credentials) {
        ecu_.router_->update_security_policy_configuration(_credentials.uid_, _credentials.gid_);
    }

    // Declared before ecu_ so it is destroyed after it: ecu_'s apps (and their background IO
    // threads, which invoke the custom command handler that records into this) must be fully
    // stopped before commands_to_client_/commands_from_client_ are torn down, otherwise a
    // still-running app thread can race with the destructor of its underlying vector.
    attribute_recorder<protocol::id_e> commands_to_client_;
    attribute_recorder<command_message> commands_from_client_;

    /// peer_credentials_ to accept the incoming local connections, own_credentials() because the
    /// routing manager host registers itself through a routing_manager_client as well.
    ecu_setup ecu_{"security", enforcing_config({{routingmanager_name_, routingmanager_id_}}, {peer_credentials_, own_credentials()}),
                   *socket_manager_};
};

TEST_F(test_security_policy_distribution, distributed_policy_arrives_after_the_client_checked_its_own_credentials) {
    // the policy covering the client's own uid/gid is only known to the routing manager
    start_router({foreign_credentials()});
    distribute_policy(own_credentials());

    auto const its_drop = watch_connection_drop(client_name_, routingmanager_name_);
    ASSERT_NE(ecu_.start_one(client_name_), nullptr);

    // the routing manager does forward the cached policy to the registering client ...
    ASSERT_TRUE(commands_to_client_.wait_for_any(protocol::id_e::DISTRIBUTE_SECURITY_POLICIES_ID));

    // ... but routing_manager_stub::on_register_application sends it only after ASSIGN_CLIENT_ACK was
    // queued, and the client checks its own credentials while handling that ack. The check therefore
    // always fails on a fresh connection and the client starts the whole registration over.
    // Invert this expectation once the distribution happens before the client decides.
    EXPECT_FALSE(its_drop.wait());
}

TEST_F(test_security_policy_distribution, requester_policy_is_bundled_into_the_assign_client_ack) {
    // the sequence of vsomeip messages being tested is the following:
    // 1. the client registers with the routing manager (ASSIGN_CLIENT_ID)
    // 2. the routing manager responds with ASSIGN_CLIENT_ACK_ID, which carries the assigned client
    //    id together with any requester policy for that client, bundled directly into the ack
    //    (rather than sent separately beforehand as UPDATE_SECURITY_POLICY_INT_ID) precisely so
    //    that the client already has its real id by the time it applies one
    // 3. the client applies the bundled policy directly and does NOT send a response back: this is
    //    the client's own initial state, not a runtime update, and nothing external ever waits on
    //    whether a freshly-registering client applied its own initial policies

    // start client_name_ without client_id_ in the config, so it gets a dynamically assigned client id
    ecu_.add_app(client_name_);
    ecu_.prepare();
    ecu_.start_router();
    ASSERT_NE(ecu_.router_, nullptr);
    ASSERT_TRUE(ecu_.router_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

    service_instance const si{0x1234, 0x5678};
    security_credentials const some_requester{peer_credentials_.uid_ + 1, peer_credentials_.gid_ + 1};

    // a requester is needed to populate any_client_policies_ so a match can be found
    ecu_.router_->allow_requesting(some_requester.uid_, some_requester.gid_, si);

    // the client's uid/gid (peer_credentials_) is allowed to offer the
    // service, so the client can register and receive a policy for it.
    ecu_.router_->allow_offering(peer_credentials_.uid_, peer_credentials_.gid_, si);

    // capture the actual ASSIGN_CLIENT_ACK_ID sent to the client, so its bundled content can be
    // inspected below - commands_to_client_ (installed by the fixture) only records the message id
    attribute_recorder<command_message> ack_to_client;
    set_custom_command_handler(
            client_name_, routingmanager_name_,
            [&ack_to_client](command_message const& _msg) {
                if (_msg.id_ == protocol::id_e::ASSIGN_CLIENT_ACK_ID) {
                    ack_to_client.record(_msg);
                }
                return false; // never drop a message
            },
            socket_role::server);

    auto* its_client = ecu_.start_one(client_name_);
    ASSERT_NE(its_client, nullptr);
    // registration completing at all proves the bundled policy round-tripped and was applied
    // without upsetting the registration handshake
    ASSERT_TRUE(its_client->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

    // no UPDATE_SECURITY_POLICY_RESPONSE_ID is ever sent for a bundled, initial policy
    EXPECT_FALSE(commands_from_client_.wait_for(
            [](auto const& _record) {
                return std::any_of(_record.begin(), _record.end(),
                                   [](command_message const& _m) { return _m.id_ == protocol::id_e::UPDATE_SECURITY_POLICY_RESPONSE_ID; });
            },
            std::chrono::milliseconds(200)));

    // the ack must actually carry a requester policy for some_requester
    auto const its_ack = ack_to_client.last();
    ASSERT_TRUE(its_ack.has_value());
    auto const* its_ack_data = its_ack->payload_.get_if<protocol::assign_client_ack_data>();
    ASSERT_NE(its_ack_data, nullptr);
    ASSERT_FALSE(its_ack_data->policies_.empty());

    bool found_matching_requester = false;
    for (auto const& its_update : its_ack_data->policies_) {
        auto its_policy = std::make_shared<vsomeip::policy>();
        byte_t const* ptr = its_update.policy_.data();
        uint32_t size = static_cast<uint32_t>(its_update.policy_.size());
        if (!its_policy->deserialize(ptr, size)) {
            continue;
        }
        uid_t its_uid;
        gid_t its_gid;
        if (its_policy->get_uid_gid(its_uid, its_gid) && its_uid == some_requester.uid_ && its_gid == some_requester.gid_) {
            EXPECT_FALSE(its_policy->requests_.empty()) << "bundled requester policy has no request grants for si";
            found_matching_requester = true;
        }
    }
    EXPECT_TRUE(found_matching_requester) << "no bundled policy matched some_requester's uid/gid";
}

TEST_F(test_security_policy_distribution, policy_in_the_static_configuration_allows_registration) {
    // same setup, but the client does not depend on the distributed policy
    start_router({own_credentials()});
    distribute_policy(own_credentials());

    auto* its_client = ecu_.start_one(client_name_);
    ASSERT_NE(its_client, nullptr);
    EXPECT_TRUE(its_client->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
    EXPECT_EQ(1u, connection_count(client_name_, routingmanager_name_));
}

TEST_F(test_security_policy_distribution, corrupted_assign_client_ack_does_not_leave_the_client_stuck) {
    // ASSIGN_CLIENT_ACK_ID is deals with a client registration. If it ever arrives corrupted,
    // nothing else would retry it on its own, so we should ensure
    // the client can recover from a corrupted ack.

    ecu_.add_app(client_name_);
    ecu_.prepare();
    ecu_.start_router();
    ASSERT_NE(ecu_.router_, nullptr);
    ASSERT_TRUE(ecu_.router_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

    // Drop the real ack once, then inject a corrupted one
    auto its_drop = drop_command_once(client_name_, routingmanager_name_, protocol::id_e::ASSIGN_CLIENT_ACK_ID);

    auto* its_client = ecu_.start_one(client_name_);
    ASSERT_NE(its_client, nullptr);

    ASSERT_TRUE(its_drop.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    auto corrupted_ack = construct_basic_raw_command(protocol::id_e::ASSIGN_CLIENT_ACK_ID, // command
                                                     static_cast<uint16_t>(0), // version
                                                     static_cast<client_t>(routingmanager_id_), // sender
                                                     static_cast<uint32_t>(6), // payload size: assigned_(2) + element_count(4)
                                                     static_cast<client_t>(0x4711), // assigned_ client id
                                                     static_cast<uint32_t>(5) // policies_ element_count: claims 5, supplies none
    );
    ASSERT_TRUE(inject_command_tcp(client_name_, routingmanager_name_, corrupted_ack, socket_role::client));

    // The client must recover on its own: reconnect, get a fresh (this time valid) ack, and
    // register
    EXPECT_TRUE(its_client->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED, std::chrono::seconds(2)));
}
}
