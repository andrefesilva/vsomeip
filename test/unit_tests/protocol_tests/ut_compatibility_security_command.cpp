// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Security Command Compatibility Tests (header + service_data payload)
// ============================================================================
//
// Proves that the new struct-based serialization produces
// byte-for-byte identical wire output to the old class-based approach
// that the new side understands this format is ensured by the corresponding roundtrip test
//
// ============================================================================

#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#if __GNUC__ > 11
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

#include <vector>

#include "../../../implementation/message/include/payload_impl.hpp"
#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/update_security_credentials_command.hpp"
#include "../../../implementation/protocol/include/update_security_policy_command.hpp"
#include "../../../implementation/protocol/include/distribute_security_policies_command.hpp"
#include "../../../implementation/security/include/policy.hpp"

namespace vsomeip_v3::protocol {

template<typename T>
static std::vector<uint8_t> to_wire(T const& _cmd) {
    std::vector<uint8_t> buf(wire_size(_cmd));
    serialize(_cmd, buf.data());
    return buf;
}

template<typename OldCmd>
static std::vector<uint8_t> old_to_wire(OldCmd const& _cmd) {
    std::vector<uint8_t> buf;
    _cmd.serialize(buf);
    return buf;
}

TEST(ut_compatibility_security_command, update_credentials) {
    std::set<std::pair<uid_t, gid_t>> none = {};
    std::set<std::pair<uid_t, gid_t>> one = {{0x1, 0x1}};
    std::set<std::pair<uid_t, gid_t>> many = {{0x2, 0x2}, {0x3, 0x3}};

    for (auto const& ids : {none, one, many}) {
        auto cmd = protocol::create_update_security_credentials_cmd(0x7, ids);

        auto old_cmd = protocol::update_security_credentials_command();
        old_cmd.set_client(cmd.header_.client_);
        old_cmd.set_credentials(ids);

        auto new_wire = to_wire(cmd);
        auto old_wire = old_to_wire(old_cmd);

        EXPECT_EQ(new_wire, old_wire);
    }
}
TEST(ut_compatibility_security_command, update_policy) {
    std::shared_ptr<policy> none = {};
    std::shared_ptr<policy> one = std::make_shared<policy>();
    auto its_uid_interval = boost::icl::construct<boost::icl::discrete_interval<uid_t>>(0x1, 0x1, boost::icl::interval_bounds::closed());

    auto its_gid_interval = boost::icl::construct<boost::icl::discrete_interval<gid_t>>(0x2, 0x2, boost::icl::interval_bounds::closed());
    boost::icl::interval_set<gid_t> its_gid_interval_set;
    its_gid_interval_set.insert(its_gid_interval);
    one->credentials_ += std::make_pair(its_uid_interval, its_gid_interval_set);
    one->allow_who_ = true;

    for (auto const& policy : {none, one}) {
        std::vector<unsigned char> buffer;
        if (policy) {
            ASSERT_TRUE(policy->serialize(buffer));
        }
        auto cmd = protocol::create_update_security_policy_cmd(0x7, 0x13, buffer);

        auto old_cmd = protocol::update_security_policy_command();
        old_cmd.set_client(cmd.header_.client_);
        old_cmd.set_update_id(cmd.payload_.update_id_);
        old_cmd.set_policy(policy);

        auto new_wire = to_wire(cmd);
        auto old_wire = old_to_wire(old_cmd);

        EXPECT_EQ(new_wire, old_wire);
    }
}

TEST(ut_compatibility_security_command, distribute_security_policy) {
    std::shared_ptr<policy> example_one = std::make_shared<policy>();
    {
        auto its_uid_interval =
                boost::icl::construct<boost::icl::discrete_interval<uid_t>>(0x1, 0x1, boost::icl::interval_bounds::closed());
        auto its_gid_interval =
                boost::icl::construct<boost::icl::discrete_interval<gid_t>>(0x2, 0x2, boost::icl::interval_bounds::closed());
        boost::icl::interval_set<gid_t> its_gid_interval_set;
        its_gid_interval_set.insert(its_gid_interval);
        example_one->credentials_ += std::make_pair(its_uid_interval, its_gid_interval_set);
        example_one->allow_who_ = true;
    }

    std::shared_ptr<policy> example_two = std::make_shared<policy>();
    {
        auto its_uid_interval =
                boost::icl::construct<boost::icl::discrete_interval<uid_t>>(0x2, 0x2, boost::icl::interval_bounds::closed());
        auto its_gid_interval =
                boost::icl::construct<boost::icl::discrete_interval<gid_t>>(0x3, 0x3, boost::icl::interval_bounds::closed());
        boost::icl::interval_set<gid_t> its_gid_interval_set;
        its_gid_interval_set.insert(its_gid_interval);
        example_two->credentials_ += std::make_pair(its_uid_interval, its_gid_interval_set);
        example_two->allow_who_ = true;
    }

    std::vector<std::shared_ptr<policy>> none;
    std::vector<std::shared_ptr<policy>> one{example_one};
    std::vector<std::shared_ptr<policy>> two{example_one, example_two};

    for (auto const& policies : {none, one, two}) {
        std::vector<std::vector<byte_t>> buffer;

        for (auto const& p : policies) {
            auto& mem = buffer.emplace_back();
            ASSERT_TRUE(p->serialize(mem));
        }
        std::vector<std::span<byte_t const>> new_input;
        std::map<uint32_t, std::shared_ptr<payload>> old_input;
        uint32_t count = 0;
        for (auto const& buf : buffer) {
            new_input.push_back(std::span<byte_t const>(buf));
            auto& payload = old_input[++count];
            payload = std::make_shared<payload_impl>();
            payload->set_data(buf);
        }

        auto cmd = protocol::create_distribute_security_policy_cmd(0x13, new_input);

        auto old_cmd = protocol::distribute_security_policies_command();
        old_cmd.set_client(cmd.header_.client_);
        old_cmd.set_payloads(old_input);

        auto new_wire = to_wire(cmd);
        auto old_wire = old_to_wire(old_cmd);

        EXPECT_EQ(new_wire, old_wire);
    }
}

} // namespace vsomeip_v3::protocol

#pragma GCC diagnostic pop
