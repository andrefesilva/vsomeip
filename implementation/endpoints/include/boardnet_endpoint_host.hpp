// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <memory>

#include <boost/asio/ip/address.hpp>

#include <vsomeip/primitive_types.hpp>

#include "internal.hpp"

namespace vsomeip_v3 {

class configuration;
class boardnet_endpoint;

/// Why a client endpoint reports that it can no longer talk to its remote (see boardnet_endpoint_host::on_disconnect()).
enum class disconnect_reason_e : uint8_t {
    /// A connect attempt failed; nothing had been established yet.
    CONNECT_FAILED,
    /// The endpoint restarts its connection to resync after broken or oversized data on the stream.
    RESTART,
    /// An established connection broke: receive/send error on a connected socket, or a send that never completed.
    CONNECTION_LOST
};

inline const char* to_string(disconnect_reason_e _reason) {
    switch (_reason) {
    case disconnect_reason_e::CONNECT_FAILED:
        return "connect attempt failed";
    case disconnect_reason_e::RESTART:
        return "endpoint restarted its connection";
    case disconnect_reason_e::CONNECTION_LOST:
        return "established connection broke";
    }
    return "unknown";
}

class boardnet_endpoint_host {
public:
    virtual ~boardnet_endpoint_host() = default;

    virtual void on_connect(std::shared_ptr<boardnet_endpoint> _endpoint) = 0;
    /**
     * @brief Called by a client endpoint when it can no longer talk to its remote.
     *
     * @param _reason why the endpoint lost contact with its remote, see disconnect_reason_e.
     */
    virtual void on_disconnect(std::shared_ptr<boardnet_endpoint> _endpoint, disconnect_reason_e _reason) = 0;
    virtual bool on_bind_error(std::shared_ptr<boardnet_endpoint> _endpoint, const boost::asio::ip::address& _remote_address,
                               uint16_t _remote_port, uint16_t& _local_port) = 0;
    /**
     * @brief Find instance for a given service+endpoint pair
     *
     * @return instance_t the instance id, or ANY_INSTANCE if not found
     */
    virtual instance_t find_instance(service_t _service, major_version_t _major, boardnet_endpoint* const _endpoint) const = 0;
};

} // namespace vsomeip_v3
