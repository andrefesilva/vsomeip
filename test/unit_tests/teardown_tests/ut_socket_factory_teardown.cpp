// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../../../implementation/endpoints/include/abstract_socket_factory.hpp"
#include "../../../implementation/endpoints/include/asio_socket_factory.hpp"

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace vsomeip_v3::testing {
namespace {

// An application that is never stopped keeps its io threads running past the return of
// main(), so they create endpoints while the process destroys its static objects. The
// socket factory is owned by two static shared_ptrs inside the library, which are the
// last destructors to run: everything the io thread asks the factory for afterwards is
// served from an object that is being, or has been, destroyed.
constexpr int EXIT_FACTORY_DESTROYED = 42;
constexpr int EXIT_PROBE_NOT_PINNED = 43;

// asio_socket_factory is final, so the probe forwards to an owned instance instead of
// deriving from it.
class probe_socket_factory final : public abstract_socket_factory {
public:
    // The factory must outlive the process, so this must never run at all.
    ~probe_socket_factory() override {
        std::fputs("the socket factory was destroyed while the process was still using it\n", stderr);
        std::_Exit(EXIT_FACTORY_DESTROYED);
    }

#if defined(__linux__)
    std::shared_ptr<abstract_netlink_connector> create_netlink_connector(boost::asio::io_context& _io,
                                                                         const boost::asio::ip::address& _address,
                                                                         const boost::asio::ip::address& _multicast_address,
                                                                         bool _is_requiring_link) override {
        return impl_.create_netlink_connector(_io, _address, _multicast_address, _is_requiring_link);
    }
#endif

    std::unique_ptr<tcp_socket> create_tcp_socket(boost::asio::io_context& _io) override { return impl_.create_tcp_socket(_io); }
    std::unique_ptr<tcp_acceptor> create_tcp_acceptor(boost::asio::io_context& _io) override { return impl_.create_tcp_acceptor(_io); }
    std::unique_ptr<udp_socket> create_udp_socket(boost::asio::io_context& _io) override { return impl_.create_udp_socket(_io); }
#if defined(__linux__) || defined(__QNX__)
    std::unique_ptr<uds_socket> create_uds_socket(boost::asio::io_context& _io) override { return impl_.create_uds_socket(_io); }
    std::unique_ptr<uds_acceptor> create_uds_acceptor(boost::asio::io_context& _io) override { return impl_.create_uds_acceptor(_io); }
#endif
    std::unique_ptr<abstract_timer> create_timer(boost::asio::io_context& _io) override { return impl_.create_timer(_io); }
    std::shared_ptr<abstract_clock> get_clock() override { return impl_.get_clock(); }

private:
    asio_socket_factory impl_;
};

[[noreturn]] void exit_with_a_factory_that_reports_its_own_destruction() {
    auto its_probe = std::make_shared<probe_socket_factory>();
    auto* const its_address = its_probe.get();
    // std::exit does not unwind, so a surviving local reference would keep the probe alive
    // whatever the library does with it.
    set_abstract_factory(std::move(its_probe));

    // Materialize the static the library serves get() from, as any started application does,
    // and establish that it is the probe that got pinned.
    if (abstract_socket_factory::get() != its_address) {
        std::fputs("the probe was not the factory the library pinned\n", stderr);
        std::_Exit(EXIT_PROBE_NOT_PINNED);
    }
    std::exit(0);
}

}

TEST(socket_factory_teardown_test, factory_is_not_destroyed_while_get_still_returns_it) {
    EXPECT_EXIT(exit_with_a_factory_that_reports_its_own_destruction(), ::testing::ExitedWithCode(0), "");
}

}
