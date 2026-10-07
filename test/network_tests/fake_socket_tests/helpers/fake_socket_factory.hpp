// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../../../implementation/endpoints/include/abstract_socket_factory.hpp"
#include "../../../implementation/endpoints/include/asio_timer.hpp"
#include "../../../implementation/endpoints/include/steady_clock.hpp"

#include "fake_netlink_connector.hpp"
#include "sockets/fake_uds_socket.hpp"
#include "sockets/fake_tcp_socket.hpp"
#include "sockets/fake_udp_socket.hpp"
#include "socket_manager.hpp"

#include <atomic>

namespace vsomeip_v3::testing {

/// Forwards to a real asio_timer, and reports a timer re-armed twice in a row, each time right
/// after it expired, at a deadline earlier than that expiry: a working handler always schedules
/// something later, so such a timer fires back to back. @see socket_manager::spinning_timer_apps()
class spin_detecting_timer : public abstract_timer {
public:
    spin_detecting_timer(std::weak_ptr<socket_manager> _sm, boost::asio::io_context& _io) :
        socket_manager_(std::move(_sm)), io_(&_io), timer_(std::make_unique<asio_timer>(_io)) { }

private:
    void cancel() override { timer_->cancel(); }

    void expires_after(std::chrono::milliseconds _timeout) override { timer_->expires_after(_timeout); }

    void expires_at(std::chrono::steady_clock::time_point _expiration) override {
        auto const its_expiry = expired_at_->exchange({});
        stale_arms_ = its_expiry != std::chrono::steady_clock::time_point{} && _expiration < its_expiry ? stale_arms_ + 1 : 0;
        if (stale_arms_ == 2) {
            if (auto sm = socket_manager_.lock()) {
                sm->report_spinning_timer(io_);
            }
        }
        timer_->expires_at(_expiration);
    }

    void async_wait(handler_t _handler) override {
        timer_->async_wait([expired_at = expired_at_, handler = std::move(_handler)](boost::system::error_code _error) {
            if (!_error) {
                expired_at->store(std::chrono::steady_clock::now());
            }
            handler(_error);
        });
    }

    std::weak_ptr<socket_manager> socket_manager_;
    boost::asio::io_context* io_;
    std::unique_ptr<abstract_timer> timer_;
    std::shared_ptr<std::atomic<std::chrono::steady_clock::time_point>> expired_at_{
            std::make_shared<std::atomic<std::chrono::steady_clock::time_point>>()};
    size_t stale_arms_{0};
};

/**
 * Fake impl of the abstract_socket_factory, that will register each
 * created fake_socket_handle (that is owned by the fake_socket) with the
 * injected socket_manager.
 *
 * For the chosen injection mechanism there will be exactly one fake_socket_factory
 * per test binary. Each test should renew the socket_manager to ensure no interference.
 * It is adviced to use this class not directly but by using the
 * base_fake_socket_fixture.
 **/
class fake_socket_factory : public abstract_socket_factory {
public:
    void set_manager(std::shared_ptr<socket_manager> const& _sm) { socket_manager_ = _sm; }

    // Real monotonic clock: fake_socket tests run real asio_timers, so debounce interval
    // logic must observe real elapsed time (not a frozen fake clock).

private:
    std::shared_ptr<abstract_netlink_connector> create_netlink_connector(boost::asio::io_context& _io, const boost::asio::ip::address&,
                                                                         const boost::asio::ip::address&, bool) override {
        if (auto sm = socket_manager_.lock(); sm) {
            auto netconn = std::make_shared<fake_netlink_connector>(_io);
            sm->add_netlink_connector(netconn, &_io);
            if (auto state = sm->extract_state(&_io); state.has_value()) {
                netconn->set_state(state.value());
            }
            return netconn;
        }
        return nullptr;
    }

    std::unique_ptr<tcp_socket> create_tcp_socket(boost::asio::io_context& _io) override {
        if (auto sm = socket_manager_.lock()) {
            auto state = std::make_shared<fake_tcp_socket_handle>(_io);
            sm->add_socket(state, &_io, socket_type::tcp);
            return std::make_unique<fake_tcp_socket>(state);
        }
        return nullptr;
    }
    std::unique_ptr<tcp_acceptor> create_tcp_acceptor(boost::asio::io_context& _io) override {
        if (auto sm = socket_manager_.lock()) {
            auto state = std::make_shared<fake_tcp_acceptor_handle>(_io);
            sm->add_acceptor(state, &_io, socket_type::tcp);
            return std::make_unique<fake_tcp_acceptor>(state);
        }
        return nullptr;
    }

    std::unique_ptr<udp_socket> create_udp_socket(boost::asio::io_context& _io) override {
        if (auto sm = socket_manager_.lock()) {
            auto state = std::make_shared<fake_udp_socket_handle>(_io);
            sm->add_socket(state, &_io, socket_type::udp);
            return std::make_unique<fake_udp_socket>(state);
        }
        return nullptr;
    }

    std::unique_ptr<abstract_timer> create_timer(boost::asio::io_context& _io) override {
        // do not tinker with timeouts in network tests for now
        return std::make_unique<spin_detecting_timer>(socket_manager_, _io);
    }

    std::shared_ptr<abstract_clock> get_clock() override { return clock_; }
#if defined(__linux__) || defined(__QNX__)
    std::unique_ptr<uds_socket> create_uds_socket(boost::asio::io_context& _io) override {
        if (auto sm = socket_manager_.lock()) {
            auto state = std::make_shared<fake_tcp_socket_handle>(_io);
            sm->add_socket(state, &_io, socket_type::uds);
            return std::make_unique<fake_uds_socket>(state);
        }
        return nullptr;
    }
    std::unique_ptr<uds_acceptor> create_uds_acceptor(boost::asio::io_context& _io) override {
        if (auto sm = socket_manager_.lock()) {
            auto state = std::make_shared<fake_tcp_acceptor_handle>(_io);
            sm->add_acceptor(state, &_io, socket_type::uds);
            return std::make_unique<fake_uds_acceptor>(state);
        }
        return nullptr;
    }
#endif

    std::weak_ptr<socket_manager> socket_manager_;
    std::shared_ptr<steady_clock> clock_{std::make_shared<steady_clock>()};
};
}
