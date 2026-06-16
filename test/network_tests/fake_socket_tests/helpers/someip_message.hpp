// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../../../../implementation/service_discovery/include/message_impl.hpp"
#include "../../../../implementation/message/include/message_impl.hpp"
#include "service_state.hpp"

#include <vsomeip/vsomeip.hpp>
#include "to_string.hpp"
#include "message_common.hpp"

#include <iomanip>
#include <ostream>

namespace vsomeip_v3::testing {
struct someip_message {
    std::shared_ptr<message_impl> msg_;
    std::shared_ptr<sd::message_impl> sd_;
};

struct someip_sd_record_message {
    sd::entry_type_e id_;
    ttl_t ttl_;

    bool operator==(const someip_sd_record_message& _other) const { return id_ == _other.id_ && ttl_ == _other.ttl_; }
};

/// Lightweight struct capturing the header fields of a SOME/IP message for recording.
/// Used with attribute_recorder to allow tests to assert on boardnet SOME/IP traffic.
struct someip_record_message {
    /// Wildcard sentinels for client_ / session_: when either side of a comparison holds
    /// these, that field is ignored and matches any value. Using dedicated sentinels
    /// (rather than 0) keeps 0 usable as a real value one might want to test against.
    static constexpr client_t ANY_CLIENT = 0xFFFF;
    static constexpr session_t ANY_SESSION = 0xFFFF;

    service_t service_{};
    method_t method_{};
    client_t client_{ANY_CLIENT};
    session_t session_{ANY_SESSION};
    message_type_e message_type_{message_type_e::MT_UNKNOWN};
    return_code_e return_code_{return_code_e::E_UNKNOWN};

    /// Equality comparison. ANY_CLIENT / ANY_SESSION act as wildcards matching any value.
    [[nodiscard]] bool operator==(someip_record_message const& _other) const {
        if (service_ != _other.service_ || method_ != _other.method_) {
            return false;
        }
        if (client_ != ANY_CLIENT && _other.client_ != ANY_CLIENT && client_ != _other.client_) {
            return false;
        }
        if (session_ != ANY_SESSION && _other.session_ != ANY_SESSION && session_ != _other.session_) {
            return false;
        }
        return message_type_ == _other.message_type_ && return_code_ == _other.return_code_;
    }
    [[nodiscard]] bool operator!=(someip_record_message const& _other) const { return !(*this == _other); }
};

inline std::ostream& operator<<(std::ostream& _out, someip_record_message const& _m) {
    _out << "{service=0x" << std::hex << std::setfill('0') << std::setw(4) << _m.service_ << " method=0x" << std::setw(4) << _m.method_
         << " client=0x" << std::setw(4) << _m.client_ << " session=0x" << std::setw(4) << _m.session_ << std::dec
         << " type=" << static_cast<int>(_m.message_type_) << " rc=" << static_cast<int>(_m.return_code_) << "}";
    return _out;
}

[[nodiscard]] size_t parse(std::vector<unsigned char>& message, someip_message& _out_message);
[[nodiscard]] size_t parse_sequential_someip(unsigned char* _message, size_t _message_size, someip_message& _out_message);
[[nodiscard]] std::shared_ptr<vsomeip_v3::sd::message_impl> parse_sd(std::vector<unsigned char>& _message);
std::vector<unsigned char> construct_subscription(event_ids const& _subscription, boost::asio::ip::address _address, uint16_t _port);
std::vector<unsigned char> construct_offer(event_ids const& _offer, boost::asio::ip::address _address, uint16_t _port);

std::ostream& operator<<(std::ostream& _out, someip_message const& _m);

/**
 * @brief Constructs someip message, example:
 * construct_someip_raw_message(static_cast<uint16_t>(service), static_cast<uint16_t>(method), static_cast<uint32_t>(length),
                               static_cast<uint16_t>(client), static_cast<uint16_t>(session), static_cast<uint8_t>(protocol),
                               static_cast<uint8_t>(interface), static_cast<uint8_t>(message_type), static_cast<uint8_t>(return_code));
 *
 * @param payload All fields needed to construct the message.
 * @return std::vector<unsigned char> Constructed raw message.
 */
template<typename... Ts>
std::vector<unsigned char> construct_someip_raw_message(Ts&&... payload) {
    std::vector<unsigned char> message;
    (..., ([&] {
         using U = std::decay_t<decltype(payload)>;
         if constexpr (std::is_same_v<U, std::string> || is_std_vector<U>::value) {
             message.insert(message.end(), payload.begin(), payload.end());
         } else if constexpr (std::is_convertible_v<U, const char*>) {
             std::string s(payload);
             message.insert(message.end(), s.begin(), s.end());
         } else {
             // Byte-swap into big-endian order.
             auto bytes = reinterpret_cast<unsigned char*>(&payload);
             for (std::size_t i = sizeof(payload); i > 0; --i) {
                 message.push_back(bytes[i - 1]);
             }
         }
     }()));
    return message;
}
}
