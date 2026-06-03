// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "command_types.hpp"

#include <cstring> // memcpy
#include <optional>
#include <type_traits>

namespace vsomeip_v3::protocol {

template<typename T>
    requires(std::is_integral_v<T> || std::is_enum_v<T>)
inline uint32_t deserialize(T& _out, unsigned char const* _mem, uint32_t _size) {
    static constexpr auto size = sizeof(T);
    if (size > _size) {
        return 0;
    }
    std::memcpy(&_out, _mem, size);
    return size;
}

template<typename... Ts>
uint32_t parse(unsigned char const* _mem, uint32_t _size, Ts&... _ts) {
    uint32_t parsed{0};
    auto checked_parse = [&](auto& _out) {
        auto const result = deserialize(_out, _mem, _size);
        if (result == 0) {
            return false;
        }
        parsed += result;
        _mem += result;
        _size -= result;
        return true;
    };
    return (checked_parse(_ts) && ...) ? parsed : 0;
}

inline uint32_t deserialize(command_header& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.id_, _out.version_, _out.client_, _out.length_);
}

inline uint32_t deserialize(service_data& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.service_, _out.instance_, _out.major_version_, _out.minor_version_);
}

} // namespace vsomeip_v3::protocol
