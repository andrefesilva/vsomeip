// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../include/session_tracker.hpp"
#include "vsomeip/primitive_types.hpp"
#include <mutex>

namespace vsomeip_v3 {

session_t session_tracker::track(client_t _client) {
    std::scoped_lock lock{mutex_};
    // Session that the client will use.
    auto session = starting_session_;
    // Check if the client is already being tracked.
    if (auto it = sessions_.find(_client); it != sessions_.end()) {
        // Offset the session to avoid collisions with recently used ids.
        session = static_cast<session_t>(it->second + reuse_offset_);
        if (session == 0) {
            session = 1;
        }
    }
    // Track the session.
    sessions_[_client] = session;
    return session;
}

void session_tracker::update(client_t _client, session_t _session) {
    std::scoped_lock lock{mutex_};
    sessions_[_client] = _session;
}

} // namespace vsomeip_v3
