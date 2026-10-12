// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <mutex>
#include <unordered_map>

#include <vsomeip/primitive_types.hpp>

namespace vsomeip_v3 {

// Tracks client session ids.
//
// Used to reduce the chance of session id collisions for clients with short lifecycles which make
// multiple request to the same service.
class session_tracker {
public:
    // Creates a new `session_tracker`.
    //
    // The `starting_session` is the session id used by newly tracked clients and `reuse_offset`
    // is used to avoid session collisions when reusing the client id.
    session_tracker(session_t _starting_session = 1, session_t _reuse_offset = 100) :
        starting_session_(_starting_session), reuse_offset_(_reuse_offset) { }

    // Begins tracking the session id of the given `client`.
    //
    // Returns the starting sesssion id of the client.
    session_t track(client_t _client);

    // Updates the latest `session` used by the given `client`.
    //
    // This is used to calculate the starting session id the next time that the client is tracked.
    void update(client_t _client, session_t _session);

private:
    // Last observed session id of each client.
    std::unordered_map<client_t, session_t> sessions_;

    // Starting session id of new clients.
    const session_t starting_session_;

    // Session id offset when reusing client ids.
    const session_t reuse_offset_;

    // Synchronizes access to this tracker.
    mutable std::mutex mutex_;
};

} // namespace vsomeip_v3
