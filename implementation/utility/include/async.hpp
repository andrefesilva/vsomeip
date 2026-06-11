// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <functional>

namespace vsomeip_v3::async {

struct shared_trigger_state {

    explicit shared_trigger_state(boost::asio::io_context& _io) : io_(_io) { }

    // trigger was fired
    bool is_ready_{false};
    // continuation was scheduled
    bool is_consumed_{false};

    boost::asio::io_context& io_;
    std::function<void()> on_ready_;
    std::mutex mtx_;
};

/// A handle to await a single trigger. Exactly one continuation may be attached.
class hook {
public:
    hook() = default;
    explicit hook(std::shared_ptr<shared_trigger_state> _state) : state_(std::move(_state)) { }

    explicit operator bool() const { return static_cast<bool>(state_); }

    /// Attach a continuation. Runs on the io_context once the trigger fires.
    /// Returns a hook that completes after _on_ready returns.
    hook then(std::function<void()> _on_ready) const;

    /// Race between this hook completing and _timeout expiring.
    /// If the hook fires first, the returned hook completes immediately.
    /// If _timeout expires first, _timeout_handler runs and the returned hook completes.
    /// Whichever wins cancels the other.
    /// Guarantees the returned hook completes even if this hook never fires.
    /// Mutually exclusive with then().
    hook when_not_within(std::chrono::milliseconds _timeout, std::function<void()> _timeout_handler) const;

private:
    std::shared_ptr<shared_trigger_state> state_;

    friend hook when_all(hook _lhs, hook _rhs);
};

/// Producer side. Call fire() to signal completion to the attached hook.
class trigger {
public:
    trigger() = default;

    explicit trigger(boost::asio::io_context& _io) : state_(std::make_shared<shared_trigger_state>(_io)) { }

    explicit operator bool() const { return static_cast<bool>(state_); }
    hook get_hook() const { return hook(state_); }

    /// Signal completion. Posts the hook's continuation to the io_context.
    /// Returns false if already fired.
    bool fire() const;

private:
    std::shared_ptr<shared_trigger_state> state_;
};

/// Returns a hook that completes when both _lhs and _rhs have completed.
hook when_all(hook _lhs, hook _rhs);

}
