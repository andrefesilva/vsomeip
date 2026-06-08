// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Service Command Compatibility Tests (header + service_data payload)
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

#include "../../../implementation/protocol/include/request_service_command.hpp"
#include "../../../implementation/protocol/include/offered_services_response_command.hpp"

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

TEST(ut_compatibility_service_commands, offered_services_response_cmd) {
    std::vector<protocol::service_data> no{};
    std::vector<protocol::service_data> one{
            {.service_ = 0x21, .instance_ = 2, .major_version_ = ANY_MAJOR, .minor_version_ = DEFAULT_MINOR}};
    std::vector<protocol::service_data> many{{.service_ = 0xc, .instance_ = 1, .major_version_ = 1, .minor_version_ = 0},
                                             {.service_ = 0xd, .instance_ = 2, .major_version_ = 2, .minor_version_ = 1}};

    for (auto const& payload : {no, one, many}) {
        client_t client = 0x1;
        auto cmd = create_offered_services_response_cmd(client, payload);
        offered_services_response_command old_cmd;
        old_cmd.set_client(client);
        for (auto const& input : payload) {
            old_cmd.add_service(protocol::service{input.service_, input.instance_, input.major_version_, input.minor_version_});
        }

        auto new_wire = to_wire(cmd);
        auto old_wire = old_to_wire(old_cmd);

        EXPECT_EQ(new_wire, old_wire);
    }
}

TEST(ut_compatibility_service_commands, request_service_command) {
    std::vector<protocol::service_data> no{};
    std::vector<protocol::service_data> one{
            {.service_ = 0x12, .instance_ = 1, .major_version_ = DEFAULT_MAJOR, .minor_version_ = ANY_MINOR}};
    std::vector<protocol::service_data> many{{.service_ = 0xa, .instance_ = 2, .major_version_ = 1, .minor_version_ = 0},
                                             {.service_ = 0xb, .instance_ = 3, .major_version_ = 2, .minor_version_ = 1}};

    for (auto const& payload : {no, one, many}) {
        client_t client = 0x1;
        auto cmd = create_request_service_cmd(client, payload);
        request_service_command old_cmd;
        old_cmd.set_client(client);
        for (auto const& input : payload) {
            old_cmd.add_service(protocol::service{input.service_, input.instance_, input.major_version_, input.minor_version_});
        }

        auto new_wire = to_wire(cmd);
        auto old_wire = old_to_wire(old_cmd);

        EXPECT_EQ(new_wire, old_wire);
    }
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
