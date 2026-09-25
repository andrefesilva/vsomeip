// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include "../../../implementation/routing/include/local_service_table.hpp"

namespace vsomeip_v3::testing {

protocol::service_data convert(protocol::service const& _in) {
    return {.key_ = {.service = _in.service_, .instance = _in.instance_, .major = _in.major_}, .minor_version_ = _in.minor_};
}
protocol::service convert(protocol::service_data const& _in) {
    return {_in.key_.service, _in.key_.instance, _in.key_.major, _in.minor_version_};
}

struct ut_sanity_check : ::testing::Test {

    std::vector<protocol::service_data> table_content() const {
        auto content = table_.view();
        std::vector<protocol::service_data> out(content.begin(), content.end());
        return out;
    }
    std::vector<protocol::service_data> set_content() const {
        std::vector<protocol::service_data> out;
        for (auto const& s : set_) {
            out.push_back(convert(s));
        }
        return out;
    }
    local_service_table table_;
    std::set<protocol::service> set_;
};

TEST_F(ut_sanity_check, check_book_keeping) {
    auto service_d = protocol::service_data{.key_ = {.service = 0xd, .instance = 2, .major = 2}, .minor_version_ = 1};
    std::vector<protocol::service_data> many{
            {.key_ = {.service = 0xc, .instance = 1, .major = 1}, .minor_version_ = 0},
            {.key_ = {.service = 0xc, .instance = 3, .major = 1}, .minor_version_ = 0},
            {.key_ = {.service = 0xc, .instance = 3, .major = 1}, .minor_version_ = 1}, // this should be removed
            {.key_ = {.service = 0xc, .instance = 3, .major = 2}, .minor_version_ = 0}, // this should be removed unfortunately atm.
            service_d};

    for (auto const& s : many) {
        table_.insert(s);
        set_.insert(convert(s));
    }

    ASSERT_EQ(table_content(), set_content());
    ASSERT_EQ(table_.size(), 3); // this should change, once we support multiple major versions

    // this is a new feature.. removing any instance should really realse _any_ instance..
    table_.remove({.key_ = {.service = 0xc, .instance = ANY_INSTANCE, .major = ANY_MAJOR}, .minor_version_ = ANY_MINOR});

    auto new_content = table_content();
    std::vector<protocol::service_data> cleaned{service_d};
    EXPECT_EQ(new_content, cleaned);
}

}
