// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <compare>
#include <functional>
#include <iomanip>
#include <ostream>

#include <boost/functional/hash.hpp>

#include <vsomeip/primitive_types.hpp>

namespace vsomeip_v3 {

struct service_instance_t {

    constexpr auto operator<=>(const service_instance_t&) const = default;

    service_t service;
    instance_t instance;
};

inline std::ostream& operator<<(std::ostream& _os, const service_instance_t& _si) {
    auto flags = _os.flags();
    auto fill = _os.fill();
    _os << std::hex << std::setfill('0') << std::setw(4) << _si.service << "." << std::setw(4) << _si.instance;
    _os.flags(flags);
    _os.fill(fill);
    return _os;
}

struct versioned_service_instance_t {

    constexpr auto operator<=>(const versioned_service_instance_t&) const = default;

    service_t service;
    instance_t instance;
    major_version_t major;
};

std::ostream& operator<<(std::ostream& _os, versioned_service_instance_t const& _s);

// Pairs a service/instance/major key with a single event id for logging; the key is referenced,
// not owned, so this is only meant to be used as a short-lived stream argument.
struct event_key {
    versioned_service_instance_t const& key;
    event_t event;
};

std::ostream& operator<<(std::ostream& _os, event_key const& _data);

} // namespace vsomeip_v3

namespace std {
template<>
struct hash<vsomeip_v3::service_instance_t> {
    size_t operator()(const vsomeip_v3::service_instance_t& k) const {
        size_t seed = 0;
        boost::hash_combine(seed, k.service);
        boost::hash_combine(seed, k.instance);
        return seed;
    }
};

template<>
struct hash<vsomeip_v3::versioned_service_instance_t> {
    size_t operator()(const vsomeip_v3::versioned_service_instance_t& k) const {
        size_t seed = 0;
        boost::hash_combine(seed, k.service);
        boost::hash_combine(seed, k.instance);
        boost::hash_combine(seed, k.major);
        return seed;
    }
};
} // namespace std
