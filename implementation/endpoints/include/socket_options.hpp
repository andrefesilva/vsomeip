// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#if defined(__linux__) || defined(__QNX__)

#pragma once

#include <boost/asio/ip/udp.hpp>

#include <string>

#include <sys/time.h>

namespace vsomeip_v3 {

template<typename Protocol>
struct bind_to_device {
    // The size has a +1 since std::string.size does not take into account the null terminator
    explicit bind_to_device(std::string const& _device) : data_(static_cast<const void*>(_device.data())), size_(_device.size() + 1) { }
    static int level(Protocol const&) { return SOL_SOCKET; };
    static int name(Protocol const&) { return SO_BINDTODEVICE; };
    const void* data(Protocol const&) const { return data_; }
    size_t size(Protocol const&) const { return size_; }

    const void* data_;
    size_t size_;
};

template<typename Protocol>
struct receive_buffer_force {
    static int level(Protocol const&) { return SOL_SOCKET; };
    static int name(Protocol const&) { return SO_RCVBUFFORCE; };
    const void* data(Protocol const&) const { return static_cast<const void*>(&size_); }
    size_t size(Protocol const&) const { return sizeof(size_); }

    int size_{};
};

using udp_bind_to_device = bind_to_device<boost::asio::ip::udp::endpoint::protocol_type>;
using udp_receive_buffer_force = receive_buffer_force<boost::asio::ip::udp::endpoint::protocol_type>;

}
#endif
