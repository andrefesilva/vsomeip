// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "command_types.hpp"

#include <cstring> // memcpy
#include <iterator>
#include <iostream>
namespace vsomeip_v3::protocol {

template<typename T>
concept has_header = requires(T t) { t.header_; };
template<typename T>
concept has_payload = requires(T t) { t.payload_; };
template<typename T>
concept is_serializable_range = requires(T const& t) {
    std::begin(t);
    std::end(t);
};

static_assert(has_header<service_command_data>);
static_assert(has_payload<service_command_data>);
static_assert(has_header<single_field_command_data<offer_type_e>>);
static_assert(has_payload<single_field_command_data<offer_type_e>>);

// Forward declaration so write_fields can call serialize in its fold expression.
template<typename T>
uint32_t serialize(T const& _value, unsigned char* _mem);

template<typename T>
uint32_t write_field(unsigned char* _mem, T const& _value) {
    std::memcpy(_mem, &_value, sizeof(T));
    return sizeof(T);
}

template<typename... Ts>
uint32_t write_fields(unsigned char* _mem, Ts const&... _fields) {
    uint32_t written{0};
    ((written += serialize(_fields, _mem + written)), ...);
    return written;
}

template<typename T>
uint32_t serialize(T const& _value, unsigned char* _mem) {
    if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        std::memcpy(_mem, &_value, sizeof(T));
        return sizeof(T);
    } else if constexpr (std::is_same_v<T, command_header>) {
        return write_fields(_mem, _value.id_, _value.version_, _value.client_, _value.length_);
    } else if constexpr (std::is_same_v<T, service_data>) {
        return write_fields(_mem, _value.service_, _value.instance_, _value.major_version_, _value.minor_version_);
    } else if constexpr (std::is_same_v<T, release_service_data>) {
        return write_fields(_mem, _value.service_, _value.instance_);
    } else if constexpr (std::is_same_v<T, unregister_event_data>) {
        return write_fields(_mem, _value.service_, _value.instance_, _value.event_, _value.is_provided_);
    } else if constexpr (std::is_same_v<T, unsubscribe_ack_data>) {
        return write_fields(_mem, _value.service_, _value.instance_, _value.eventgroup_, _value.pending_id_);
    } else if constexpr (std::is_same_v<T, remove_security_policy_data>) {
        return write_fields(_mem, _value.update_id_, _value.uid_, _value.gid_);
    } else if constexpr (is_serializable_range<T>) {
        // covers span, vector, set, map etc..
        uint32_t written = 0;
        for (auto const& v : _value) {
            written += serialize(v, _mem + written);
        }
        return written;
    } else if constexpr (has_header<T>) {
        if constexpr (has_payload<T>) {
            // non-trivial commands -> recurse!
            return write_fields(_mem, _value.header_, _value.payload_);
        } else {
            // simple commands (PING, PONG, SUSPEND)
            return serialize(_value.header_, _mem);
        }
    } else {
        static_assert(!std::is_same_v<T, T>, "Unsupported type requested to be serialized");
    }
    return 0;
}

} // namespace vsomeip_v3::protocol
