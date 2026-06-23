// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "command_types.hpp"
#include "../../message/include/message_impl.hpp"
#include "../../message/include/payload_impl.hpp"
#include "vsomeip/defines.hpp"
#include "vsomeip/enumeration_types.hpp"
#include "vsomeip/primitive_types.hpp"

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

//! This function does not check the memory size, as this should be covered by an earlier check from the caller already
template<typename T>
inline uint32_t deserialize_be(T& _out, unsigned char const* _mem) {
    if constexpr (std::is_same_v<T, uint8_t>) {
        _out = _mem[0];
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        _out = static_cast<uint16_t>(_mem[1] | (static_cast<uint16_t>(_mem[0]) << 8));
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        _out = static_cast<uint32_t>(_mem[3] | (static_cast<uint32_t>(_mem[2]) << 8) | (static_cast<uint32_t>(_mem[1]) << 16)
                                     | (static_cast<uint32_t>(_mem[0]) << 24));
    } else {
        static_assert(!std::is_same_v<T, T>, "unsupported type passed in");
    }
    return sizeof(T);
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

inline uint32_t deserialize(std::vector<service_data>& _out, unsigned char const* _mem, uint32_t _size) {
    uint32_t acc = 0;
    auto elems = _size / service_data::wire_size_;
    if (elems * service_data::wire_size_ != _size) {
        // some remaining bytes?
        return 0;
    }
    _out.resize(elems);
    for (auto& out : _out) {
        // there is no need to check for the amount of parsed bytes,
        // as we only allocated enough space to also deserialize the payload
        acc += deserialize(out, _mem + acc, _size - acc);
    }
    return acc;
}

inline uint32_t deserialize(ipc_message_header& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.instance_, _out.reliable_, _out.status_, _out.target_);
}

inline uint32_t deserialize(release_service_data& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.service_, _out.instance_);
}

inline uint32_t deserialize(unregister_event_data& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.service_, _out.instance_, _out.event_, _out.is_provided_);
}

inline uint32_t deserialize(unsubscribe_ack_data& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.service_, _out.instance_, _out.eventgroup_, _out.pending_id_);
}

inline uint32_t deserialize(remove_security_policy_data& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.update_id_, _out.uid_, _out.gid_);
}
inline uint32_t deserialize(subscribe_answer_data& _out, unsigned char const* _mem, uint32_t _size) {
    return parse(_mem, _size, _out.service_, _out.instance_, _out.eventgroup_, _out.subscriber_, _out.event_, _out.pending_id_);
}

inline uint32_t deserialize(routing_info_entry_data& _out, unsigned char const* _mem, uint32_t _size) {
    // Wire layout: type (1) | entry-size (4) | client-info-size (4) | client (2)
    //              [ IPv4 address (4) | port (2) ] | services-size (4) | N * service (wire_size_)
    byte_t its_type{0};
    uint32_t its_entry_size{0};
    uint32_t its_client_size{0};
    uint32_t parsed = parse(_mem, _size, its_type, its_entry_size, its_client_size, _out.client_);
    if (parsed == 0) {
        return 0;
    }

    _out.type_ = static_cast<routing_info_entry_type_e>(its_type);
    if (_out.type_ == routing_info_entry_type_e::RIE_UNKNOWN) {
        return 0;
    }

    if (_out.type_ != routing_info_entry_type_e::RIE_ADD_SERVICE_INSTANCE
        && _out.type_ != routing_info_entry_type_e::RIE_DELETE_SERVICE_INSTANCE) {
        return 0;
    }

    // The address and port are optional; they are present iff the client-info section is
    // larger than the client id. Only IPv4 addresses are supported.
    if (its_client_size > sizeof(client_t)) {
        boost::asio::ip::address_v4::bytes_type its_address;
        if (its_client_size != sizeof(client_t) + its_address.size() + sizeof(port_t) || _size - parsed < its_address.size()) {
            return 0;
        }
        std::memcpy(its_address.data(), _mem + parsed, its_address.size());
        _out.address_ = boost::asio::ip::address_v4(its_address);
        parsed += static_cast<uint32_t>(its_address.size());

        uint32_t const consumed = parse(_mem + parsed, _size - parsed, _out.port_);
        if (consumed == 0) {
            return 0;
        }
        parsed += consumed;
    }

    uint32_t its_services_size{0};
    uint32_t consumed = parse(_mem + parsed, _size - parsed, its_services_size);
    if (consumed == 0) {
        return 0;
    }
    parsed += consumed;

    if (_size - parsed < its_services_size) {
        return 0;
    }

    // Reuse the std::vector<service_data> overload; it validates that the section is an
    // exact multiple of service_data::wire_size_ and returns 0 otherwise.
    consumed = deserialize(_out.services_, _mem + parsed, its_services_size);
    if (consumed != its_services_size) {
        return 0;
    }
    parsed += consumed;

    return parsed;
}

inline uint32_t deserialize(std::vector<routing_info_entry_data>& _out, unsigned char const* _mem, uint32_t _size) {
    uint32_t acc = 0;
    while (acc < _size) {
        routing_info_entry_data its_entry;
        uint32_t const consumed = deserialize(its_entry, _mem + acc, _size - acc);
        if (consumed == 0) {
            return 0;
        }
        _out.emplace_back(std::move(its_entry));
        acc += consumed;
    }
    return acc;
}

inline uint32_t deserialize(std::shared_ptr<message_impl>& _out, unsigned char const* _mem, uint32_t _size) {
    if (_size < ipc_message_header::wire_size_ + VSOMEIP_FULL_HEADER_SIZE) {
        return 0;
    }

    // first we need to add our own auxiliary data
    _out = std::make_shared<message_impl>();
    ipc_message_header ipc_header;
    uint32_t its_offset = deserialize(ipc_header, _mem, _size);
    _out->set_instance(ipc_header.instance_);
    _out->set_reliable(ipc_header.reliable_);
    _out->set_check_result(ipc_header.status_);
    // the "target" of the former "command" was only of interest in the rmc
    // in which only the ipc_header is read. In the rmc reading the whole
    // message the client of the message is the one of importance.

    // now comes the "official" someip message part
    service_t service;
    its_offset += deserialize_be(service, _mem + its_offset);
    _out->set_service(service);

    method_t method;
    its_offset += deserialize_be(method, _mem + its_offset);
    _out->set_method(method);

    length_t length;
    its_offset += deserialize_be(length, _mem + its_offset);
    // length is derived from the payload, no need to set it explicitly
    if (length != _size - its_offset) {
        // this implies that the command header total size does not match the nested someip message size
        // --> Something is wrong with the message
        return 0;
    }

    client_t target;
    its_offset += deserialize_be(target, _mem + its_offset);
    _out->set_client(target);

    session_t session;
    its_offset += deserialize_be(session, _mem + its_offset);
    _out->set_session(session);

    protocol_version_t pv;
    its_offset += deserialize_be(pv, _mem + its_offset);
    _out->set_protocol_version(pv);

    interface_version_t iv;
    its_offset += deserialize_be(iv, _mem + its_offset);
    _out->set_interface_version(iv);

    uint8_t message_type;
    its_offset += deserialize_be(message_type, _mem + its_offset);
    _out->set_message_type(static_cast<message_type_e>(message_type));

    uint8_t return_code;
    its_offset += deserialize_be(return_code, _mem + its_offset);
    _out->set_return_code(static_cast<return_code_e>(return_code));

    auto const payload_size = _size - its_offset;
    if (payload_size > 0) {
        std::vector<unsigned char> message(payload_size);
        std::memcpy(message.data(), &_mem[its_offset], payload_size);
        auto payload = std::make_shared<payload_impl>();
        payload->set_data(std::move(message));
        _out->set_payload(std::move(payload));
    }

    // due to the last guard ensuring that the length of the someip message + the ipc header size
    // match _size + the assumed payload size we can savely return the _size
    return _size;
}

} // namespace vsomeip_v3::protocol
