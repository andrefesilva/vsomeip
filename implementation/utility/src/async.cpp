// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../include/async.hpp"
#include "../../endpoints/include/timer.hpp"

#include "logger_ext.hpp"

namespace vsomeip_v3::async {

hook hook::then(std::function<void()> _on_ready) const {
    std::scoped_lock lock{state_->mtx_};
    assert(!state_->is_consumed_);
    assert(!state_->on_ready_);
    if (state_->is_consumed_ || state_->on_ready_) {
        return {};
    }

    auto t = trigger(state_->io_);
    auto continuation = [t, f = std::move(_on_ready)]() mutable {
        f();
        t.fire();
    };
    if (state_->is_ready_) {
        state_->is_consumed_ = true;
        boost::asio::post(state_->io_, std::move(continuation));
    } else {
        state_->on_ready_ = std::move(continuation);
    }
    return t.get_hook();
}

hook hook::when_not_within(std::chrono::milliseconds _timeout, std::function<void()> _timeout_handler) const {
    std::scoped_lock lock{state_->mtx_};
    assert(!state_->is_consumed_);
    assert(!state_->on_ready_);
    if (state_->is_consumed_ || state_->on_ready_) {
        return {};
    }

    auto t = trigger(state_->io_);
    if (state_->is_ready_) {
        state_->is_consumed_ = true;
        t.fire();
        return t.get_hook();
    }

    auto race = std::make_shared<std::atomic<bool>>(false);
    auto timer = timer::create(state_->io_, _timeout, [race, t, _timeout_handler] {
        bool const already_set = race->exchange(true);
        if (already_set) {
            return false;
        }
        _timeout_handler();
        t.fire();
        return false;
    });

    state_->on_ready_ = [race, timer, t] {
        bool const already_set = race->exchange(true);
        if (already_set) {
            return;
        }
        timer->stop();
        t.fire();
    };
    timer->start();

    return t.get_hook();
}

bool trigger::fire() const {
    // ensure that when potentially cleaning the continuation the dtor
    // would be able to lock the mutex of the shared_state.
    std::function<void()> c;
    boost::asio::io_context* io{nullptr};
    {
        std::scoped_lock lock{state_->mtx_};
        assert(!state_->is_consumed_);
        assert(!state_->is_ready_);
        if (state_->is_ready_ || state_->is_consumed_) {
            return false;
        }
        if (state_->on_ready_) {
            state_->is_consumed_ = true;
            io = &(state_->io_);
            // because the standard does not guarantee that std::move(std::function<void()>)
            // is a nullptr we might remain "shared" owner of the memory managed by this object.
            // If we would post directly and a context switch might execute this before we can go on,
            // the implication would be that we might call a d'tor on the next line
            // that could then try to lock the mutex of the shared state.
            // --> ensure the lifetime of the object ends outside of the lock of the mutex
            c = std::move(state_->on_ready_);
            state_->on_ready_ = nullptr;
        } else {
            state_->is_ready_ = true;
        }
    }
    if (io && c) {
        boost::asio::post(*io, [f = std::move(c)] { f(); });
    }
    return true;
}

hook when_all(hook _lhs, hook _rhs) {
    // pick an arbitrary execution context
    assert(static_cast<bool>(_lhs));
    assert(static_cast<bool>(_rhs));
    if (!_lhs || !_rhs) {
        return {};
    }
    trigger t{_lhs.state_->io_};
    auto flag = std::make_shared<std::atomic<bool>>(false);
    auto continuation = [flag, t] {
        if (auto already_set = flag->exchange(true); already_set) {
            t.fire();
        }
    };
    _lhs.then(continuation);
    _rhs.then(continuation);
    return t.get_hook();
}

}
