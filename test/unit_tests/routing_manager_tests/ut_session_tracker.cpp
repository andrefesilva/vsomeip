// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include "../../../implementation/routing/include/session_tracker.hpp"

namespace vsomeip_v3 {

TEST(session_tracker, untracked_client_gets_default_starting_session) {
    session_tracker tracker;
    EXPECT_EQ(1, tracker.track(0x1234));
}

TEST(session_tracker, untracked_client_gets_custom_starting_session) {
    session_tracker tracker{7, 100};
    EXPECT_EQ(7, tracker.track(0x1234));
}

TEST(session_tracker, observe_alone_does_not_change_other_clients) {
    session_tracker tracker{5, 100};
    tracker.update(0x1, 50);
    EXPECT_EQ(5, tracker.track(0x2));
}

TEST(session_tracker, retracking_without_observing_applies_offset_to_starting_session) {
    session_tracker tracker{1, 100};
    EXPECT_EQ(1, tracker.track(0x1));
    EXPECT_EQ(101, tracker.track(0x1));
    EXPECT_EQ(201, tracker.track(0x1));
}

TEST(session_tracker, observed_session_plus_offset_is_used_on_next_track) {
    session_tracker tracker{1, 100};
    tracker.track(0x1);
    tracker.update(0x1, 500);
    EXPECT_EQ(600, tracker.track(0x1));
}

TEST(session_tracker, latest_observation_wins) {
    session_tracker tracker{1, 100};
    tracker.update(0x1, 10);
    tracker.update(0x1, 20);
    EXPECT_EQ(120, tracker.track(0x1));
}

TEST(session_tracker, observe_on_unknown_client_starts_tracking_it) {
    session_tracker tracker{1, 100};
    tracker.update(0x1, 42);
    EXPECT_EQ(142, tracker.track(0x1));
}

TEST(session_tracker, clients_are_tracked_independently) {
    session_tracker tracker{1, 100};
    tracker.update(0x1, 10);
    tracker.update(0x2, 20);
    EXPECT_EQ(110, tracker.track(0x1));
    EXPECT_EQ(120, tracker.track(0x2));
}

TEST(session_tracker, zero_offset_reuses_observed_session) {
    session_tracker tracker{1, 0};
    tracker.update(0x1, 33);
    EXPECT_EQ(33, tracker.track(0x1));
}

TEST(session_tracker, session_wraps_around_on_overflow) {
    session_tracker tracker{1, 100};
    tracker.update(0x1, 0xFFFF);
    EXPECT_EQ(99, tracker.track(0x1));
}

TEST(session_tracker, session_zero_after_wrap_is_replaced_by_one) {
    session_tracker tracker{1, 100};
    tracker.update(0x1, static_cast<session_t>(0x10000 - 100));
    EXPECT_EQ(1, tracker.track(0x1));
}

} // namespace vsomeip_v3
