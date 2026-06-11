// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>

/// @brief Time for which the service application is active
constexpr auto SERVICE_UP_TIME = std::chrono::seconds(9);

/// @brief Time for which the service application is offering the services and responding to
/// requests, needs to be at least an offer cyclic delay to ensure offers are allowed to be sent to remote clients.
/// In practice, it needs to be longer than the offer cyclic delay to ensure that at least one offer cycle is completed
/// and the service is offered to remote clients.
constexpr auto SERVICE_OFFER_TIME = std::chrono::milliseconds(2000);

/// @brief Time for which the service application stops offering the services
constexpr auto SERVICE_STOP_OFFER_TIME = std::chrono::milliseconds(2);

/// @brief Time for which the client application is active
constexpr auto CLIENT_UP_TIME = std::chrono::seconds(10);
