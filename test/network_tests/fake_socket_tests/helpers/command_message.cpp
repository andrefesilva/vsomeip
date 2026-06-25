// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "command_message.hpp"

#include "../../../../implementation/protocol/include/deserialize.hpp"
#include "test_logging.hpp"
#include <cstdint>
#include <ostream>
#include <utility>
#include <iomanip>
#include <sstream>
#include <cstring> // memcpy
#include <vsomeip/payload.hpp>

namespace vsomeip_v3::testing {

[[nodiscard]] size_t parse(unsigned char const* _data, size_t _len, command_message& _out_message) {
    if (_len < protocol::COMMAND_POSITION_PAYLOAD) {
        TEST_LOG << "wire bytes were not long enough to contain the header";
        return 0;
    }

    auto handle_message = [&_out_message](uint8_t const* _begin, size_t _size) {
        command_message out;
        if (_size < protocol::COMMAND_POSITION_PAYLOAD) {
            TEST_LOG << "wire bytes were not long enough to contain the header";
            return false;
        }
        // ROUTING_INFO uses the struct-based codec, so it is parsed with the free-function
        // deserialize overloads.
        auto const deal_with_routing_info = [&] {
            protocol::routing_info_command_data its_command;
            auto const its_size = static_cast<uint32_t>(_size);
            uint32_t const its_header_size = protocol::deserialize(its_command.header_, _begin, its_size);
            if (its_header_size == 0) {
                return false;
            }
            if (its_command.header_.length_ > 0
                && protocol::deserialize(its_command.payload_, _begin + its_header_size, its_size - its_header_size) == 0) {
                return false;
            }
            out.id_ = its_command.header_.id_;
            out.client_id_ = its_command.header_.client_;
            out.payload_ = command_payload(std::move(its_command));
            return true;
        };

        // understand how to parse the data
        std::memcpy(&out.id_, &_begin[protocol::COMMAND_POSITION_ID], 1);

        if (out.id_ == protocol::id_e::ROUTING_INFO_ID && deal_with_routing_info()) {
            _out_message = std::move(out);
        } else if (out.id_ == protocol::id_e::CONFIG_ID) {
            protocol::command_header hdr{};
            auto parsed_hdr = protocol::deserialize(hdr, _begin, static_cast<uint32_t>(_size));
            if (parsed_hdr) {
                out.client_id_ = hdr.client_;
                // deserialize all key-value pairs for display
                std::vector<std::pair<std::string, std::string>> entries;
                protocol::deserialize(entries, _begin + parsed_hdr, hdr.length_);
                out.payload_ = command_payload(std::move(entries));
            }
            _out_message = std::move(out);
        } else {
            // the data is not important enough to parse the command payload. Lets parse the client
            // and copy the payload as is.
            std::memcpy(&out.client_id_, &_begin[protocol::COMMAND_POSITION_CLIENT], 2);
            uint32_t length;
            std::memcpy(&length, &_begin[protocol::COMMAND_POSITION_SIZE], 4);
            std::vector<unsigned char> payload;
            payload.reserve(length);
            std::copy(_begin + protocol::COMMAND_POSITION_PAYLOAD, _begin + _size, std::back_inserter(payload));
            out.payload_ = command_payload(std::move(payload));
            _out_message = std::move(out);
        }
        return true;
    };

    uint32_t length = 0;
    memcpy(&length, &_data[protocol::COMMAND_POSITION_SIZE], sizeof(length));
    if (std::numeric_limits<uint32_t>::max() - protocol::COMMAND_HEADER_SIZE < length) {
        TEST_LOG << "ERROR message length: " << length << " exceeded allowed message size";
        return 0;
    }
    auto const size = length + protocol::COMMAND_HEADER_SIZE;
    if (size > _len) {
        TEST_LOG << "ERROR remaining_bytes are insufficient";
        return 0;
    }
    if (size <= std::numeric_limits<uint32_t>::max()) {
        if (!handle_message(&_data[0], static_cast<uint32_t>(size))) {
            TEST_LOG << "ERROR message could not be parsed";
            return 0;
        }
    }

    return size;
}
[[nodiscard]] size_t parse(const std::vector<unsigned char>& _message, command_message& _out_message) {
    return parse(_message.data(), _message.size(), _out_message);
}

std::ostream& operator<<(std::ostream& _out, command_message const& _m) {
    _out << "{id: " << to_string(_m.id_);
    _out << ", client_id: " << std::hex << std::setfill('0') << std::setw(4) << _m.client_id_;
    _out << ", payload: [" << to_string(_m.payload_) << "]}";
    return _out;
}
}
