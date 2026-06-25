// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Subscribe Command Compatibility Tests (header + service_data payload)
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
#include "../../../implementation/protocol/include/expire_command.hpp"
#include "../../../implementation/protocol/include/unsubscribe_command.hpp"
#include "../../../implementation/protocol/include/subscribe_command.hpp"

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

TEST(ut_compatibility_subscribe_command, expire_id) {
    auto cmd = protocol::create_expire_cmd(
            0x7, subscribe_data{.service_ = 0x1, .instance_ = 0x2, .eventgroup_ = 0x3, .major_ = 0x4, .event_ = 0x5, .pending_id_ = 0x6});

    auto old_cmd = protocol::expire_command();
    old_cmd.set_client(cmd.header_.client_);
    old_cmd.set_service(cmd.payload_.service_);
    old_cmd.set_instance(cmd.payload_.instance_);
    old_cmd.set_eventgroup(cmd.payload_.eventgroup_);
    old_cmd.set_major(cmd.payload_.major_);
    old_cmd.set_event(cmd.payload_.event_);
    old_cmd.set_pending_id(cmd.payload_.pending_id_);

    auto new_wire = to_wire(cmd);
    auto old_wire = old_to_wire(old_cmd);

    EXPECT_EQ(new_wire, old_wire);
}

TEST(ut_compatibility_subscribe_command, unsubscribe_id) {
    auto cmd = protocol::create_unsubscribe_cmd(
            0x14,
            subscribe_data{.service_ = 0x8, .instance_ = 0x9, .eventgroup_ = 0x10, .major_ = 0x11, .event_ = 0x12, .pending_id_ = 0x13});

    auto old_cmd = protocol::unsubscribe_command();
    old_cmd.set_client(cmd.header_.client_);
    old_cmd.set_service(cmd.payload_.service_);
    old_cmd.set_instance(cmd.payload_.instance_);
    old_cmd.set_eventgroup(cmd.payload_.eventgroup_);
    old_cmd.set_major(cmd.payload_.major_);
    old_cmd.set_event(cmd.payload_.event_);
    old_cmd.set_pending_id(cmd.payload_.pending_id_);

    auto new_wire = to_wire(cmd);
    auto old_wire = old_to_wire(old_cmd);

    EXPECT_EQ(new_wire, old_wire);
}
TEST(ut_compatibility_subscribe_command, subscribe_id) {
    std::shared_ptr<debounce_filter_impl_t> no_filter = nullptr;
    std::shared_ptr<debounce_filter_impl_t> no_map = std::make_shared<debounce_filter_impl_t>();
    no_map->on_change_ = true;
    no_map->on_change_resets_interval_ = true;
    no_map->interval_ = 64;
    no_map->send_current_value_after_ = true;
    std::shared_ptr<debounce_filter_impl_t> one_entry = std::make_shared<debounce_filter_impl_t>();
    one_entry->ignore_[0] = 0x0;
    one_entry->send_current_value_after_ = true;

    std::shared_ptr<debounce_filter_impl_t> many_entries = std::make_shared<debounce_filter_impl_t>();
    many_entries->ignore_[1] = 0x2;
    many_entries->ignore_[2] = 0x33;
    many_entries->ignore_[2092834] = 0x8;
    many_entries->send_current_value_after_ = true;

    auto const data =
            subscribe_data{.service_ = 0x8, .instance_ = 0x9, .eventgroup_ = 0x10, .major_ = 0x11, .event_ = 0x12, .pending_id_ = 0x13};

    for (auto filter : {no_filter, no_map, one_entry, many_entries}) {
        auto cmd = protocol::create_subscribe_cmd(0x14, filter, data);

        auto old_cmd = protocol::subscribe_command();
        old_cmd.set_client(cmd.header_.client_);
        old_cmd.set_service(data.service_);
        old_cmd.set_instance(data.instance_);
        old_cmd.set_eventgroup(data.eventgroup_);
        old_cmd.set_major(data.major_);
        old_cmd.set_event(data.event_);
        old_cmd.set_pending_id(data.pending_id_);
        old_cmd.set_filter(filter);

        auto new_wire = to_wire(cmd);
        auto old_wire = old_to_wire(old_cmd);

        EXPECT_EQ(new_wire, old_wire);
    }
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
