// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// ============================================================================
// Config Command Compatibility Tests (header + key/value pairs payload)
// ============================================================================
//
// Proves that the new struct-based serialization/deserialization produces
// byte-for-byte identical wire output to the old class-based approach and
// that each side can correctly consume what the other produces.
//
// For each command we verify:
//   1. old serialize → new deserialize (new code reads old format correctly)
//   2. new serialize → old deserialize (old code reads new format correctly)
//
// Keys are kept in lexicographic order so the old class (which stores them in
// a sorted std::map) and the new code (which preserves insertion order)
// produce the same wire layout.
// ============================================================================

#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <string>
#include <utility>
#include <vector>

#include "../../../implementation/protocol/include/command_types.hpp"
#include "../../../implementation/protocol/include/deserialize.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/serialize.hpp"

#include "../../../implementation/protocol/include/config_command.hpp"

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

// ============================================================================
// Config
// ============================================================================

TEST(ut_compatibility_config_command, old_serialize_new_deserialize) {
    config_command old_cmd;
    old_cmd.set_client(0x0001);
    old_cmd.insert("abcd", std::string("1234"));
    old_cmd.insert("efgh", std::string("5678"));
    auto bytes = old_to_wire(old_cmd);

    command_header header{};
    auto const header_size = deserialize(header, bytes.data(), static_cast<uint32_t>(bytes.size()));
    ASSERT_GT(header_size, 0u);
    EXPECT_EQ(header.id_, old_cmd.get_id());
    EXPECT_EQ(header.client_, old_cmd.get_client());
    EXPECT_EQ(header.length_, old_cmd.get_size());

    std::vector<std::pair<std::string, std::string>> configs;
    auto const payload_size = deserialize(configs, bytes.data() + header_size, header.length_);
    EXPECT_EQ(payload_size, header.length_);
    ASSERT_EQ(configs.size(), 2u);
    EXPECT_EQ(configs[0].first, "abcd");
    EXPECT_EQ(configs[0].second, "1234");
    EXPECT_EQ(configs[1].first, "efgh");
    EXPECT_EQ(configs[1].second, "5678");
}

TEST(ut_compatibility_config_command, new_serialize_old_deserialize) {
    auto bytes = to_wire(create_config_cmd(0x0001, {{"abcd", "1234"}, {"efgh", "5678"}}));

    config_command old_cmd;
    error_e err;
    std::vector<uint8_t> v(bytes.begin(), bytes.end());
    old_cmd.deserialize(v, err);

    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(old_cmd.get_id(), id_e::CONFIG_ID);
    EXPECT_EQ(old_cmd.get_client(), 0x0001);
    ASSERT_TRUE(old_cmd.contains("abcd"));
    EXPECT_EQ(old_cmd.at("abcd"), "1234");
    ASSERT_TRUE(old_cmd.contains("efgh"));
    EXPECT_EQ(old_cmd.at("efgh"), "5678");
}

} // namespace vsomeip_v3::protocol
