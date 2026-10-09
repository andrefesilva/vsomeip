// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../include/remote_subscription.hpp"
#include "../../endpoints/include/endpoint_definition.hpp"

#include <vsomeip/internal/logger.hpp>

namespace vsomeip_v3 {

remote_subscription::remote_subscription(const std::shared_ptr<eventgroupinfo>& _info, bool _force_initial_events, ttl_t _ttl,
                                         const std::shared_ptr<endpoint_definition>& _subscriber,
                                         const std::shared_ptr<endpoint_definition>& _reliable,
                                         const std::shared_ptr<endpoint_definition>& _unreliable) :
    eventgroupinfo_(_info), force_initial_events_(_force_initial_events), ttl_(_ttl), subscriber_(_subscriber), reliable_(_reliable),
    unreliable_(_unreliable) { }

remote_subscription::~remote_subscription() { }

bool remote_subscription::operator==(const remote_subscription& _other) const {
    auto own_egi = eventgroupinfo_.lock();
    auto other_egi = _other.eventgroupinfo_.lock();
    bool reliable_equal(true);
    if (reliable_ && _other.reliable_) {
        reliable_equal = (reliable_ == _other.reliable_);
    }
    bool unreliable_equal(true);
    if (unreliable_ && _other.unreliable_) {
        unreliable_equal = (unreliable_ == _other.unreliable_);
    }
    return (own_egi && other_egi && own_egi == other_egi && unreliable_equal && reliable_equal);
}

bool remote_subscription::equals(const std::shared_ptr<remote_subscription>& _other) const {
    return operator==(*_other);
}

void remote_subscription::reset(const std::set<client_t>& _clients) {
    std::scoped_lock its_lock(mutex_);
    auto its_client_state = std::make_pair(remote_subscription_state_e::SUBSCRIPTION_PENDING, std::chrono::steady_clock::time_point());
    if (_clients.empty()) {
        clients_[0] = its_client_state;
    } else {
        for (const auto& its_client : _clients) {
            clients_[its_client] = its_client_state;
        }
    }
}

bool remote_subscription::is_initial() const {
    std::scoped_lock its_lock(mutex_);
    return is_initial_;
}

void remote_subscription::set_initial(const bool _is_initial) {
    std::scoped_lock its_lock(mutex_);
    is_initial_ = _is_initial;
}

bool remote_subscription::force_initial_events() const {
    return force_initial_events_;
}

remote_subscription_id_t remote_subscription::get_id() const {
    std::scoped_lock its_lock(mutex_);
    return id_;
}

void remote_subscription::set_id(const remote_subscription_id_t _id) {
    std::scoped_lock its_lock(mutex_);
    id_ = _id;
}

std::shared_ptr<eventgroupinfo> remote_subscription::get_eventgroupinfo() const {
    return eventgroupinfo_.lock();
}

ttl_t remote_subscription::get_ttl() const {
    return ttl_;
}

std::set<client_t> remote_subscription::get_clients() const {
    std::scoped_lock its_lock(mutex_);
    std::set<client_t> its_clients;
    for (const auto& its_item : clients_) {
        its_clients.insert(its_item.first);
    }
    return its_clients;
}

bool remote_subscription::has_client() const {
    std::scoped_lock its_lock(mutex_);
    return (clients_.size() > 0);
}

bool remote_subscription::has_client(const client_t _client) const {
    std::scoped_lock its_lock(mutex_);
    return (clients_.count(_client) > 0);
}

void remote_subscription::remove_client(const client_t _client) {
    std::scoped_lock its_lock(mutex_);
    clients_.erase(_client);
}

remote_subscription_state_e remote_subscription::get_client_state(const client_t _client) const {
    std::scoped_lock its_lock(mutex_);
    auto found_client = clients_.find(_client);
    if (found_client != clients_.end()) {
        return found_client->second.first;
    }
    return remote_subscription_state_e::SUBSCRIPTION_PENDING;
}

void remote_subscription::set_client_state(const client_t _client, remote_subscription_state_e _state) {
    std::scoped_lock its_lock(mutex_);
    auto found_item = clients_.find(_client);
    if (found_item != clients_.end()) {
        found_item->second.first = _state;
        if (found_item->second.second == std::chrono::steady_clock::time_point()
            && (_state == remote_subscription_state_e::SUBSCRIPTION_ACKED || _state == remote_subscription_state_e::SUBSCRIPTION_NACKED)) {
            found_item->second.second = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_);
        }
    }
}

void remote_subscription::set_all_client_states(remote_subscription_state_e _state) {
    std::scoped_lock its_lock(mutex_);
    for (auto& its_item : clients_) {
        its_item.second.first = _state;
    }
}

std::shared_ptr<endpoint_definition> remote_subscription::get_subscriber() const {
    return subscriber_;
}

std::shared_ptr<endpoint_definition> remote_subscription::get_reliable() const {
    return reliable_;
}

std::shared_ptr<endpoint_definition> remote_subscription::get_unreliable() const {
    return unreliable_;
}

bool remote_subscription::is_pending() const {
    std::scoped_lock its_lock(mutex_);
    for (auto its_client : clients_) {
        if (its_client.second.first == remote_subscription_state_e::SUBSCRIPTION_PENDING) {
            return true;
        }
    }
    return false;
}

std::chrono::steady_clock::time_point remote_subscription::get_expiration(const client_t _client) const {
    std::scoped_lock its_lock(mutex_);
    auto found_client = clients_.find(_client);
    if (found_client != clients_.end()) {
        return found_client->second.second;
    }
    return std::chrono::steady_clock::now();
}

std::set<client_t> remote_subscription::update(const std::set<client_t>& _clients, const std::chrono::steady_clock::time_point& _timepoint,
                                               const bool _is_subscribe) {
    std::set<client_t> its_changed;

    std::scoped_lock its_lock(mutex_);
    for (const auto& its_client : _clients) {
        auto found_client = clients_.find(its_client);
        if (_is_subscribe) {
            if (found_client != clients_.end()) {
                found_client->second.second = _timepoint;
            } else {
                its_changed.insert(its_client);
            }
        } else {
            if (found_client != clients_.end()) {
                its_changed.insert(its_client);
            }
        }
    }

    for (const auto& its_client : its_changed) {
        if (_is_subscribe) {
            clients_[its_client] = std::make_pair(remote_subscription_state_e::SUBSCRIPTION_PENDING, _timepoint);
        } else {
            clients_.erase(its_client);
        }
    }

    return its_changed;
}

std::shared_ptr<remote_subscription> remote_subscription::get_parent() const {
    std::scoped_lock its_lock(mutex_);
    return parent_.lock();
}

void remote_subscription::set_parent(const std::shared_ptr<remote_subscription>& _parent) {
    std::scoped_lock its_lock(mutex_);
    parent_ = _parent;
}

uint32_t remote_subscription::get_answers() const {
    std::scoped_lock its_lock(mutex_);
    return answers_;
}

void remote_subscription::set_answers(const uint32_t _answers) {
    std::scoped_lock its_lock(mutex_);
    answers_ = _answers;
}

bool remote_subscription::get_ip_address(boost::asio::ip::address& _address) const {
    if (reliable_) {
        _address = reliable_->get_address();
        return true;
    } else if (unreliable_) {
        _address = unreliable_->get_address();
        return true;
    }
    return false;
}

bool remote_subscription::is_expired() const {
    std::scoped_lock its_lock(mutex_);
    return final_destination_ == destiny::expire;
}

void remote_subscription::set_expired() {
    std::scoped_lock its_lock(mutex_);
    final_destination_ = destiny::expire;
}

bool remote_subscription::is_forwarded() const {
    std::scoped_lock its_lock(mutex_);
    return final_destination_ == destiny::forward;
}

void remote_subscription::set_forwarded() {
    std::scoped_lock its_lock(mutex_);
    final_destination_ = destiny::forward;
}

void remote_subscription::clear_destiny() {
    std::scoped_lock its_lock(mutex_);
    final_destination_ = destiny::none;
}

std::shared_ptr<remote_subscription> remote_subscription::get_parent_or_self() {
    std::scoped_lock its_lock(mutex_);
    // check if parent subscription exists, if so return it, otherwise return the subscription itself to be used for checking pending
    // clients
    if (std::shared_ptr<remote_subscription> p = parent_.lock()) {
        return p;
    }
    return shared_from_this();
}
} // namespace vsomeip_v3
