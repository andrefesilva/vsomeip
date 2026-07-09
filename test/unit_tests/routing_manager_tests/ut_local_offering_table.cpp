// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "../../../implementation/routing/include/local_offering_table.hpp"

// Characterization suite pinning the observable behaviour of every public method
// of local_offering_table (sorted-vector storage).

namespace vsomeip_v3 {

class local_offering_table_test : public ::testing::Test {
protected:
    using entry = local_offering_table::entry;

    entry make_entry(service_t _service, instance_t _instance, major_version_t _major, minor_version_t _minor, client_t _client) const {
        return entry{_service, _instance, _major, _minor, _client};
    }

    local_offering_table table_;
};

// ---------------------------------------------------------------------------
// Empty-table behaviour
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, empty_table_queries) {
    EXPECT_EQ(table_.find_client(0x1111, 0x1, 0x2), VSOMEIP_ROUTING_CLIENT);
    EXPECT_FALSE(table_.find_entry(0x1111, 0x1, ANY_MAJOR).has_value());
    EXPECT_TRUE(table_.find_clients(0x1111, 0x1, 0x1).empty());
    EXPECT_TRUE(table_.find_clients(0x1111, 0x2, ANY_MAJOR).empty());
    EXPECT_TRUE(table_.find_clients(0x1111, ANY_INSTANCE, ANY_MAJOR).empty());
    EXPECT_FALSE(table_.is_available(0x1111, 0x1, 0x1));
    EXPECT_FALSE(table_.remove(0x1111, 0x1, 0x3));
}

// ---------------------------------------------------------------------------
// add() return value (is_new) semantics
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, add_new_instance_is_new) {
    EXPECT_TRUE(table_.add(0x1111, 0x1, 0x2, 0x3, 0x10));
}

TEST_F(local_offering_table_test, add_same_instance_same_major_not_new) {
    EXPECT_TRUE(table_.add(0x1111, 0x1, 0x2, 0x3, 0x10));
    EXPECT_FALSE(table_.add(0x1111, 0x1, 0x2, 0x9, 0x11));
}

TEST_F(local_offering_table_test, add_same_instance_different_concrete_major_is_new) {
    EXPECT_TRUE(table_.add(0x1111, 0x1, 0x2, 0x3, 0x10));
    EXPECT_TRUE(table_.add(0x1111, 0x1, 0x5, 0x3, 0x10));
}

TEST_F(local_offering_table_test, add_same_instance_any_major_not_new) {
    EXPECT_TRUE(table_.add(0x1111, 0x1, 0x2, 0x3, 0x10));
    EXPECT_TRUE(table_.add(0x1111, 0x1, ANY_MAJOR, 0x3, 0x10));
    EXPECT_FALSE(table_.add(0x1111, 0x1, ANY_MAJOR, 0x1, 0x10));
}

TEST_F(local_offering_table_test, add_same_instance_default_major_not_new) {
    EXPECT_TRUE(table_.add(0x1111, 0x1, 0x2, 0x3, 0x10));
    EXPECT_TRUE(table_.add(0x1111, 0x1, DEFAULT_MAJOR, 0x3, 0x10));
    EXPECT_FALSE(table_.add(0x1111, 0x1, DEFAULT_MAJOR, 0x2, 0x10));
}

TEST_F(local_offering_table_test, add_overwrites_stored_entry) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x1111, 0x1, 0x2, 0x9, 0x11);

    auto found = table_.find_entry(0x1111, 0x1, 0x2);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, make_entry(0x1111, 0x1, 0x2, 0x9, 0x11));
    EXPECT_EQ(table_.find_client(0x1111, 0x1, 0x2), 0x11);
}

// ---------------------------------------------------------------------------
// find_client / find_entry point lookups
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, find_client_and_entry_after_add) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);

    EXPECT_EQ(table_.find_client(0x1111, 0x1, 0x2), 0x10);

    auto found = table_.find_entry(0x1111, 0x1, 0x2);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, make_entry(0x1111, 0x1, 0x2, 0x3, 0x10));
}

TEST_F(local_offering_table_test, find_client_miss_returns_routing_client) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_EQ(table_.find_client(0x1111, 0x2, 0x2), VSOMEIP_ROUTING_CLIENT);
    EXPECT_EQ(table_.find_client(0x2222, 0x1, 0x2), VSOMEIP_ROUTING_CLIENT);
    EXPECT_EQ(table_.find_client(0x1111, 0x1, 0x1), VSOMEIP_ROUTING_CLIENT);
}

TEST_F(local_offering_table_test, find_entry_miss_returns_nullopt) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_FALSE(table_.find_entry(0x1111, 0x2, 0x2).has_value());
    EXPECT_FALSE(table_.find_entry(0x2222, 0x1, 0x2).has_value());
    EXPECT_FALSE(table_.find_entry(0x1111, 0x1, 0x3).has_value());
}

// ---------------------------------------------------------------------------
// remove()
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, remove_existing) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_TRUE(table_.remove(0x1111, 0x1, 0x2));
    EXPECT_FALSE(table_.find_entry(0x1111, 0x1, 0x2).has_value());
    EXPECT_EQ(table_.find_client(0x1111, 0x1, 0x2), VSOMEIP_ROUTING_CLIENT);
}

TEST_F(local_offering_table_test, remove_missing_returns_false) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_FALSE(table_.remove(0x1111, 0x2, 0x2));
    EXPECT_FALSE(table_.remove(0x2222, 0x1, 0x2));
    EXPECT_FALSE(table_.remove(0x1111, 0x1, 0x1));
}

TEST_F(local_offering_table_test, remove_any_major_deletes_all_versions) {
    table_.add(0x1111, 0x1, 0x1, 0x3, 0x10);
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x11);
    table_.add(0x1111, 0x1, 0x3, 0x3, 0x11);
    table_.add(0x1111, 0x2, 0x3, 0x3, 0x11);

    EXPECT_TRUE(table_.remove(0x1111, 0x1, ANY_MAJOR));
    auto kept = table_.find_entry(0x1111, 0x2, 0x3);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(*kept, make_entry(0x1111, 0x2, 0x3, 0x3, 0x11));
}

TEST_F(local_offering_table_test, remove_keeps_other_instances) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x1111, 0x2, 0x2, 0x3, 0x11);

    EXPECT_TRUE(table_.remove(0x1111, 0x1, 0x2));
    EXPECT_FALSE(table_.find_entry(0x1111, 0x1, 0x2).has_value());

    auto kept = table_.find_entry(0x1111, 0x2, 0x2);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(*kept, make_entry(0x1111, 0x2, 0x2, 0x3, 0x11));
}
TEST_F(local_offering_table_test, remove_keeps_other_version) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x1111, 0x1, 0x3, 0x3, 0x11);

    EXPECT_TRUE(table_.remove(0x1111, 0x1, 0x2));
    EXPECT_FALSE(table_.find_entry(0x1111, 0x1, 0x2).has_value());

    auto kept = table_.find_entry(0x1111, 0x1, 0x3);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(*kept, make_entry(0x1111, 0x1, 0x3, 0x3, 0x11));
}

// ---------------------------------------------------------------------------
// find_clients()
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, find_clients_concrete_instance) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_EQ(table_.find_clients(0x1111, 0x1, 0x2), (std::set<client_t>{0x10}));
    EXPECT_EQ(table_.find_clients(0x1111, 0x1, ANY_MAJOR), (std::set<client_t>{0x10}));
    EXPECT_EQ(table_.find_clients(0x1111, 0x1, 0x1), (std::set<client_t>{}));
}

TEST_F(local_offering_table_test, find_clients_any_instance_collects_all) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x1111, 0x2, 0x2, 0x3, 0x11);
    table_.add(0x1111, 0x3, 0x2, 0x3, 0x12);

    EXPECT_EQ(table_.find_clients(0x1111, ANY_INSTANCE, ANY_MAJOR), (std::set<client_t>{0x10, 0x11, 0x12}));
}

TEST_F(local_offering_table_test, find_clients_any_instance_deduplicates) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x1111, 0x2, 0x2, 0x3, 0x10);

    EXPECT_EQ(table_.find_clients(0x1111, ANY_INSTANCE, ANY_MAJOR), (std::set<client_t>{0x10}));
}

TEST_F(local_offering_table_test, find_clients_unknown_service_is_empty) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_TRUE(table_.find_clients(0x2222, ANY_INSTANCE, ANY_MAJOR).empty());
    EXPECT_TRUE(table_.find_clients(0x1111, 0x9, ANY_MAJOR).empty());
}

// ---------------------------------------------------------------------------
// is_available() / has_available() wildcard + version matrix
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, is_available_major_matching) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);

    EXPECT_TRUE(table_.is_available(0x1111, 0x1, 0x2)); // exact major
    EXPECT_TRUE(table_.is_available(0x1111, 0x1, ANY_MAJOR)); // any major
    EXPECT_TRUE(table_.is_available(0x1111, 0x1, DEFAULT_MAJOR)); // default major
    EXPECT_FALSE(table_.is_available(0x1111, 0x1, 0x5)); // wrong major
    EXPECT_FALSE(table_.is_available(0x1111, 0x9, 0x2)); // wrong instance
    EXPECT_FALSE(table_.is_available(0x2222, 0x1, 0x2)); // wrong service
}

TEST_F(local_offering_table_test, is_available_any_service) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x2222, 0x5, 0x4, 0x3, 0x11);

    EXPECT_TRUE(table_.is_available(ANY_SERVICE, 0x5, 0x4));
    EXPECT_TRUE(table_.is_available(ANY_SERVICE, 0x1, ANY_MAJOR));
    EXPECT_FALSE(table_.is_available(ANY_SERVICE, 0x5, 0x2)); // wrong major for instance 0x5
}

TEST_F(local_offering_table_test, is_available_any_instance) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x1111, 0x2, 0x4, 0x3, 0x11);

    EXPECT_TRUE(table_.is_available(0x1111, ANY_INSTANCE, 0x4));
    EXPECT_TRUE(table_.is_available(0x1111, ANY_INSTANCE, ANY_MAJOR));
    EXPECT_FALSE(table_.is_available(0x1111, ANY_INSTANCE, 0x9));
}

TEST_F(local_offering_table_test, is_available_any_service_any_instance) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_TRUE(table_.is_available(ANY_SERVICE, ANY_INSTANCE, ANY_MAJOR));
    EXPECT_TRUE(table_.is_available(ANY_SERVICE, ANY_INSTANCE, 0x2));
    EXPECT_FALSE(table_.is_available(ANY_SERVICE, ANY_INSTANCE, 0x5));
}

TEST_F(local_offering_table_test, has_available_minor_matching) {
    table_.add(0x1111, 0x1, 0x2, 0x10, 0x10); // stored minor = 0x10

    EXPECT_TRUE(table_.has_available(0x1111, 0x1, 0x2, 0x10)); // equal minor
    EXPECT_TRUE(table_.has_available(0x1111, 0x1, 0x2, 0x5)); // requested < stored
    EXPECT_TRUE(table_.has_available(0x1111, 0x1, 0x2, ANY_MINOR)); // any minor
    EXPECT_TRUE(table_.has_available(0x1111, 0x1, 0x2, DEFAULT_MINOR)); // default minor
    EXPECT_FALSE(table_.has_available(0x1111, 0x1, 0x2, 0x20)); // requested > stored
    EXPECT_FALSE(table_.has_available(0x1111, 0x1, 0x5, 0x5)); // wrong major
}

// ---------------------------------------------------------------------------
// for_each_available() traversal order + early stop
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, for_each_available_visits_in_sorted_order) {
    // Insert out of order; traversal must yield ascending (service, instance).
    table_.add(0x2222, 0x5, 0x1, 0x0, 0x11);
    table_.add(0x1111, 0x3, 0x1, 0x0, 0x12);
    table_.add(0x1111, 0x1, 0x1, 0x0, 0x10);

    std::vector<entry> visited;
    table_.for_each_available(ANY_SERVICE, ANY_INSTANCE, ANY_MAJOR, ANY_MINOR, [&visited](const entry& _e) {
        visited.push_back(_e);
        return true;
    });

    std::vector<entry> expected{
            make_entry(0x1111, 0x1, 0x1, 0x0, 0x10),
            make_entry(0x1111, 0x3, 0x1, 0x0, 0x12),
            make_entry(0x2222, 0x5, 0x1, 0x0, 0x11),
    };
    EXPECT_EQ(visited, expected);
}

TEST_F(local_offering_table_test, for_each_available_stops_when_callback_returns_false) {
    table_.add(0x1111, 0x1, 0x1, 0x0, 0x10);
    table_.add(0x1111, 0x2, 0x1, 0x0, 0x11);
    table_.add(0x1111, 0x3, 0x1, 0x0, 0x12);

    int count = 0;
    table_.for_each_available(0x1111, ANY_INSTANCE, ANY_MAJOR, ANY_MINOR, [&count](const entry&) {
        ++count;
        return false; // stop after the first match
    });
    EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------------
// remove_all_for_client()
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, remove_all_for_client_removes_and_returns_sorted) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    table_.add(0x2222, 0x5, 0x4, 0x3, 0x10);
    table_.add(0x1111, 0x2, 0x2, 0x3, 0x11);

    auto removed = table_.remove_all_for_client(0x10);

    std::vector<entry> expected{
            make_entry(0x1111, 0x1, 0x2, 0x3, 0x10),
            make_entry(0x2222, 0x5, 0x4, 0x3, 0x10),
    };
    EXPECT_EQ(removed, expected);

    EXPECT_FALSE(table_.find_entry(0x1111, 0x1, 0x2).has_value());
    EXPECT_FALSE(table_.find_entry(0x2222, 0x5, 0x4).has_value());

    auto kept = table_.find_entry(0x1111, 0x2, 0x2);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(*kept, make_entry(0x1111, 0x2, 0x2, 0x3, 0x11));
}

TEST_F(local_offering_table_test, remove_all_for_client_unknown_is_empty) {
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);
    EXPECT_TRUE(table_.remove_all_for_client(0x99).empty());
    EXPECT_TRUE(table_.find_entry(0x1111, 0x1, 0x2).has_value());
}

// ---------------------------------------------------------------------------
// clear()
// ---------------------------------------------------------------------------
TEST_F(local_offering_table_test, clear_returns_all_sorted_and_empties) {
    table_.add(0x2222, 0x5, 0x4, 0x3, 0x11);
    table_.add(0x1111, 0x1, 0x2, 0x3, 0x10);

    auto removed = table_.clear();

    std::vector<entry> expected{
            make_entry(0x1111, 0x1, 0x2, 0x3, 0x10),
            make_entry(0x2222, 0x5, 0x4, 0x3, 0x11),
    };
    EXPECT_EQ(removed, expected);

    EXPECT_FALSE(table_.find_entry(0x1111, 0x1, 0x2).has_value());
    EXPECT_FALSE(table_.find_entry(0x2222, 0x5, 0x4).has_value());
    EXPECT_TRUE(table_.clear().empty());
}
} // namespace vsomeip_v3
