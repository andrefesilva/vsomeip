// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Single-Field Command Compatibility Tests (header + single typed payload)
// ============================================================================
//
// Proves that the new struct-based serialization/deserialization produces
// byte-for-byte identical wire output to the old class-based approach and
// that each side can correctly consume what the other produces.
//
// For each command we verify:
//   1. old serialize → new deserialize (new code reads old format correctly)
//   2. new serialize → old deserialize (old code reads new format correctly)
// ============================================================================

#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <vector>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"

#include "../../../implementation/protocol/include/assign_client_ack_command.hpp"
#include "../../../implementation/protocol/include/offered_services_request_command.hpp"
#include "../../../implementation/protocol/include/resend_provided_events_command.hpp"

namespace vsomeip_v3::protocol {

template<typename T>
static std::vector<uint8_t> to_wire(T const& _cmd) {
    std::vector<uint8_t> buf(wire_size(_cmd));
    serialize(_cmd, buf.data());
    return buf;
}

template<typename OldCmd>
static std::vector<uint8_t> old_to_wire(OldCmd const& _cmd) {
    std::vector<uint8_t> buf;
    _cmd.serialize(buf);
    return buf;
}

// ============================================================================
// Assign Client Ack
// ============================================================================

TEST(ut_compatibility_single_field_commands, assign_client_ack_old_serialize_new_deserialize) {
    assign_client_ack_command old_cmd;
    old_cmd.set_client(0x0000);
    old_cmd.set_assigned(0x04CF);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    client_t result{};
    auto const payload_size = deserialize(result, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(result, old_cmd.get_assigned());
}

TEST(ut_compatibility_single_field_commands, assign_client_ack_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_assign_client_ack_cmd(0x0000, 0x04CF));

    assign_client_ack_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::ASSIGN_CLIENT_ACK_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x0000);
    EXPECT_EQ(old_cmd.get_assigned(), 0x04CF);
}

// ============================================================================
// Offered Services Request
// ============================================================================

TEST(ut_compatibility_single_field_commands, offered_services_request_old_serialize_new_deserialize) {
    offered_services_request_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_offer_type(offer_type_e::OT_REMOTE);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    offer_type_e result{};
    auto const payload_size = deserialize(result, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(result, old_cmd.get_offer_type());
}

TEST(ut_compatibility_single_field_commands, offered_services_request_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_offered_services_request_cmd(0x1234, offer_type_e::OT_REMOTE));

    offered_services_request_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::OFFERED_SERVICES_REQUEST_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_offer_type(), offer_type_e::OT_REMOTE);
}

// ============================================================================
// Resend Provided Events
// ============================================================================

TEST(ut_compatibility_single_field_commands, resend_provided_events_old_serialize_new_deserialize) {
    resend_provided_events_command old_cmd;
    old_cmd.set_client(0x00FF);
    old_cmd.set_remote_offer_id(0x0000ABCD);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    pending_remote_offer_id_t result{};
    auto const payload_size = deserialize(result, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(result, old_cmd.get_remote_offer_id());
}

TEST(ut_compatibility_single_field_commands, resend_provided_events_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_resend_provided_events_cmd(0x00FF, 0x0000ABCD));

    resend_provided_events_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::RESEND_PROVIDED_EVENTS_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x00FF);
    EXPECT_EQ(old_cmd.get_remote_offer_id(), 0x0000ABCDu);
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
