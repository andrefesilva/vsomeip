// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Round-trip tests for the struct-based service commands serialize/deserialize.
// ============================================================================

#include <gtest/gtest.h>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"

namespace vsomeip_v3::protocol {

TEST(ut_service_commands_roundtrip, offer_service_roundtrip) {
    auto cmd = create_offer_service_cmd(0x1234, 0xABCD, 0x0001, 0x02, 0x00000003);

    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header header{};
    auto const header_size = deserialize(header, buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header, cmd.header_);

    service_data svc{};
    auto const payload_size = deserialize(svc, buf.data() + header_size, static_cast<uint32_t>(buf.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(svc, cmd.payload_);
}

TEST(ut_service_commands_roundtrip, stop_offer_service_roundtrip) {
    auto cmd = create_stop_offer_service_cmd(0x5678, 0x1111, 0x2222, 0x01, 0x00000000);

    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header header{};
    auto const header_size = deserialize(header, buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header, cmd.header_);

    service_data svc{};
    auto const payload_size = deserialize(svc, buf.data() + header_size, static_cast<uint32_t>(buf.size()) - header_size);
    ASSERT_GT(payload_size, 0u);
    EXPECT_EQ(svc, cmd.payload_);
}

TEST(ut_service_commands_roundtrip, deserialize_rejects_truncated_header) {
    auto cmd = create_offer_service_cmd(0x04CF, 0xBEEF, 0x0001, 0x03, 0x00000005);

    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header header{};
    // Truncate within the header region
    EXPECT_FALSE(deserialize(header, buf.data(), static_cast<uint32_t>(wire_size(command_header{})) - 1));
}

TEST(ut_service_commands_roundtrip, deserialize_rejects_truncated_payload) {
    auto cmd = create_offer_service_cmd(0x04CF, 0xBEEF, 0x0001, 0x03, 0x00000005);

    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header header{};
    auto const header_size = deserialize(header, buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_GT(header_size, 0u);

    service_data svc{};
    // Truncate within the payload region
    EXPECT_EQ(0u, deserialize(svc, buf.data() + header_size, static_cast<uint32_t>(buf.size()) - header_size - 1));
}

} // namespace vsomeip_v3::protocol
