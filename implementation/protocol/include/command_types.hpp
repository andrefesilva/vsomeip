// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <vsomeip/primitive_types.hpp>

#include "protocol.hpp"

#include <cstring>
#include <type_traits>

namespace vsomeip_v3::protocol {

struct command_header {
    auto operator<=>(command_header const&) const = default;

    static command_header create(id_e _id, uint32_t _length, client_t _client) {
        return {.id_ = _id, .version_ = IPC_VERSION, .client_ = _client, .length_ = _length};
    }

    id_e id_;
    version_t version_;
    client_t client_; // Sender's client identifier.
    uint32_t length_; // Payload length (bytes following the header).
};

struct simple_command_data {
    auto operator<=>(simple_command_data const&) const = default;
    static simple_command_data create(id_e _id, client_t _client) { return {.header_ = command_header::create(_id, 0, _client)}; }

    command_header header_;
};

struct service_data {
    auto operator<=>(service_data const&) const = default;

    service_t service_;
    instance_t instance_;
    major_version_t major_version_;
    minor_version_t minor_version_;
};

struct service_command_data {
    auto operator<=>(service_command_data const&) const = default;

    command_header header_;
    service_data payload_;
};

template<typename T>
struct single_field_command_data {
    auto operator<=>(single_field_command_data const&) const = default;

    command_header header_;
    T payload_;
};

// Wire-size helpers (sum of field sizes, no padding)
constexpr uint32_t wire_size(command_header const&) {
    return sizeof(id_e) + sizeof(version_t) + sizeof(client_t) + sizeof(uint32_t);
}

constexpr uint32_t wire_size(service_data const&) {
    return sizeof(service_t) + sizeof(instance_t) + sizeof(major_version_t) + sizeof(minor_version_t);
}

template<typename T>
uint32_t wire_size(T const& _in) {
    return wire_size(_in.header_) + _in.header_.length_;
}

// Factory helpers
inline service_command_data create_service_cmd(id_e _id, client_t _client, service_t _service, instance_t _instance, major_version_t _major,
                                               minor_version_t _minor) {
    return {.header_ = command_header::create(_id, wire_size(service_data{}), _client),
            .payload_ = {.service_ = _service, .instance_ = _instance, .major_version_ = _major, .minor_version_ = _minor}};
}

template<typename T>
inline single_field_command_data<T> create_single_field_cmd(id_e _id, client_t _client, T const& _field) {
    return {.header_ = command_header::create(_id, sizeof(T), _client), .payload_ = _field};
}

// Factory functions
inline simple_command_data create_ping_cmd(client_t _client) {
    return simple_command_data::create(id_e::PING_ID, _client);
}

inline simple_command_data create_pong_cmd(client_t _client) {
    return simple_command_data::create(id_e::PONG_ID, _client);
}

inline simple_command_data create_suspend_cmd(client_t _client) {
    return simple_command_data::create(id_e::SUSPEND_ID, _client);
}

inline service_command_data create_offer_service_cmd(client_t _client, service_t _service, instance_t _instance, major_version_t _major,
                                                     minor_version_t _minor) {
    return create_service_cmd(id_e::OFFER_SERVICE_ID, _client, _service, _instance, _major, _minor);
}

inline service_command_data create_stop_offer_service_cmd(client_t _client, service_t _service, instance_t _instance,
                                                          major_version_t _major, minor_version_t _minor) {
    return create_service_cmd(id_e::STOP_OFFER_SERVICE_ID, _client, _service, _instance, _major, _minor);
}

inline auto create_offered_services_request_cmd(client_t _client, offer_type_e _offer_type) {
    return create_single_field_cmd(id_e::OFFERED_SERVICES_REQUEST_ID, _client, _offer_type);
}

inline auto create_resend_provided_events_cmd(client_t _client, pending_remote_offer_id_t _id) {
    return create_single_field_cmd(id_e::RESEND_PROVIDED_EVENTS_ID, _client, _id);
}

inline auto create_assign_client_ack_cmd(client_t _client, client_t _assigned_id) {
    return create_single_field_cmd(id_e::ASSIGN_CLIENT_ACK_ID, _client, _assigned_id);
}

} // namespace vsomeip_v3::protocol
