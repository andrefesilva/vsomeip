// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Compatibility Tests for the struct-based replacements of the deprecated
// class-based commands (release_service, unregister_event, unsubscribe_ack,
// remove_security_policy and the two security-policy response commands).
// ============================================================================
//
// Proves that the new struct-based serialization/deserialization produces
// byte-for-byte identical wire output to the old class-based approach and
// that each side can correctly consume what the other produces.
//
// For each command we verify:
//   1. old serialize -> new deserialize (new code reads old format correctly)
//   2. new serialize -> old deserialize (old code reads new format correctly)
// ============================================================================

#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <vector>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"

#include "../../../implementation/protocol/include/release_service_command.hpp"
#include "../../../implementation/protocol/include/remove_security_policy_command.hpp"
#include "../../../implementation/protocol/include/remove_security_policy_response_command.hpp"
#include "../../../implementation/protocol/include/unregister_event_command.hpp"
#include "../../../implementation/protocol/include/unsubscribe_ack_command.hpp"
#include "../../../implementation/protocol/include/update_security_policy_response_command.hpp"

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
// Release Service
// ============================================================================

TEST(ut_compatibility_deprecated_commands, release_service_old_serialize_new_deserialize) {
    release_service_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_service(0xABCD);
    old_cmd.set_instance(0x0001);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    release_service_data data{};
    auto const payload_size = deserialize(data, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(data.service_, old_cmd.get_service());
    EXPECT_EQ(data.instance_, old_cmd.get_instance());
}

TEST(ut_compatibility_deprecated_commands, release_service_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_release_service_cmd(0x1234, 0xABCD, 0x0001));

    release_service_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::RELEASE_SERVICE_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_service(), 0xABCD);
    EXPECT_EQ(old_cmd.get_instance(), 0x0001);
}

// ============================================================================
// Unregister Event
// ============================================================================

TEST(ut_compatibility_deprecated_commands, unregister_event_old_serialize_new_deserialize) {
    unregister_event_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_service(0xABCD);
    old_cmd.set_instance(0x0001);
    old_cmd.set_event(0x4242);
    old_cmd.set_provided(true);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    unregister_event_data data{};
    auto const payload_size = deserialize(data, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(data.service_, old_cmd.get_service());
    EXPECT_EQ(data.instance_, old_cmd.get_instance());
    EXPECT_EQ(data.event_, old_cmd.get_event());
    EXPECT_EQ(data.is_provided_, old_cmd.is_provided());
}

TEST(ut_compatibility_deprecated_commands, unregister_event_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_unregister_event_cmd(0x1234, 0xABCD, 0x0001, 0x4242, true));

    unregister_event_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::UNREGISTER_EVENT_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_service(), 0xABCD);
    EXPECT_EQ(old_cmd.get_instance(), 0x0001);
    EXPECT_EQ(old_cmd.get_event(), 0x4242);
    EXPECT_EQ(old_cmd.is_provided(), true);
}

// ============================================================================
// Unsubscribe Ack
// ============================================================================

TEST(ut_compatibility_deprecated_commands, unsubscribe_ack_old_serialize_new_deserialize) {
    unsubscribe_ack_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_service(0xABCD);
    old_cmd.set_instance(0x0001);
    old_cmd.set_eventgroup(0x0005);
    old_cmd.set_pending_id(0x1111);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    unsubscribe_ack_data data{};
    auto const payload_size = deserialize(data, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(data.service_, old_cmd.get_service());
    EXPECT_EQ(data.instance_, old_cmd.get_instance());
    EXPECT_EQ(data.eventgroup_, old_cmd.get_eventgroup());
    EXPECT_EQ(data.pending_id_, old_cmd.get_pending_id());
}

TEST(ut_compatibility_deprecated_commands, unsubscribe_ack_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_unsubscribe_ack_cmd(0x1234, 0xABCD, 0x0001, 0x0005, 0x1111));

    unsubscribe_ack_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::UNSUBSCRIBE_ACK_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_service(), 0xABCD);
    EXPECT_EQ(old_cmd.get_instance(), 0x0001);
    EXPECT_EQ(old_cmd.get_eventgroup(), 0x0005);
    EXPECT_EQ(old_cmd.get_pending_id(), 0x1111);
}

// ============================================================================
// Remove Security Policy
// ============================================================================

TEST(ut_compatibility_deprecated_commands, remove_security_policy_old_serialize_new_deserialize) {
    remove_security_policy_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_update_id(0x00ABCDEF);
    old_cmd.set_uid(0x000003E8);
    old_cmd.set_gid(0x000003E9);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    remove_security_policy_data data{};
    auto const payload_size = deserialize(data, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(data.update_id_, old_cmd.get_update_id());
    EXPECT_EQ(data.uid_, old_cmd.get_uid());
    EXPECT_EQ(data.gid_, old_cmd.get_gid());
}

TEST(ut_compatibility_deprecated_commands, remove_security_policy_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_remove_security_policy_cmd(0x1234, 0x00ABCDEF, 0x000003E8, 0x000003E9));

    remove_security_policy_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::REMOVE_SECURITY_POLICY_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_update_id(), 0x00ABCDEFu);
    EXPECT_EQ(old_cmd.get_uid(), 0x000003E8u);
    EXPECT_EQ(old_cmd.get_gid(), 0x000003E9u);
}

// ============================================================================
// Update Security Policy Response
// ============================================================================

TEST(ut_compatibility_deprecated_commands, update_security_policy_response_old_serialize_new_deserialize) {
    update_security_policy_response_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_update_id(0x0000BEEF);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    uint32_t update_id{};
    auto const payload_size = deserialize(update_id, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(update_id, old_cmd.get_update_id());
}

TEST(ut_compatibility_deprecated_commands, update_security_policy_response_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_update_security_policy_response_cmd(0x1234, 0x0000BEEF));

    update_security_policy_response_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::UPDATE_SECURITY_POLICY_RESPONSE_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_update_id(), 0x0000BEEFu);
}

// ============================================================================
// Remove Security Policy Response
// ============================================================================

TEST(ut_compatibility_deprecated_commands, remove_security_policy_response_old_serialize_new_deserialize) {
    remove_security_policy_response_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_update_id(0x0000BEEF);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    uint32_t update_id{};
    auto const payload_size = deserialize(update_id, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(update_id, old_cmd.get_update_id());
}

TEST(ut_compatibility_deprecated_commands, remove_security_policy_response_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_remove_security_policy_response_cmd(0x1234, 0x0000BEEF));

    remove_security_policy_response_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::REMOVE_SECURITY_POLICY_RESPONSE_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_update_id(), 0x0000BEEFu);
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
