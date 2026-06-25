// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <memory>

#include <vsomeip/export.hpp>
#include <vsomeip/primitive_types.hpp>
#include "message_base_impl.hpp"

#if _MSC_VER >= 1300
/*
 * Diamond inheritance is used for the vsomeip::message_base base class.
 * The Microsoft compiler put warning (C4250) using a desired c++ feature: "Delegating to a sister
 * class" A powerful technique that arises from using virtual inheritance is to delegate a method
 * from a class in another class by using a common abstract base class. This is also called cross
 * delegation.
 */
#pragma warning(disable : 4250)
#endif

namespace vsomeip_v3 {

class payload;

class message_impl : virtual public message, virtual public message_base_impl {
public:
    VSOMEIP_EXPORT message_impl();
    VSOMEIP_EXPORT virtual ~message_impl();

    VSOMEIP_EXPORT length_t get_length() const override;

    VSOMEIP_EXPORT std::shared_ptr<payload> get_payload() const override;
    VSOMEIP_EXPORT void set_payload(std::shared_ptr<payload> _payload) override;

    VSOMEIP_EXPORT bool serialize(serializer* _to) const override;
    VSOMEIP_EXPORT bool deserialize(deserializer* _from) override;

    VSOMEIP_EXPORT uint8_t get_check_result() const override;
    VSOMEIP_EXPORT void set_check_result(uint8_t _check_result) override;
    VSOMEIP_EXPORT bool is_valid_crc() const override;

    VSOMEIP_EXPORT uid_t get_uid() const override;

    VSOMEIP_EXPORT gid_t get_gid() const override;

    VSOMEIP_EXPORT vsomeip_sec_client_t get_sec_client() const override;
    VSOMEIP_EXPORT std::string get_env() const override;

public:
    // not exported, these are internal to the codebase
    message_impl(const message_header_impl& _header, bool _reliable, uint8_t _check_result);

    void set_sec_client(const vsomeip_sec_client_t& _sec_client);
    void set_env(const std::string& _env);

protected: // members
    std::shared_ptr<payload> payload_;
    uint8_t check_result_;
    vsomeip_sec_client_t sec_client_;
    std::string env_;
};

} // namespace vsomeip_v3
