// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Subscribe Answer Command Compatibility Tests (header + service_data payload)
// ============================================================================
//
// Proves that the new struct-based serialization produces
// byte-for-byte identical wire output to the old class-based approach
// that the new side understands this format is ensured by the corresponding roundtrip test
//
// ============================================================================

#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <vector>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/subscribe_ack_command.hpp"
#include "../../../implementation/protocol/include/subscribe_nack_command.hpp"

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

TEST(ut_compatibility_subscribe_answer_command, subscribe_ack_id) {
    auto cmd = protocol::create_subscribe_ack_cmd(
            0x7,
            subscribe_answer_data{
                    .service_ = 0x1, .instance_ = 0x2, .eventgroup_ = 0x3, .subscriber_ = 0x4, .event_ = 0x5, .pending_id_ = 0x6});

    auto old_cmd = protocol::subscribe_ack_command();
    old_cmd.set_client(cmd.header_.client_);
    old_cmd.set_service(cmd.payload_.service_);
    old_cmd.set_instance(cmd.payload_.instance_);
    old_cmd.set_eventgroup(cmd.payload_.eventgroup_);
    old_cmd.set_subscriber(cmd.payload_.subscriber_);
    old_cmd.set_event(cmd.payload_.event_);
    old_cmd.set_pending_id(cmd.payload_.pending_id_);

    auto new_wire = to_wire(cmd);
    auto old_wire = old_to_wire(old_cmd);

    EXPECT_EQ(new_wire, old_wire);
}

TEST(ut_compatibility_subscribe_answer_command, subscribe_nack_id) {
    auto cmd = protocol::create_subscribe_nack_cmd(
            0x99,
            subscribe_answer_data{
                    .service_ = 0x98, .instance_ = 0x97, .eventgroup_ = 0x96, .subscriber_ = 0x95, .event_ = 0x94, .pending_id_ = 0x93});

    auto old_cmd = protocol::subscribe_nack_command();
    old_cmd.set_client(cmd.header_.client_);
    old_cmd.set_service(cmd.payload_.service_);
    old_cmd.set_instance(cmd.payload_.instance_);
    old_cmd.set_eventgroup(cmd.payload_.eventgroup_);
    old_cmd.set_subscriber(cmd.payload_.subscriber_);
    old_cmd.set_event(cmd.payload_.event_);
    old_cmd.set_pending_id(cmd.payload_.pending_id_);

    auto new_wire = to_wire(cmd);
    auto old_wire = old_to_wire(old_cmd);

    EXPECT_EQ(new_wire, old_wire);
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
