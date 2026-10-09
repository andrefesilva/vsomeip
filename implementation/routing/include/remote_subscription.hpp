// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <map>
#include <mutex>
#include <set>

#include <vsomeip/primitive_types.hpp>

#include "types.hpp"

#if defined(__QNX__)
#include "../../utility/include/qnx_helper.hpp"
#endif

namespace vsomeip_v3 {

class eventgroupinfo;
class endpoint_definition;

const remote_subscription_id_t PENDING_SUBSCRIPTION_ID(0);

class remote_subscription : public std::enable_shared_from_this<remote_subscription> {
public:
    explicit remote_subscription(const std::shared_ptr<eventgroupinfo>& _info, bool _force_initial_events, ttl_t _ttl,
                                 const std::shared_ptr<endpoint_definition>& _subscriber,
                                 const std::shared_ptr<endpoint_definition>& _reliable,
                                 const std::shared_ptr<endpoint_definition>& _unreliable);
    ~remote_subscription();

    bool operator==(const remote_subscription& _other) const;
    bool equals(const std::shared_ptr<remote_subscription>& _other) const;

    void reset(const std::set<client_t>& _clients);

    bool is_initial() const;
    void set_initial(const bool _is_initial);

    bool force_initial_events() const;

    remote_subscription_id_t get_id() const;
    void set_id(const remote_subscription_id_t _id);

    std::shared_ptr<remote_subscription> get_parent() const;
    void set_parent(const std::shared_ptr<remote_subscription>& _parent);

    std::shared_ptr<eventgroupinfo> get_eventgroupinfo() const;

    ttl_t get_ttl() const;

    std::set<client_t> get_clients() const;
    bool has_client() const;
    bool has_client(const client_t _client) const;
    void remove_client(const client_t _client);

    remote_subscription_state_e get_client_state(const client_t _client) const;
    void set_client_state(const client_t _client, remote_subscription_state_e _state);
    void set_all_client_states(remote_subscription_state_e _state);

    std::chrono::steady_clock::time_point get_expiration(const client_t _client) const;

    std::shared_ptr<endpoint_definition> get_subscriber() const;
    std::shared_ptr<endpoint_definition> get_reliable() const;
    std::shared_ptr<endpoint_definition> get_unreliable() const;

    bool is_pending() const;

    std::set<client_t> update(const std::set<client_t>& _clients, const std::chrono::steady_clock::time_point& _timepoint,
                              const bool _is_subscribe);

    uint32_t get_answers() const;
    void set_answers(const uint32_t _answers);

    bool get_ip_address(boost::asio::ip::address& _address) const;

    bool is_expired() const;
    void set_expired();
    bool is_forwarded() const;
    void set_forwarded();
    void clear_destiny();
    std::shared_ptr<remote_subscription> get_parent_or_self();

private:
    const std::weak_ptr<eventgroupinfo> eventgroupinfo_;
    const bool force_initial_events_;
    const ttl_t ttl_;
    // The endpoint that sent(!) the subscription
    const std::shared_ptr<endpoint_definition> subscriber_;
    // The endpoints defined by the endpoint options
    const std::shared_ptr<endpoint_definition> reliable_;
    const std::shared_ptr<endpoint_definition> unreliable_;

    mutable std::mutex mutex_;

    // unique id within the eventgroup
    remote_subscription_id_t id_ = PENDING_SUBSCRIPTION_ID;
    // whether initial events (for fields) must be sent
    bool is_initial_ = true;
    // reference to parent; only makes sense for selective subscriptions
    std::weak_ptr<remote_subscription> parent_;
    // client state; selective subscriptions can have several clients, otherwise it is always only VSOMEIP_ROUTING_CLIENT
    std::map<client_t, std::pair<remote_subscription_state_e, std::chrono::steady_clock::time_point>> clients_;
    // Number of acknowledgements that must be sent
    // for the subscriptions. This is usually 1, but
    // may be larger if a matching subscription arrived
    // before the subscription could be acknowledged
    uint32_t answers_ = 1;
    // This flag specifies what the "winner" of the
    // expire_subscriptions()/on_remote_subscribe()
    // race shall have as destination:
    // - expiration, if expire_subscriptions() runs first
    // - forwarding, if on_remote_subscribe() runs first
    enum struct destiny : uint8_t { none, expire, forward };
    destiny final_destination_ = destiny::none;
};

} // namespace vsomeip_v3
