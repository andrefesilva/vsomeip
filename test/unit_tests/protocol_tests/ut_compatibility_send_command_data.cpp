// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Send Command Compatibility Tests (header + service_data payload)
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
#include "../../../implementation/message/include/message_impl.hpp"
#include "../../../implementation/message/include/payload_impl.hpp"
#include "../../../implementation/message/include/serializer.hpp"

#include "../../../implementation/protocol/include/send_command.hpp"

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

TEST(ut_compatibility_send_id, send_id) {
    auto input = std::make_shared<message_impl>();
    input->set_service(0x2);
    input->set_instance(0x3);
    input->set_method(0x4);
    input->set_client(0x5);
    input->set_session(0x6);
    input->set_protocol_version(0x7);
    input->set_interface_version(0x8);
    input->set_message_type(message_type_e::MT_REQUEST);
    input->set_return_code(return_code_e::E_NOT_REACHABLE);
    input->set_reliable(true);
    input->set_initial(true);
    input->set_payload(std::make_shared<payload_impl>(std::vector<unsigned char>{0xc, 0xa, 0xf, 0xe}));
    input->set_check_result(0x9);

    client_t notify_target = input->get_client() + 2;

    client_t client = 0x1;
    auto cmd = protocol::create_send_cmd(id_e::SEND_ID, client, input, notify_target);
    send_command old_cmd{id_e::SEND_ID};
    old_cmd.set_client(client);
    old_cmd.set_instance(input->get_instance());
    old_cmd.set_reliable(input->is_reliable());
    old_cmd.set_status(input->get_check_result());
    old_cmd.set_target(notify_target);

    auto serializer = std::make_shared<vsomeip_v3::serializer>(500);
    serializer->serialize(input.get());
    std::vector<unsigned char> data{serializer->get_data(), serializer->get_data() + serializer->get_size()};
    old_cmd.set_message(data);

    auto new_wire = to_wire(cmd);
    auto old_wire = old_to_wire(old_cmd);

    EXPECT_EQ(new_wire, old_wire);
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
