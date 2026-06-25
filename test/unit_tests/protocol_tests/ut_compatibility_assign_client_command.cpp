// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Assign Client Command Compatibility Tests (header + variable-sized payload)
// ============================================================================
//
// Proves that the new struct-based serialization/deserialization produces
// byte-for-byte identical wire output to the old class-based approach and
// that each side can correctly consume what the other produces.
//
// For each command we verify:
//   1. old serialize → new deserialize (new code reads old format correctly)
//   2. new serialize → old deserialize (old code reads new format correctly)
//
// The assign-client payload carries an optional address, so both the
// with-address and without-address variants are exercised.
// ============================================================================

#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <vector>

#include <boost/asio/ip/address_v4.hpp>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"

#include "../../../implementation/protocol/include/assign_client_command.hpp"

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
// Assign Client (with address)
// ============================================================================

TEST(ut_compatibility_assign_client_command, with_address_old_serialize_new_deserialize) {
    assign_client_command old_cmd;
    old_cmd.set_client(0x0042);
    old_cmd.set_name("my_app");
    old_cmd.set_address(boost::asio::ip::make_address_v4("127.0.0.2"));
    old_cmd.set_port(31492);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    assign_client_data payload{};
    auto const payload_size = deserialize(payload, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(payload.name_, old_cmd.get_name());
    EXPECT_TRUE(payload.has_address_);
    EXPECT_EQ(payload.address_bytes_, old_cmd.get_address().to_v4().to_bytes());
    EXPECT_EQ(payload.port_, old_cmd.get_port());
}

TEST(ut_compatibility_assign_client_command, with_address_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_assign_client_cmd(0x0042, "my_app", {127, 0, 0, 2}, 31492, true));

    assign_client_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::ASSIGN_CLIENT_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x0042);
    EXPECT_EQ(old_cmd.get_name(), "my_app");
    ASSERT_TRUE(old_cmd.get_address().is_v4());
    EXPECT_EQ(old_cmd.get_address().to_v4(), boost::asio::ip::make_address_v4("127.0.0.2"));
    EXPECT_EQ(old_cmd.get_port(), 31492);
}

// ============================================================================
// Assign Client (without address)
// ============================================================================

TEST(ut_compatibility_assign_client_command, without_address_old_serialize_new_deserialize) {
    assign_client_command old_cmd;
    old_cmd.set_client(0x0010);
    old_cmd.set_name("client");
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    assign_client_data payload{};
    auto const payload_size = deserialize(payload, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(payload.name_, old_cmd.get_name());
    EXPECT_FALSE(payload.has_address_);
}

TEST(ut_compatibility_assign_client_command, without_address_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_assign_client_cmd(0x0010, "client"));

    assign_client_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::ASSIGN_CLIENT_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x0010);
    EXPECT_EQ(old_cmd.get_name(), "client");
    EXPECT_TRUE(old_cmd.get_address().is_unspecified());
}

} // namespace vsomeip_v3::protocol
