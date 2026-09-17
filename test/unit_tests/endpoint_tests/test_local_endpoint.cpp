// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "base_endpoint_fixture.hpp"
#include "mock_routing_host.hpp"

#include "../../../implementation/security/include/policy_manager_impl.hpp"
#include "../../../implementation/security/include/security.hpp"

#include "../../../implementation/endpoints/include/asio_timer.hpp"
#include "../../../implementation/endpoints/include/steady_clock.hpp"
#include "../../../implementation/endpoints/include/asio_tcp_socket.hpp"
#include "../../../implementation/endpoints/include/asio_udp_socket.hpp"
#include "../../../implementation/endpoints/include/asio_uds_acceptor.hpp"
#include "../../../implementation/endpoints/include/asio_uds_socket.hpp"
#include "../../../implementation/endpoints/include/local_endpoint.hpp"
#include "../../../implementation/endpoints/include/local_server.hpp"
#include "../../../implementation/endpoints/include/local_acceptor_uds_impl.hpp"
#include "../../../implementation/endpoints/include/local_socket_uds_impl.hpp"
#include "../../../implementation/configuration/include/configuration_impl.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/system/error_code.hpp>

#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace vsomeip_v3::testing {

class stub_factory : public abstract_socket_factory {
public:
    std::shared_ptr<abstract_netlink_connector> create_netlink_connector(boost::asio::io_context&, const boost::asio::ip::address&,
                                                                         const boost::asio::ip::address&, bool) override {
        return nullptr;
    }

    virtual std::unique_ptr<tcp_socket> create_tcp_socket(boost::asio::io_context& _io) override {
        return std::make_unique<asio_tcp_socket>(_io);
    }
    virtual std::unique_ptr<tcp_acceptor> create_tcp_acceptor(boost::asio::io_context& _io) override {
        return std::make_unique<asio_tcp_acceptor>(_io);
    }
    virtual std::unique_ptr<udp_socket> create_udp_socket(boost::asio::io_context& _io) override {
        return std::make_unique<asio_udp_socket>(_io);
    }
    virtual std::unique_ptr<abstract_timer> create_timer(boost::asio::io_context& _io) override {
        return std::make_unique<asio_timer>(_io);
    }
    std::shared_ptr<abstract_clock> get_clock() override {
        static auto clock = std::make_shared<steady_clock>();
        return clock;
    }
#if defined(__linux__) || defined(__QNX__)
    std::unique_ptr<uds_socket> create_uds_socket(boost::asio::io_context& _io) override { return std::make_unique<asio_uds_socket>(_io); }
    std::unique_ptr<uds_acceptor> create_uds_acceptor(boost::asio::io_context& _io) override {
        return std::make_unique<asio_uds_acceptor>(_io);
    }
#endif
};

struct test_uds_local_endpoint : base_endpoint_fixture {
    test_uds_local_endpoint() : factory_(std::make_shared<stub_factory>()) {
        delegate_->impl_ = factory_;

        static constexpr char const* path = "uds_local_endpoint_config.json";
        configuration_ = std::make_shared<vsomeip_v3::cfg::configuration_impl>(path);
        configuration_->load("stub");

        ON_CALL(*server_routing_host_, get_policy_manager()).WillByDefault(::testing::Return(policy_manager_));
        ON_CALL(*server_routing_host_, get_security()).WillByDefault(::testing::Return(security_));
        ON_CALL(*client_routing_host_, get_policy_manager()).WillByDefault(::testing::Return(policy_manager_));
        ON_CALL(*client_routing_host_, get_security()).WillByDefault(::testing::Return(security_));
    }

    auto create_server() {
        auto acceptor = std::make_shared<local_acceptor_uds_impl>(io_, server_endpoint_, configuration_);
        boost::system::error_code ec;
        acceptor->init(ec, std::nullopt);
        if (ec) {
            throw std::logic_error("server could not be set-up");
        }
        return std::make_shared<local_server>(io_, std::move(acceptor), configuration_, server_routing_host_, false, "host-env");
    }
    auto create_client_ep(std::optional<vsomeip_sec_client_t> _expected_sec_client = {}) {
        return local_endpoint::create_client_ep(
                local_endpoint_context{io_, configuration_, client_routing_host_},
                local_endpoint_params{server_,
                                      client_,
                                      "",
                                      std::make_unique<local_socket_uds_impl>(io_, boost::asio::local::stream_protocol::endpoint{},
                                                                              server_endpoint_, socket_role_e::CLIENT),
                                      {},
                                      ILLEGAL_PORT,
                                      _expected_sec_client});
    }
    auto create_client_config_command() {
        std::vector<byte_t> msg;
        auto serialize_cmd = [&](protocol::config_command_data const& _cmd) {
            msg.resize(protocol::wire_size(_cmd));
            protocol::serialize(_cmd, msg.data());
        };
        serialize_cmd(protocol::create_config_cmd(client_, {{"hostname", ""}}));
        return msg;
    }
    void add_offer_service_command(std::vector<std::vector<byte_t>>& _queue) {
        auto cmd =
                protocol::create_offer_service_cmd(client_, {.key_ = {.service_ = 2222, .instance_ = 1, .major_ = 1}, .minor_version_ = 0});
        std::vector<byte_t> msg(protocol::wire_size(cmd));
        protocol::serialize(cmd, msg.data());
        _queue.push_back(std::move(msg));
    }
    std::shared_ptr<policy_manager_impl> policy_manager_{std::make_shared<policy_manager_impl>()};
    std::shared_ptr<security> security_{std::make_shared<security>(policy_manager_)};

    std::shared_ptr<stub_factory> factory_;

    boost::asio::io_context io_;
    client_t client_{2222};
    client_t server_{3333};
    boost::asio::local::stream_protocol::endpoint server_endpoint_{"/tmp/vsomeip-3333"};
    std::shared_ptr<mock_routing_host> server_routing_host_{std::make_shared<mock_routing_host>()};
    std::shared_ptr<mock_routing_host> client_routing_host_{std::make_shared<mock_routing_host>()};
    std::shared_ptr<configuration> configuration_;
    std::vector<std::shared_ptr<local_endpoint>> server_eps_;
};

TEST_F(test_uds_local_endpoint, a_local_endpoint_can_connect_to_the_local_server) {

    auto server = create_server();
    auto client = create_client_ep();
    server->start([this](auto const& ep) {
        server_eps_.push_back(ep);
        ep->start();
    });
    client->start();

    io_.poll();
}

TEST_F(test_uds_local_endpoint, config_command_leads_to_information_forwarding_to_routing) {

    auto server = create_server();
    auto client = create_client_ep();
    server->start([this](auto const& ep) {
        server_eps_.push_back(ep);
        ep->start();
    });
    client->start();

    io_.poll();

    EXPECT_CALL(*server_routing_host_, on_message).Times(0); // the config command shall not be forwarded
    EXPECT_CALL(*server_routing_host_, lazy_load(::testing::_));
    auto config_msg = create_client_config_command();
    client->send(&config_msg[0], static_cast<uint32_t>(config_msg.size()));
    io_.poll();

    // check buffered message forwarding
    std::vector<std::vector<byte_t>> received_messages;
    ON_CALL(*server_routing_host_, on_message).WillByDefault([&](auto ptr, auto size, auto...) {
        std::vector<byte_t> msg;
        msg.insert(msg.end(), ptr, ptr + size);
        received_messages.push_back(std::move(msg));
    });
    EXPECT_CALL(*server_routing_host_, on_message).Times(3);
    std::vector<std::vector<byte_t>> send_messages;
    add_offer_service_command(send_messages);
    add_offer_service_command(send_messages);
    add_offer_service_command(send_messages);
    for (auto const& msg : send_messages) {
        client->send(&msg[0], static_cast<uint32_t>(msg.size()));
    }
    io_.poll();

    EXPECT_EQ(received_messages, send_messages);
}
// The identity reported for the peer (passed in when the consumer endpoint is created) is checked
// against the one actually read off the socket (SO_PEERCRED) once connected. On a mismatch -- e.g.
// the client id was re-used by another process -- the connection must not become usable.
TEST_F(test_uds_local_endpoint, connection_is_escalated_when_the_reported_peer_identity_does_not_match) {
    // The peer is the test process itself, so its real identity is the real uid/gid; report a
    // different one for it.
    vsomeip_sec_client_t const its_wrong_identity{.user = getuid() + 1, .group = getgid() + 1, .host = 0, .port = VSOMEIP_SEC_PORT_UNUSED};

    auto server = create_server();
    auto client = create_client_ep(its_wrong_identity);

    bool cleanup_called{false};
    bool cleanup_due_to_error{false};
    client->register_cleanup_handler([&](bool _due_to_error) {
        cleanup_called = true;
        cleanup_due_to_error = _due_to_error;
    });

    server->start([this](auto const& ep) {
        server_eps_.push_back(ep);
        ep->start();
    });
    client->start();
    io_.poll();

    EXPECT_TRUE(cleanup_called);
    EXPECT_TRUE(cleanup_due_to_error);
}

TEST_F(test_uds_local_endpoint, connection_stays_up_when_the_reported_peer_identity_matches) {
    vsomeip_sec_client_t const its_real_identity{.user = getuid(), .group = getgid(), .host = 0, .port = VSOMEIP_SEC_PORT_UNUSED};

    auto server = create_server();
    auto client = create_client_ep(its_real_identity);

    bool cleanup_called{false};
    client->register_cleanup_handler([&](bool _due_to_error) {
        cleanup_called = true;
        client->stop(_due_to_error);
    });

    server->start([this](auto const& ep) {
        server_eps_.push_back(ep);
        ep->start();
    });
    client->start();
    io_.poll();

    EXPECT_FALSE(cleanup_called);

    EXPECT_CALL(*server_routing_host_, lazy_load(::testing::_));
    auto config_msg = create_client_config_command();
    client->send(&config_msg[0], static_cast<uint32_t>(config_msg.size()));
    io_.poll();

    client->request_cleanup(false);
    io_.poll();
    EXPECT_TRUE(cleanup_called);
}

TEST_F(test_uds_local_endpoint, a_terminated_endpoint_abandons_the_rest_of_the_burst) {

    auto server = create_server();
    auto client = create_client_ep();
    server->start([this](auto const& ep) {
        server_eps_.push_back(ep);
        ep->start();
    });
    client->start();

    io_.poll();

    // The server keeps a fresh connection as a tmp_connection until the client identifies itself
    // with a config command; only then is a local_endpoint created and passed to the connection
    // handler. Any other command sent before that is rejected and breaks the connection.
    auto config_msg = create_client_config_command();
    client->send(&config_msg[0], static_cast<uint32_t>(config_msg.size()));
    io_.poll();
    ASSERT_FALSE(server_eps_.empty());

    // Terminate the endpoint from within the first dispatch, which is what stop() does when it is
    // called by an owner while a burst is being processed. The two remaining messages of the burst
    // must not reach the routing host anymore.
    std::size_t dispatched{0};
    ON_CALL(*server_routing_host_, on_message).WillByDefault([&](auto...) {
        if (++dispatched == 1) {
            ASSERT_FALSE(server_eps_.empty());
            server_eps_.front()->stop(true);
        }
    });
    EXPECT_CALL(*server_routing_host_, on_message).Times(1);

    std::vector<std::vector<byte_t>> send_messages;
    add_offer_service_command(send_messages);
    add_offer_service_command(send_messages);
    add_offer_service_command(send_messages);
    for (auto const& msg : send_messages) {
        client->send(&msg[0], static_cast<uint32_t>(msg.size()));
    }

    io_.poll();
    // no further read may have been armed either
    io_.poll();

    EXPECT_EQ(dispatched, 1u);
}

// Fixture for the cases that need the io context to be driven by more than one thread: the
// interleaving under test is a cleanup running on one thread while another is inside on_message.
struct test_uds_local_endpoint_mt : test_uds_local_endpoint {
    void connect_and_settle() {
        server_ = create_server();
        client_ = create_client_ep();
        server_->start([this](auto const& ep) {
            server_eps_.push_back(ep);
            ep->start();
        });
        client_->start();
        io_.poll();
        // identifies the client to the server, which only then creates the server side endpoint
        auto config_msg = create_client_config_command();
        client_->send(&config_msg[0], static_cast<uint32_t>(config_msg.size()));
        io_.poll();
        ASSERT_FALSE(server_eps_.empty());
    }

    // Blocks every dispatch until release_dispatch() is called, and records when it entered and
    // when it left.
    void block_in_on_message() {
        ON_CALL(*server_routing_host_, on_message).WillByDefault([this](auto...) {
            {
                std::unique_lock lk{m_};
                in_dispatch_ = true;
            }
            cv_.notify_all();
            std::unique_lock lk{m_};
            cv_.wait(lk, [this] { return release_; });
            events_.emplace_back("dispatch_end");
            cv_.notify_all();
        });
    }

    void await_dispatch() {
        std::unique_lock lk{m_};
        cv_.wait(lk, [this] { return in_dispatch_; });
    }

    void release_dispatch() {
        {
            std::scoped_lock lk{m_};
            release_ = true;
        }
        cv_.notify_all();
    }

    void run_io(unsigned _threads) {
        guard_.emplace(boost::asio::make_work_guard(io_));
        for (unsigned i = 0; i < _threads; ++i) {
            io_threads_.emplace_back([this] { io_.run(); });
        }
    }

    void TearDown() override {
        release_dispatch();
        guard_.reset();
        io_.stop();
        for (auto& t : io_threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    std::shared_ptr<local_server> server_;
    std::shared_ptr<local_endpoint> client_;

    std::mutex m_;
    std::condition_variable cv_;
    bool in_dispatch_{false};
    bool release_{false};
    std::vector<std::string> events_; // guarded by m_

    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> guard_;
    std::vector<std::thread> io_threads_;
};

TEST_F(test_uds_local_endpoint_mt, cleanup_does_not_overlap_a_dispatch_in_flight) {

    connect_and_settle();
    block_in_on_message();

    server_eps_.front()->register_cleanup_handler([this](bool) {
        {
            std::scoped_lock lk{m_};
            events_.emplace_back("cleanup");
        }
        cv_.notify_all();
    });

    run_io(2);

    std::vector<std::vector<byte_t>> send_messages;
    add_offer_service_command(send_messages);
    client_->send(&send_messages[0][0], static_cast<uint32_t>(send_messages[0].size()));

    await_dispatch();
    // Escalates on the second io thread while the first one is still inside on_message.
    server_eps_.front()->request_cleanup(true);

    {
        // Nothing may be recorded yet: the dispatch has not finished, so the cleanup must still be
        // held back. Without the barrier the cleanup lands here, ahead of "dispatch_end".
        std::unique_lock lk{m_};
        EXPECT_FALSE(cv_.wait_for(lk, std::chrono::milliseconds(200), [this] { return !events_.empty(); }))
                << "cleanup ran while a dispatch was still in flight";
    }

    release_dispatch();

    std::unique_lock lk{m_};
    EXPECT_TRUE(cv_.wait_for(lk, std::chrono::seconds(5), [this] { return events_.size() == 2; })) << "cleanup handler was not invoked";
    EXPECT_EQ(events_, (std::vector<std::string>{"dispatch_end", "cleanup"}));
}

TEST_F(test_uds_local_endpoint_mt, holding_back_a_cleanup_does_not_park_an_io_thread) {

    connect_and_settle();
    block_in_on_message();

    server_eps_.front()->register_cleanup_handler([](bool) {});

    run_io(2);

    std::vector<std::vector<byte_t>> send_messages;
    add_offer_service_command(send_messages);
    client_->send(&send_messages[0][0], static_cast<uint32_t>(send_messages[0].size()));

    await_dispatch();

    // One io thread is stuck inside on_message. The other has to pick the escalation up, hand the
    // cleanup over to process() and stay available. Were it to wait for the dispatch to finish
    // instead, both threads would be parked and nothing queued behind it could run.
    server_eps_.front()->request_cleanup(true);

    // shared_ptr, so that the promise outlives this test body: if the marker never runs, the task is
    // still queued when the body returns and only executes during TearDown.
    auto marker = std::make_shared<std::promise<void>>();
    auto ran = marker->get_future();
    boost::asio::post(io_, [marker] { marker->set_value(); });

    EXPECT_EQ(ran.wait_for(std::chrono::seconds(5)), std::future_status::ready)
            << "no io thread was left to run other work while the dispatch was blocked";
}

TEST_F(test_uds_local_endpoint_mt, a_flush_completing_during_a_dispatch_is_held_back) {

    connect_and_settle();
    block_in_on_message();

    server_eps_.front()->register_cleanup_handler([this](bool _due_to_error) {
        {
            std::scoped_lock lk{m_};
            events_.emplace_back(_due_to_error ? "cleanup_error" : "cleanup_flushed");
        }
        cv_.notify_all();
    });

    run_io(2);

    std::vector<std::vector<byte_t>> send_messages;
    add_offer_service_command(send_messages);
    client_->send(&send_messages[0][0], static_cast<uint32_t>(send_messages[0].size()));

    await_dispatch();
    // The send queue is empty, so the flush completes immediately - on the second io thread, while
    // the first is still inside on_message.
    server_eps_.front()->start_flushing();

    {
        std::unique_lock lk{m_};
        EXPECT_FALSE(cv_.wait_for(lk, std::chrono::milliseconds(200), [this] { return !events_.empty(); }))
                << "flush cleanup ran while a dispatch was still in flight";
    }

    release_dispatch();

    std::unique_lock lk{m_};
    EXPECT_TRUE(cv_.wait_for(lk, std::chrono::seconds(5), [this] { return events_.size() == 2; })) << "cleanup handler was not invoked";
    // A completed flush is a deliberate shutdown, so it must not be reported as an error.
    EXPECT_EQ(events_, (std::vector<std::string>{"dispatch_end", "cleanup_flushed"}));
}

TEST_F(test_uds_local_endpoint, clean_endpoint_runs_on_io_context) {

    auto server = create_server();
    auto client = create_client_ep();
    server->start([this](auto const& ep) {
        server_eps_.push_back(ep);
        ep->start();
    });
    client->start();
    io_.poll();
    auto config_msg = create_client_config_command();
    client->send(&config_msg[0], static_cast<uint32_t>(config_msg.size()));
    io_.poll();
    ASSERT_FALSE(server_eps_.empty());

    bool cleaned_up{false};
    server_eps_.front()->register_cleanup_handler([&cleaned_up](bool) { cleaned_up = true; });

    // Asking the endpoint to tear itself down is what every owner is expected to do instead. The
    // handler lands posted, so the teardown is only complete once the io context ran it.
    server_eps_.front()->request_cleanup(true);
    EXPECT_FALSE(cleaned_up);
    io_.poll();
    EXPECT_TRUE(cleaned_up);
    EXPECT_NO_THROW(server_eps_.front()->stop(true));
}
}
