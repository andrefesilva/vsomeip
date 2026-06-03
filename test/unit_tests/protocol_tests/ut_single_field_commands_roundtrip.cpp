// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Round-trip tests for the struct-based single-field commands serialize/deserialize.
// ============================================================================

#include <gtest/gtest.h>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"

namespace vsomeip_v3::protocol {

template<typename SingleFieldCommand>
SingleFieldCommand roundtrip(SingleFieldCommand const& _input) {
    auto const size = wire_size(_input);
    std::vector<uint8_t> buf(size);
    serialize(_input, buf.data());

    SingleFieldCommand out{};
    auto const hdr_size = deserialize(out.header_, buf.data(), size);
    if (hdr_size == 0) {
        return {};
    }
    if (0 == deserialize(out.payload_, buf.data() + hdr_size, size - hdr_size)) {
        return {};
    }
    return out;
}

TEST(ut_single_field_commands_roundtrip, assign_client_ack_roundtrip) {
    auto cmd = create_assign_client_ack_cmd(0x0000, 0x04CF);
    auto out = roundtrip(cmd);
    EXPECT_EQ(out.header_, cmd.header_);
    EXPECT_EQ(out.payload_, cmd.payload_);
}

TEST(ut_single_field_commands_roundtrip, offered_services_request_roundtrip) {
    auto cmd = create_offered_services_request_cmd(0x1234, offer_type_e::OT_ALL);
    auto out = roundtrip(cmd);
    EXPECT_EQ(out.header_, cmd.header_);
    EXPECT_EQ(out.payload_, cmd.payload_);
}

TEST(ut_single_field_commands_roundtrip, resend_provided_events_roundtrip) {
    auto cmd = create_resend_provided_events_cmd(0x00FF, 0x0000ABCD);
    auto out = roundtrip(cmd);
    EXPECT_EQ(out.header_, cmd.header_);
    EXPECT_EQ(out.payload_, cmd.payload_);
}

TEST(ut_single_field_commands_roundtrip, deserialize_rejects_truncated_payload) {
    auto cmd = create_resend_provided_events_cmd(0x04CF, 0x00001234);

    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header hdr{};
    auto const hdr_size = deserialize(hdr, buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_GT(hdr_size, 0u);

    pending_remote_offer_id_t out{};
    // Feed one byte less than needed for the payload
    EXPECT_FALSE(deserialize(out, buf.data() + hdr_size, static_cast<uint32_t>(buf.size()) - hdr_size - 1));
}

} // namespace vsomeip_v3::protocol
