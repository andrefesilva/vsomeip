// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/app.hpp"
#include "helpers/attribute_recorder.hpp"
#include "helpers/base_fake_socket_fixture.hpp"
#include "helpers/command_gate.hpp"
#include "helpers/command_record.hpp"
#include "helpers/ecu_config.hpp"
#include "helpers/ecu_setup.hpp"
#include "helpers/fake_socket_factory.hpp"
#include "helpers/message_checker.hpp"
#include "helpers/service_state.hpp"
#include "helpers/someip_gate.hpp"

#include "sample_configurations.hpp"

#include "common/timeout_scale.hpp"
#include "vsomeip/constants.hpp"

#include <chrono>
#include <vsomeip/enumeration_types.hpp>
#include <vsomeip/vsomeip.hpp>

#include <gtest/gtest.h>

#include <boost/asio/error.hpp>

#include <cstdint>
#include <cstdlib>
#include <utility>

namespace vsomeip_v3::testing {
struct test_someip_protocol : public base_fake_socket_fixture {
    // does not start the _any_ client
    void start_two_ecu_setup() {
        provider_ecu_.add_app(provider_name_);
        consumer_ecu_.add_app(client_name_);

        provider_ecu_.prepare();
        consumer_ecu_.prepare();

        provider_ecu_.start_apps();
        consumer_ecu_.start_apps();

        provider_ = provider_ecu_.apps_[provider_name_];
        client_ = consumer_ecu_.apps_[client_name_];

        ASSERT_TRUE(provider_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));
        ASSERT_TRUE(client_->app_state_record_.wait_for_last(vsomeip::state_type_e::ST_REGISTERED));

        ASSERT_TRUE(set_udp_data_pipe(udp_party::any(), udp_party::any(), udp_traffic_->get_data_pipe()));
    }

    std::string const provider_name_{"provider"};
    std::string const client_name_{"client"};

    interface udp_interface_{service_instance{.service_ = 0x1000, .instance_ = 0x1, .major_ = 0x1, .minor_ = 0x0},
                             {},
                             {event_spec{0x8001, {0x8001}, vsomeip::reliability_type_e::RT_UNRELIABLE}}};

    vsomeip::method_t const method_{0x1111};
    request request_{udp_interface_.instance_, method_, vsomeip::message_type_e::MT_REQUEST, false, {}};

    // The SD multicast group address (VSOMEIP_SD_DEFAULT_MULTICAST); none of the ecu_configs used
    // here override it. FindService/OfferService sent to this address still go out as an ordinary
    // sendto() on the sender's own unicast SD socket -- joining the group is only needed to
    // *receive* multicast traffic, so gating on this as the receiver party does not depend on
    // whether the multicast join has completed.
    boost::asio::ip::udp::endpoint const sd_multicast_ep_{boost::asio::ip::make_address("224.224.224.0"), 30490};

    ecu_setup provider_ecu_{
            "provider", ecu_config{boardnet::ecu_one_config}.add_interface({udp_interface_}),
            *socket_manager_}; // configuration is version independent. Adding one interface enables offering for both versions.
    ecu_setup consumer_ecu_{"consumer", boardnet::ecu_two_config, *socket_manager_};

    app* provider_;
    app* client_;
    std::shared_ptr<someip_gate> udp_traffic_ = someip_gate::create();
};

TEST_F(test_someip_protocol, a_request_with_a_non_offered_version_receives_error_with_wrong_version) {
    start_two_ecu_setup();

    provider_->offer(udp_interface_.instance_);
    client_->request_service(udp_interface_.instance_);
    ASSERT_TRUE(client_->availability_record_.wait_for_last(service_availability::available(udp_interface_.instance_)));

    auto request_with_wrong_major = construct_someip_raw_message(static_cast<uint16_t>(udp_interface_.instance_.service_), // service id
                                                                 static_cast<uint16_t>(method_), // method id
                                                                 static_cast<uint32_t>(0x00000008), // correct length
                                                                 static_cast<uint16_t>(client_->get_client()), // client id
                                                                 static_cast<uint16_t>(0x0003), // session id
                                                                 static_cast<uint8_t>(0x01), // proto version
                                                                 static_cast<uint8_t>(udp_interface_.instance_.major_ + 1), // iface version
                                                                 static_cast<uint8_t>(0x00), // message type
                                                                 static_cast<uint8_t>(0x00) // return code
    );
    inject_message_udp(boost::asio::ip::udp::endpoint(consumer_ecu_.config().unicast_ip_, 30491),
                       boost::asio::ip::udp::endpoint(provider_ecu_.config().unicast_ip_, 30501), request_with_wrong_major);

    someip_record_message notification_record{udp_interface_.instance_.service_, method_,
                                              someip_record_message::ANY_CLIENT, someip_record_message::ANY_SESSION,
                                              message_type_e::MT_ERROR,          return_code_e::E_WRONG_INTERFACE_VERSION};

    EXPECT_TRUE(udp_traffic_->someip_record_.wait_for_any(notification_record)) << udp_traffic_->someip_record_;
}
}
