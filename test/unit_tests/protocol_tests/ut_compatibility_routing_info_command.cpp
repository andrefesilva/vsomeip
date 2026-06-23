// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Routing Info Command Compatibility Tests (header + routing_info_entry payload)
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

#include <vector>

#include <boost/asio/ip/address.hpp>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"

#include "../../../implementation/protocol/include/routing_info_command.hpp"
#include "../../../implementation/protocol/include/routing_info_entry.hpp"

namespace vsomeip_v3::protocol {

template<typename T>
static std::vector<uint8_t> to_wire(T const& _cmd) {
    std::vector<uint8_t> buf(wire_size(_cmd));
    serialize(_cmd, buf.data());
    return buf;
}

static std::vector<uint8_t> old_to_wire(routing_info_command const& _cmd) {
    std::vector<byte_t> buf;
    _cmd.serialize(buf);
    return buf;
}

static routing_info_entry make_old_entry(routing_info_entry_data const& _data) {
    routing_info_entry entry;
    entry.set_type(_data.type_);
    entry.set_client(_data.client_);
    if (!_data.address_.is_unspecified()) {
        entry.set_address(_data.address_);
        entry.set_port(_data.port_);
    }
    for (auto const& s : _data.services_) {
        entry.add_service({s.service_, s.instance_, s.major_version_, s.minor_version_});
    }
    return entry;
}

static routing_info_command make_old_cmd(client_t _client, std::vector<routing_info_entry_data> const& _entries) {
    routing_info_command old_cmd;
    old_cmd.set_client(_client);
    for (auto const& e : _entries) {
        old_cmd.add_entry(make_old_entry(e));
    }
    return old_cmd;
}

// New serialize must equal old serialize for the same logical content.
static void expect_wire_equal(client_t _client, std::vector<routing_info_entry_data> const& _entries) {
    auto new_cmd = create_routing_info_cmd(_client, _entries);
    auto old_cmd = make_old_cmd(_client, _entries);
    EXPECT_EQ(to_wire(new_cmd), old_to_wire(old_cmd));
}

TEST(ut_compatibility_routing_info_command, empty) {
    expect_wire_equal(0x1234, {});
}

TEST(ut_compatibility_routing_info_command, entry_without_address) {
    routing_info_entry_data entry;
    entry.type_ = routing_info_entry_type_e::RIE_DELETE_SERVICE_INSTANCE;
    entry.client_ = 0x4242;
    entry.services_.push_back({0x1111, 0x0001, 0x02, 0x00000003});
    expect_wire_equal(0x1234, {entry});
}

TEST(ut_compatibility_routing_info_command, entry_with_v4_address) {
    routing_info_entry_data entry;
    entry.type_ = routing_info_entry_type_e::RIE_ADD_SERVICE_INSTANCE;
    entry.client_ = 0x0001;
    entry.address_ = boost::asio::ip::make_address_v4("192.168.0.10");
    entry.port_ = 30509;
    entry.services_.push_back({0x1234, 0x0001, 0x01, 0x00000000});
    expect_wire_equal(0x0001, {entry});
}

TEST(ut_compatibility_routing_info_command, multiple_services_and_entries) {
    routing_info_entry_data entry_a;
    entry_a.type_ = routing_info_entry_type_e::RIE_ADD_SERVICE_INSTANCE;
    entry_a.client_ = 0x0010;
    entry_a.address_ = boost::asio::ip::make_address_v4("10.0.0.1");
    entry_a.port_ = 30000;
    entry_a.services_.push_back({0x0001, 0x0001, 0x01, 0x00000001});
    entry_a.services_.push_back({0x0002, 0x0002, 0x02, 0x00000002});

    routing_info_entry_data entry_b;
    entry_b.type_ = routing_info_entry_type_e::RIE_DELETE_SERVICE_INSTANCE;
    entry_b.client_ = 0x0020;
    entry_b.services_.push_back({0x0003, 0x0003, 0x03, 0x00000003});

    expect_wire_equal(0x0001, {entry_a, entry_b});
}

// Old serialize -> new deserialize yields the original logical content.
TEST(ut_compatibility_routing_info_command, old_serialize_new_deserialize) {
    routing_info_entry_data entry_a;
    entry_a.type_ = routing_info_entry_type_e::RIE_ADD_SERVICE_INSTANCE;
    entry_a.client_ = 0x0010;
    entry_a.address_ = boost::asio::ip::make_address_v4("10.0.0.1");
    entry_a.port_ = 30000;
    entry_a.services_.push_back({0x0001, 0x0001, 0x01, 0x00000001});

    routing_info_entry_data entry_b;
    entry_b.type_ = routing_info_entry_type_e::RIE_DELETE_SERVICE_INSTANCE;
    entry_b.client_ = 0x0020;
    entry_b.services_.push_back({0x0003, 0x0003, 0x03, 0x00000003});

    std::vector<routing_info_entry_data> entries{entry_a, entry_b};

    auto old_wire = old_to_wire(make_old_cmd(0x0001, entries));

    command_header hdr{};
    auto const hdr_size = deserialize(hdr, old_wire.data(), static_cast<uint32_t>(old_wire.size()));
    ASSERT_GT(hdr_size, 0u);
    EXPECT_EQ(hdr.id_, id_e::ROUTING_INFO_ID);

    std::vector<routing_info_entry_data> out_entries;
    ASSERT_GT(deserialize(out_entries, old_wire.data() + hdr_size, static_cast<uint32_t>(old_wire.size()) - hdr_size), 0u);
    EXPECT_EQ(out_entries, entries);
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
