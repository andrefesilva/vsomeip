// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Round-trip tests for all struct-based protocol commands serialize/deserialize.
// ============================================================================

#include <gtest/gtest.h>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"

namespace vsomeip_v3::protocol {

template<typename Cmd>
std::vector<uint8_t> send(Cmd const& _input) {
    auto const size = wire_size(_input);
    std::vector<uint8_t> buf(size);
    serialize(_input, buf.data());
    return buf;
}
template<typename Cmd>
Cmd receive(std::vector<uint8_t> const& _buf) {
    Cmd out{};
    auto const size = static_cast<uint32_t>(_buf.size());
    auto const hdr_size = deserialize(out.header_, _buf.data(), size);
    if (hdr_size == 0) {
        return {};
    }
    if constexpr (has_payload<Cmd>) {
        if (0 == deserialize(out.payload_, _buf.data() + hdr_size, size - hdr_size)) {
            return {};
        }
    }
    return out;
}

template<typename Cmd>
auto roundtrip(Cmd const& _input) {
    if constexpr (std::is_same_v<multiple_service_command_data, Cmd>) {
        // this command owns the data only on reception, not on sending
        std::pair<command_header, std::vector<protocol::service_data>> out;
        auto buf = send(_input);
        auto const size = static_cast<uint32_t>(buf.size());
        auto const hdr_size = deserialize(out.first, buf.data(), size);
        if (hdr_size == 0) {
            return out;
        }
        deserialize(out.second, buf.data() + hdr_size, size - hdr_size);
        return out;
    } else {
        return receive<Cmd>(send(_input));
    }
}

// --- Simple (header-only) commands ---

TEST(ut_commands_roundtrip, ping_roundtrip) {
    EXPECT_EQ(roundtrip(create_ping_cmd(0x1234)), create_ping_cmd(0x1234));
}

TEST(ut_commands_roundtrip, pong_roundtrip) {
    EXPECT_EQ(roundtrip(create_pong_cmd(0xABCD)), create_pong_cmd(0xABCD));
}

TEST(ut_commands_roundtrip, suspend_roundtrip) {
    EXPECT_EQ(roundtrip(create_suspend_cmd(0x00FF)), create_suspend_cmd(0x00FF));
}

// --- Service commands ---

TEST(ut_commands_roundtrip, offer_service_roundtrip) {
    auto cmd = create_offer_service_cmd(0x1234, 0xABCD, 0x0001, 0x02, 0x00000003);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, stop_offer_service_roundtrip) {
    auto cmd = create_stop_offer_service_cmd(0x5678, 0x1111, 0x2222, 0x01, 0x00000000);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

// --- Single-field commands ---

TEST(ut_commands_roundtrip, assign_client_ack_roundtrip) {
    auto cmd = create_assign_client_ack_cmd(0x0000, 0x04CF);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, offered_services_request_roundtrip) {
    auto cmd = create_offered_services_request_cmd(0x1234, offer_type_e::OT_ALL);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, resend_provided_events_roundtrip) {
    auto cmd = create_resend_provided_events_cmd(0x00FF, 0x0000ABCD);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

// --- Service commands (composite payload) ---

TEST(ut_commands_roundtrip, release_service_roundtrip) {
    auto cmd = create_release_service_cmd(0x1234, 0xABCD, 0x0001);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, unregister_event_roundtrip) {
    auto cmd = create_unregister_event_cmd(0x1234, 0xABCD, 0x0001, 0x4242, true);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, unsubscribe_ack_roundtrip) {
    auto cmd = create_unsubscribe_ack_cmd(0x1234, 0xABCD, 0x0001, 0x0005, 0x1111);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, remove_security_policy_roundtrip) {
    auto cmd = create_remove_security_policy_cmd(0x1234, 0x00ABCDEF, 0x000003E8, 0x000003E9);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, update_security_policy_response_roundtrip) {
    auto cmd = create_update_security_policy_response_cmd(0x1234, 0x0000BEEF);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, remove_security_policy_response_roundtrip) {
    auto cmd = create_remove_security_policy_response_cmd(0x1234, 0x0000BEEF);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

// --- Non-owning commands
TEST(ut_commands_roundtrip, request_service_command) {
    std::vector<protocol::service_data> no{};
    std::vector<protocol::service_data> one{
            {.service_ = 0x12, .instance_ = 1, .major_version_ = DEFAULT_MAJOR, .minor_version_ = ANY_MINOR}};
    std::vector<protocol::service_data> many{{.service_ = 0xa, .instance_ = 2, .major_version_ = 1, .minor_version_ = 0},
                                             {.service_ = 0xb, .instance_ = 3, .major_version_ = 2, .minor_version_ = 1}};

    for (auto const& payload : {no, one, many}) {
        auto cmd = create_request_service_cmd(0x1, payload);
        auto [header, out_payload] = roundtrip(cmd);
        EXPECT_EQ(header, cmd.header_);
        EXPECT_EQ(out_payload, payload);
    }
}

TEST(ut_commands_roundtrip, offered_services_response_cmd) {
    std::vector<protocol::service_data> no{};
    std::vector<protocol::service_data> one{
            {.service_ = 0x21, .instance_ = 2, .major_version_ = ANY_MAJOR, .minor_version_ = DEFAULT_MINOR}};
    std::vector<protocol::service_data> many{{.service_ = 0xc, .instance_ = 1, .major_version_ = 1, .minor_version_ = 0},
                                             {.service_ = 0xd, .instance_ = 2, .major_version_ = 2, .minor_version_ = 1}};

    for (auto const& payload : {no, one, many}) {
        auto cmd = create_offered_services_response_cmd(0x1, payload);
        auto [header, out_payload] = roundtrip(cmd);
        EXPECT_EQ(header, cmd.header_);
        EXPECT_EQ(out_payload, payload);
    }
}

// --- Error handling ---

TEST(ut_commands_roundtrip, deserialize_rejects_truncated_header) {
    auto cmd = create_ping_cmd(0x04CF);
    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header out{};
    EXPECT_FALSE(deserialize(out, buf.data(), static_cast<uint32_t>(buf.size()) - 1));
}

TEST(ut_commands_roundtrip, deserialize_rejects_truncated_payload) {
    auto cmd = create_resend_provided_events_cmd(0x04CF, 0x00001234);
    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header hdr{};
    auto const hdr_size = deserialize(hdr, buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_GT(hdr_size, 0u);

    pending_remote_offer_id_t payload{};
    EXPECT_FALSE(deserialize(payload, buf.data() + hdr_size, static_cast<uint32_t>(buf.size()) - hdr_size - 1));
}

} // namespace vsomeip_v3::protocol
