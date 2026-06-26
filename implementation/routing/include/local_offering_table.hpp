// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

#include <vsomeip/constants.hpp>
#include <vsomeip/primitive_types.hpp>

#include "internal.hpp"

namespace vsomeip_v3 {

class local_offering_table {
public:
    struct entry {
        service_t service;
        instance_t instance;
        major_version_t major;
        minor_version_t minor;
        client_t client;
    };

    bool add(service_t _service, instance_t _instance, major_version_t _major, minor_version_t _minor, client_t _client) {
        auto& its_instances = services_[_service];
        auto found_instance = its_instances.find(_instance);
        const bool is_new = found_instance == its_instances.end()
                || (_major != ANY_MAJOR && _major != DEFAULT_MAJOR && _major != std::get<0>(found_instance->second));
        its_instances[_instance] = std::make_tuple(_major, _minor, _client);
        return is_new;
    }

    bool remove(service_t _service, instance_t _instance) {
        auto found_service = services_.find(_service);
        if (found_service == services_.end()) {
            return false;
        }
        auto found_instance = found_service->second.find(_instance);
        if (found_instance == found_service->second.end()) {
            return false;
        }
        found_service->second.erase(found_instance);
        if (found_service->second.empty()) {
            services_.erase(found_service);
        }
        return true;
    }

    client_t find_client(service_t _service, instance_t _instance) const {
        const auto* t = find_tuple(_service, _instance);
        return t ? std::get<2>(*t) : static_cast<client_t>(VSOMEIP_ROUTING_CLIENT);
    }

    std::optional<entry> find_entry(service_t _service, instance_t _instance) const {
        const auto* t = find_tuple(_service, _instance);
        if (!t) {
            return std::nullopt;
        }
        return entry{_service, _instance, std::get<0>(*t), std::get<1>(*t), std::get<2>(*t)};
    }

    std::set<client_t> find_clients(service_t _service, instance_t _instance) const {
        std::set<client_t> clients;
        auto found_service = services_.find(_service);
        if (found_service == services_.end()) {
            return clients;
        }
        if (_instance == ANY_INSTANCE) {
            for (const auto& [inst, tuple] : found_service->second) {
                clients.insert(std::get<2>(tuple));
            }
        } else {
            auto found_instance = found_service->second.find(_instance);
            if (found_instance != found_service->second.end()) {
                clients.insert(std::get<2>(found_instance->second));
            }
        }
        return clients;
    }

    // Visit every stored entry matching (service, instance, major, minor) using the
    // ANY_*/DEFAULT_* wildcard semantics. The callback returns true to continue the
    // iteration or false to stop early. Concrete service/instance values are resolved
    // via direct map lookups; only ANY_* widens the traversal, so the common point
    // query stays O(log n) without copying the table.
    template<typename Callback>
    void for_each_available(service_t _service, instance_t _instance, major_version_t _major, minor_version_t _minor,
                            Callback&& _callback) const {
        auto matches_version = [&](const std::tuple<major_version_t, minor_version_t, client_t>& _tuple) {
            const major_version_t its_stored_major = std::get<0>(_tuple);
            const minor_version_t its_stored_minor = std::get<1>(_tuple);
            const bool its_major_ok = _major == ANY_MAJOR || _major == DEFAULT_MAJOR || _major == its_stored_major;
            const bool its_minor_ok = _minor == ANY_MINOR || _minor == DEFAULT_MINOR || _minor <= its_stored_minor;
            return its_major_ok && its_minor_ok;
        };

        auto make_entry = [](service_t _svc, instance_t _inst, const std::tuple<major_version_t, minor_version_t, client_t>& _tuple) {
            return entry{_svc, _inst, std::get<0>(_tuple), std::get<1>(_tuple), std::get<2>(_tuple)};
        };

        // Returns false to request stopping the outer service iteration.
        auto visit_instances = [&](service_t _svc, const instance_map_t& _instances) {
            if (_instance != ANY_INSTANCE) {
                auto found_instance = _instances.find(_instance);
                if (found_instance == _instances.end() || !matches_version(found_instance->second)) {
                    return true;
                }
                return _callback(make_entry(_svc, found_instance->first, found_instance->second));
            }
            for (const auto& [instance, tuple] : _instances) {
                if (matches_version(tuple) && !_callback(make_entry(_svc, instance, tuple))) {
                    return false;
                }
            }
            return true;
        };

        if (_service != ANY_SERVICE) {
            auto found_service = services_.find(_service);
            if (found_service != services_.end()) {
                visit_instances(found_service->first, found_service->second);
            }
            return;
        }
        for (const auto& [service, instances] : services_) {
            if (!visit_instances(service, instances)) {
                break;
            }
        }
    }

    [[nodiscard]] bool has_available(service_t _service, instance_t _instance, major_version_t _major, minor_version_t _minor) const {
        bool its_found = false;
        for_each_available(_service, _instance, _major, _minor, [&its_found](const entry&) {
            its_found = true;
            return false; // stop at the first match
        });
        return its_found;
    }

    bool is_available(service_t _service, instance_t _instance, major_version_t _major) const {
        // Single matching semantics for the whole table: the major-only availability
        // check is the minor-agnostic (ANY_MINOR) case of has_available.
        return has_available(_service, _instance, _major, ANY_MINOR);
    }

    [[nodiscard]] std::vector<entry> remove_all_for_client(client_t _client) {
        std::vector<entry> removed;
        for (auto sit = services_.begin(); sit != services_.end();) {
            for (auto iit = sit->second.begin(); iit != sit->second.end();) {
                if (std::get<2>(iit->second) == _client) {
                    removed.push_back({sit->first, iit->first, std::get<0>(iit->second), std::get<1>(iit->second), _client});
                    iit = sit->second.erase(iit);
                } else {
                    ++iit;
                }
            }
            if (sit->second.empty()) {
                sit = services_.erase(sit);
            } else {
                ++sit;
            }
        }
        return removed;
    }
    [[nodiscard]] std::vector<entry> clear() {
        std::vector<entry> removed;
        for (const auto& [service, instances] : services_) {
            for (const auto& [instance, tuple] : instances) {
                removed.push_back({service, instance, std::get<0>(tuple), std::get<1>(tuple), std::get<2>(tuple)});
            }
        }
        services_.clear();
        return removed;
    }

private:
    using instance_map_t = std::map<instance_t, std::tuple<major_version_t, minor_version_t, client_t>>;
    std::map<service_t, instance_map_t> services_;

    const std::tuple<major_version_t, minor_version_t, client_t>* find_tuple(service_t _service, instance_t _instance) const {
        auto found_service = services_.find(_service);
        if (found_service == services_.end()) {
            return nullptr;
        }
        auto found_instance = found_service->second.find(_instance);
        if (found_instance == found_service->second.end()) {
            return nullptr;
        }
        return &found_instance->second;
    }
};

} // namespace vsomeip_v3
