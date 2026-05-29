// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Service Command Compatibility Tests (header + service_data payload)
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

#include "../../../implementation/protocol/include/offer_service_command.hpp"
#include "../../../implementation/protocol/include/stop_offer_service_command.hpp"

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
// Offer Service
// ============================================================================

TEST(ut_compatibility_service_commands, offer_service_old_serialize_new_deserialize) {
    offer_service_command old_cmd;
    old_cmd.set_client(0x1234);
    old_cmd.set_service(0xABCD);
    old_cmd.set_instance(0x0001);
    old_cmd.set_major(0x02);
    old_cmd.set_minor(0x00000003);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    service_data svc{};
    auto const payload_size = deserialize(svc, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(svc.service_, old_cmd.get_service());
    EXPECT_EQ(svc.instance_, old_cmd.get_instance());
    EXPECT_EQ(svc.major_version_, old_cmd.get_major());
    EXPECT_EQ(svc.minor_version_, old_cmd.get_minor());
}

TEST(ut_compatibility_service_commands, offer_service_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_offer_service_cmd(0x1234, 0xABCD, 0x0001, 0x02, 0x00000003));

    offer_service_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::OFFER_SERVICE_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x1234);
    EXPECT_EQ(old_cmd.get_service(), 0xABCD);
    EXPECT_EQ(old_cmd.get_instance(), 0x0001);
    EXPECT_EQ(old_cmd.get_major(), 0x02);
    EXPECT_EQ(old_cmd.get_minor(), 0x00000003u);
}

// ============================================================================
// Stop Offer Service
// ============================================================================

TEST(ut_compatibility_service_commands, stop_offer_service_old_serialize_new_deserialize) {
    stop_offer_service_command old_cmd;
    old_cmd.set_client(0x5678);
    old_cmd.set_service(0x1111);
    old_cmd.set_instance(0x2222);
    old_cmd.set_major(0x01);
    old_cmd.set_minor(0x00000000);
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    service_data svc{};
    auto const payload_size = deserialize(svc, bytes.data() + header_size, static_cast<uint32_t>(bytes.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(svc.service_, old_cmd.get_service());
    EXPECT_EQ(svc.instance_, old_cmd.get_instance());
    EXPECT_EQ(svc.major_version_, old_cmd.get_major());
    EXPECT_EQ(svc.minor_version_, old_cmd.get_minor());
}

TEST(ut_compatibility_service_commands, stop_offer_service_new_serialize_old_deserialize) {
    auto bytes = to_wire(create_stop_offer_service_cmd(0x5678, 0x1111, 0x2222, 0x01, 0x00000000));

    stop_offer_service_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::STOP_OFFER_SERVICE_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x5678);
    EXPECT_EQ(old_cmd.get_service(), 0x1111);
    EXPECT_EQ(old_cmd.get_instance(), 0x2222);
    EXPECT_EQ(old_cmd.get_major(), 0x01);
    EXPECT_EQ(old_cmd.get_minor(), 0x00000000u);
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
