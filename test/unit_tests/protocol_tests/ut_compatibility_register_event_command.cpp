// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Register Event Command Compatibility Tests (header + register_event payload)
// ============================================================================
//
// Proves that the new struct-based serialization produces
// byte-for-byte identical wire output to the old class-based approach.
// That the new side understands this format is ensured by the corresponding
// roundtrip test.
//
// ============================================================================

#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <set>
#include <vector>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"

#include "../../../implementation/protocol/include/register_events_command.hpp"

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

TEST(ut_compatibility_register_event_command, register_events_command) {
    std::vector<protocol::register_event_data> no{};
    std::vector<protocol::register_event_data> one{{.service_ = 0x1234,
                                                    .instance_ = 0x0001,
                                                    .event_ = 0x8001,
                                                    .event_type_ = event_type_e::ET_FIELD,
                                                    .is_provided_ = true,
                                                    .reliability_ = reliability_type_e::RT_RELIABLE,
                                                    .is_cyclic_ = false,
                                                    .eventgroups_ = {0x0010, 0x0020}}};
    std::vector<protocol::register_event_data> many{{.service_ = 0xaaaa,
                                                     .instance_ = 0x0002,
                                                     .event_ = 0x8002,
                                                     .event_type_ = event_type_e::ET_EVENT,
                                                     .is_provided_ = false,
                                                     .reliability_ = reliability_type_e::RT_UNRELIABLE,
                                                     .is_cyclic_ = true,
                                                     .eventgroups_ = {}},
                                                    {.service_ = 0xbbbb,
                                                     .instance_ = 0x0003,
                                                     .event_ = 0x8003,
                                                     .event_type_ = event_type_e::ET_SELECTIVE_EVENT,
                                                     .is_provided_ = true,
                                                     .reliability_ = reliability_type_e::RT_BOTH,
                                                     .is_cyclic_ = false,
                                                     .eventgroups_ = {0x0001, 0x0002, 0x0003}}};

    for (auto const& payload : {no, one, many}) {
        client_t client = 0x1;
        auto cmd = create_register_events_cmd(client, payload);

        register_events_command old_cmd;
        old_cmd.set_client(client);
        for (auto const& reg : payload) {
            old_cmd.add_registration(register_event{reg.service_, reg.instance_, reg.event_, reg.event_type_, reg.is_provided_,
                                                    reg.reliability_, reg.is_cyclic_, static_cast<uint16_t>(reg.eventgroups_.size()),
                                                    std::set<eventgroup_t>{reg.eventgroups_.begin(), reg.eventgroups_.end()}});
        }

        auto new_wire = to_wire(cmd);
        auto old_wire = old_to_wire(old_cmd);

        EXPECT_EQ(new_wire, old_wire);
    }
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
