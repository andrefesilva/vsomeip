// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <iomanip>
#include "common/test_main.hpp"

#include "restart_routing_test_service.hpp"
#include "restart_routing_test_globals.hpp"

restart_routing_test_service::restart_routing_test_service() :
    app_(vsomeip::runtime::get()->create_application("restart_routing_test_service")) { }

restart_routing_test_service::~restart_routing_test_service() {
    app_->clear_all_handler();
    app_->stop();

    if (runner_.joinable()) {
        runner_.join();
    }
    if (starter_.joinable()) {
        starter_.join();
    }
}

void restart_routing_test_service::offer() {
    app_->offer_service(vsomeip_test::TEST_SERVICE_SERVICE_ID, vsomeip_test::TEST_SERVICE_INSTANCE_ID);
}

void restart_routing_test_service::stop_offer() {
    app_->stop_offer_service(vsomeip_test::TEST_SERVICE_SERVICE_ID, vsomeip_test::TEST_SERVICE_INSTANCE_ID);
}

void restart_routing_test_service::on_message(const std::shared_ptr<vsomeip::message>& _request) {
    std::scoped_lock its_guard(mutex_);
    ASSERT_EQ(vsomeip_test::TEST_SERVICE_SERVICE_ID, _request->get_service());
    ASSERT_EQ(vsomeip_test::TEST_SERVICE_METHOD_ID, _request->get_method());
    received_counter_[_request->get_client()]++;
    VSOMEIP_INFO << "Received a message with Client/Session [" << std::hex << std::setfill('0') << std::setw(4) << _request->get_client()
                 << "/" << std::setw(4) << _request->get_session() << "] : " << std::dec << received_counter_[_request->get_client()];

    // send response
    std::shared_ptr<vsomeip::message> its_response = vsomeip::runtime::get()->create_response(_request);

    app_->send(its_response);
    if (++number_of_received_messages_ == NUM_SERVICE_CONSUMERS * vsomeip_test::NUMBER_OF_MESSAGES_TO_SEND_ROUTING_RESTART_TESTS) {
        VSOMEIP_INFO << "Received all messages!";
        all_received_ = true;
        condition_.notify_all();
    }
}

void restart_routing_test_service::init() {
    if (!app_->init()) {
        ADD_FAILURE() << "Couldn't initialize application";
        return;
    }

    app_->register_message_handler(vsomeip_test::TEST_SERVICE_SERVICE_ID, vsomeip_test::TEST_SERVICE_INSTANCE_ID,
                                   vsomeip_test::TEST_SERVICE_METHOD_ID,
                                   std::bind(&restart_routing_test_service::on_message, this, std::placeholders::_1));

    starter_ = std::thread([&]() { app_->start(); });
    offer();
}

bool restart_routing_test_service::wait_for_messages() {
    std::unique_lock its_lock(mutex_);
    return condition_.wait_for(its_lock, std::chrono::milliseconds(5000), [this] { return all_received_; });
}
