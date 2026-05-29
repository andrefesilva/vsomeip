// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "command_types.hpp"

#include <cstring> // memcpy

namespace vsomeip_v3::protocol {

template<typename T>
uint32_t write_field(unsigned char* _mem, T const& _value) {
    std::memcpy(_mem, &_value, sizeof(T));
    return sizeof(T);
}

template<typename... Ts>
uint32_t write_fields(unsigned char* _mem, Ts const&... _fields) {
    uint32_t written{0};
    ((written += write_field(_mem + written, _fields)), ...);
    return written;
}

inline uint32_t serialize(command_header const& _in, unsigned char* _mem) {
    return write_fields(_mem, _in.id_, _in.version_, _in.client_, _in.length_);
}

inline uint32_t serialize(service_command_data const& _in, unsigned char* _mem) {
    return write_fields(_mem, _in.header_.id_, _in.header_.version_, _in.header_.client_, _in.header_.length_, _in.service_data_.service_,
                        _in.service_data_.instance_, _in.service_data_.major_version_, _in.service_data_.minor_version_);
}

// Generic serialize for header-only composite commands (ping, pong, suspend, etc.).
// Non-template overloads (e.g. service_command_data) are preferred by overload resolution.
template<typename T>
    requires requires(T const& _t) { _t.header_; }
uint32_t serialize(T const& _in, unsigned char* _mem) {
    return serialize(_in.header_, _mem);
}

} // namespace vsomeip_v3::protocol
