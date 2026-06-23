// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Round-trip tests for all struct-based protocol commands serialize/deserialize.
// ============================================================================

#include <gtest/gtest.h>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"
#include "../../../implementation/message/include/message_impl.hpp"
#include "../../../implementation/message/include/payload_impl.hpp"
#include "vsomeip/enumeration_types.hpp"

namespace vsomeip_v3::protocol {

template<typename Cmd>
std::vector<uint8_t> send(Cmd const& _input) {
    auto const size = wire_size(_input);
    std::vector<uint8_t> buf(size);
    serialize(_input, buf.data());
    return buf;
}
template<typename Cmd>
Cmd receive(std::vector<uint8_t> const& _buf) {
    Cmd out{};
    auto const size = static_cast<uint32_t>(_buf.size());
    auto const hdr_size = deserialize(out.header_, _buf.data(), size);
    if (hdr_size == 0) {
        return {};
    }
    if constexpr (has_payload<Cmd>) {
        if (0 == deserialize(out.payload_, _buf.data() + hdr_size, size - hdr_size)) {
            return {};
        }
    }
    return out;
}

template<typename Cmd>
auto roundtrip(Cmd const& _input) {
    if constexpr (std::is_same_v<multiple_service_command_data, Cmd>) {
        // this command owns the data only on reception, not on sending
        std::pair<command_header, std::vector<protocol::service_data>> out;
        auto buf = send(_input);
        auto const size = static_cast<uint32_t>(buf.size());
        auto const hdr_size = deserialize(out.first, buf.data(), size);
        if (hdr_size == 0) {
            return out;
        }
        deserialize(out.second, buf.data() + hdr_size, size - hdr_size);
        return out;
    } else if constexpr (std::is_same_v<register_events_command_data, Cmd>) {
        // this command owns the data only on reception, not on sending
        std::pair<command_header, std::vector<protocol::register_event_data>> out;
        auto buf = send(_input);
        auto const size = static_cast<uint32_t>(buf.size());
        auto const hdr_size = deserialize(out.first, buf.data(), size);
        if (hdr_size == 0) {
            return out;
        }
        deserialize(out.second, buf.data() + hdr_size, size - hdr_size);
        return out;
    } else {
        return receive<Cmd>(send(_input));
    }
}

// --- Simple (header-only) commands ---

TEST(ut_commands_roundtrip, ping_roundtrip) {
    EXPECT_EQ(roundtrip(create_ping_cmd(0x1234)), create_ping_cmd(0x1234));
}

TEST(ut_commands_roundtrip, pong_roundtrip) {
    EXPECT_EQ(roundtrip(create_pong_cmd(0xABCD)), create_pong_cmd(0xABCD));
}

TEST(ut_commands_roundtrip, suspend_roundtrip) {
    EXPECT_EQ(roundtrip(create_suspend_cmd(0x00FF)), create_suspend_cmd(0x00FF));
}

// --- Service commands ---

TEST(ut_commands_roundtrip, offer_service_roundtrip) {
    auto cmd = create_offer_service_cmd(0x1234, 0xABCD, 0x0001, 0x02, 0x00000003);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, stop_offer_service_roundtrip) {
    auto cmd = create_stop_offer_service_cmd(0x5678, 0x1111, 0x2222, 0x01, 0x00000000);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

// --- Single-field commands ---

TEST(ut_commands_roundtrip, assign_client_ack_roundtrip) {
    auto cmd = create_assign_client_ack_cmd(0x0000, 0x04CF);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, offered_services_request_roundtrip) {
    auto cmd = create_offered_services_request_cmd(0x1234, offer_type_e::OT_ALL);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, resend_provided_events_roundtrip) {
    auto cmd = create_resend_provided_events_cmd(0x00FF, 0x0000ABCD);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

// --- Service commands (composite payload) ---

TEST(ut_commands_roundtrip, release_service_roundtrip) {
    auto cmd = create_release_service_cmd(0x1234, 0xABCD, 0x0001);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, unregister_event_roundtrip) {
    auto cmd = create_unregister_event_cmd(0x1234, 0xABCD, 0x0001, 0x4242, true);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, unsubscribe_ack_roundtrip) {
    auto cmd = create_unsubscribe_ack_cmd(0x1234, 0xABCD, 0x0001, 0x0005, 0x1111);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, remove_security_policy_roundtrip) {
    auto cmd = create_remove_security_policy_cmd(0x1234, 0x00ABCDEF, 0x000003E8, 0x000003E9);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, update_security_policy_response_roundtrip) {
    auto cmd = create_update_security_policy_response_cmd(0x1234, 0x0000BEEF);
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, remove_security_policy_response_roundtrip) {
    auto cmd = create_remove_security_policy_response_cmd(0x1234, 0x0000BEEF);
    EXPECT_EQ(roundtrip(cmd), cmd);
}
TEST(ut_commands_roundtrip, subscribe_ack_command) {
    auto cmd = create_subscribe_ack_cmd(
            0x1234, {.service_ = 0x98, .instance_ = 0x97, .eventgroup_ = 0x96, .subscriber_ = 0x95, .event_ = 0x94, .pending_id_ = 0x93});
    EXPECT_EQ(roundtrip(cmd), cmd);
}
TEST(ut_commands_roundtrip, subscribe_nack_command) {
    auto cmd = create_subscribe_nack_cmd(
            0x1234,
            {.service_ = 0x121, .instance_ = 0x122, .eventgroup_ = 0x123, .subscriber_ = 0x124, .event_ = 0x125, .pending_id_ = 0x126});
    EXPECT_EQ(roundtrip(cmd), cmd);
}

// --- Non-owning commands
TEST(ut_commands_roundtrip, request_service_command) {
    std::vector<protocol::service_data> no{};
    std::vector<protocol::service_data> one{
            {.service_ = 0x12, .instance_ = 1, .major_version_ = DEFAULT_MAJOR, .minor_version_ = ANY_MINOR}};
    std::vector<protocol::service_data> many{{.service_ = 0xa, .instance_ = 2, .major_version_ = 1, .minor_version_ = 0},
                                             {.service_ = 0xb, .instance_ = 3, .major_version_ = 2, .minor_version_ = 1}};

    for (auto const& payload : {no, one, many}) {
        auto cmd = create_request_service_cmd(0x1, payload);
        auto [header, out_payload] = roundtrip(cmd);
        EXPECT_EQ(header, cmd.header_);
        EXPECT_EQ(out_payload, payload);
    }
}

TEST(ut_commands_roundtrip, offered_services_response_cmd) {
    std::vector<protocol::service_data> no{};
    std::vector<protocol::service_data> one{
            {.service_ = 0x21, .instance_ = 2, .major_version_ = ANY_MAJOR, .minor_version_ = DEFAULT_MINOR}};
    std::vector<protocol::service_data> many{{.service_ = 0xc, .instance_ = 1, .major_version_ = 1, .minor_version_ = 0},
                                             {.service_ = 0xd, .instance_ = 2, .major_version_ = 2, .minor_version_ = 1}};

    for (auto const& payload : {no, one, many}) {
        auto cmd = create_offered_services_response_cmd(0x1, payload);
        auto [header, out_payload] = roundtrip(cmd);
        EXPECT_EQ(header, cmd.header_);
        EXPECT_EQ(out_payload, payload);
    }
}

TEST(ut_commands_roundtrip, register_events_command) {
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
        auto cmd = create_register_events_cmd(0x1, payload);
        auto [header, out_payload] = roundtrip(cmd);
        EXPECT_EQ(header, cmd.header_);
        EXPECT_EQ(out_payload, payload);
    }
}
// send command
TEST(ut_commands_roundtrip, send_id_command) {
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
    input->set_payload(std::make_shared<payload_impl>(std::vector<unsigned char>{0x1, 0x2, 0x3}));
    input->set_check_result(0x9);
    client_t extra_client = input->get_client() + 2;

    auto cmd = create_send_cmd(id_e::SEND_ID, 0x01, input, extra_client);
    auto buf_data = send(cmd);

    // Partial deserialize: extract command header and IPC header, leaving raw SOME/IP bytes
    command_header parsed_hdr{};
    auto const hdr_bytes = deserialize(parsed_hdr, buf_data.data(), static_cast<uint32_t>(buf_data.size()));
    ASSERT_GT(hdr_bytes, 0u);

    ipc_message_header parsed_ipc{};
    auto const ipc_bytes = deserialize(parsed_ipc, buf_data.data() + hdr_bytes, static_cast<uint32_t>(buf_data.size()) - hdr_bytes);
    ASSERT_GT(ipc_bytes, 0u);

    auto const* raw_someip = buf_data.data() + hdr_bytes + ipc_bytes;
    auto const raw_someip_size = static_cast<uint32_t>(buf_data.size() - hdr_bytes - ipc_bytes);

    // Rebuild as send_command_raw and re-serialize
    auto cmd_raw = create_send_cmd_raw(id_e::SEND_ID, parsed_hdr.client_, raw_someip, raw_someip_size, parsed_ipc);
    auto buf_raw = send(cmd_raw);
    ASSERT_EQ(buf_data, buf_raw) << "raw and message-based paths must produce identical wire format";

    // Full deserialize from the raw-path buffer
    command_header out_hdr{};
    auto const out_hdr_bytes = deserialize(out_hdr, buf_raw.data(), static_cast<uint32_t>(buf_raw.size()));
    ASSERT_GT(out_hdr_bytes, 0u);

    std::shared_ptr<message_impl> out_msg;
    ASSERT_GT(deserialize(out_msg, buf_raw.data() + out_hdr_bytes, static_cast<uint32_t>(buf_raw.size()) - out_hdr_bytes), 0u);

    EXPECT_EQ(out_hdr.id_, id_e::SEND_ID);
    EXPECT_EQ(out_hdr.client_, 0x01);

    auto const& msg = out_msg;
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->get_service(), input->get_service());
    EXPECT_EQ(msg->get_instance(), input->get_instance());
    EXPECT_EQ(msg->get_method(), input->get_method());
    EXPECT_EQ(msg->get_client(), input->get_client());
    EXPECT_EQ(msg->get_session(), input->get_session());
    EXPECT_EQ(msg->get_protocol_version(), input->get_protocol_version());
    EXPECT_EQ(msg->get_interface_version(), input->get_interface_version());
    EXPECT_EQ(msg->get_message_type(), input->get_message_type());
    EXPECT_EQ(msg->get_return_code(), input->get_return_code());
    EXPECT_EQ(msg->is_reliable(), input->is_reliable());
    EXPECT_EQ(msg->get_check_result(), input->get_check_result());

    auto p = msg->get_payload();
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->get_length(), 3u);
    EXPECT_EQ(p->get_data()[0], 0x1);
    EXPECT_EQ(p->get_data()[1], 0x2);
    EXPECT_EQ(p->get_data()[2], 0x3);
}

// --- Error handling ---

TEST(ut_commands_roundtrip, deserialize_rejects_truncated_header) {
    auto cmd = create_ping_cmd(0x04CF);
    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header out{};
    EXPECT_FALSE(deserialize(out, buf.data(), static_cast<uint32_t>(buf.size()) - 1));
}

TEST(ut_commands_roundtrip, deserialize_rejects_truncated_payload) {
    auto cmd = create_resend_provided_events_cmd(0x04CF, 0x00001234);
    std::vector<uint8_t> buf(wire_size(cmd));
    serialize(cmd, buf.data());

    command_header hdr{};
    auto const hdr_size = deserialize(hdr, buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_GT(hdr_size, 0u);

    pending_remote_offer_id_t payload{};
    EXPECT_FALSE(deserialize(payload, buf.data() + hdr_size, static_cast<uint32_t>(buf.size()) - hdr_size - 1));
}

// --- Routing info (owning vector payload) ---

TEST(ut_commands_roundtrip, routing_info_empty) {
    auto cmd = create_routing_info_cmd(0x1234, {});
    auto buf = send(cmd);

    command_header hdr{};
    auto const hdr_size = deserialize(hdr, buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_GT(hdr_size, 0u);
    EXPECT_EQ(hdr, cmd.header_);

    std::vector<routing_info_entry_data> out;
    deserialize(out, buf.data() + hdr_size, static_cast<uint32_t>(buf.size()) - hdr_size);
    EXPECT_TRUE(out.empty());
}

TEST(ut_commands_roundtrip, routing_info_entry_without_address) {
    routing_info_entry_data entry;
    entry.type_ = routing_info_entry_type_e::RIE_DELETE_SERVICE_INSTANCE;
    entry.client_ = 0x4242;
    entry.services_.push_back({0x1111, 0x0001, 0x02, 0x00000003});

    auto cmd = create_routing_info_cmd(0x1234, {entry});
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, routing_info_entry_with_v4_address) {
    routing_info_entry_data entry;
    entry.type_ = routing_info_entry_type_e::RIE_ADD_SERVICE_INSTANCE;
    entry.client_ = 0x0001;
    entry.address_ = boost::asio::ip::make_address_v4("192.168.0.10");
    entry.port_ = 30509;
    entry.services_.push_back({0x1234, 0x0001, 0x01, 0x00000000});

    auto cmd = create_routing_info_cmd(0x0001, {entry});
    EXPECT_EQ(roundtrip(cmd), cmd);
}

TEST(ut_commands_roundtrip, routing_info_multiple_services_and_entries) {
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

    auto cmd = create_routing_info_cmd(0x0001, {entry_a, entry_b});
    EXPECT_EQ(roundtrip(cmd), cmd);
}

} // namespace vsomeip_v3::protocol
