// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <unordered_map>

#include "common_types.hpp"

namespace vsomeip_v3 {

template<class T>
using service_instance_map = std::unordered_map<service_instance_t, T>;

template<class T>
using versioned_service_instance_map = std::unordered_map<versioned_service_instance_t, T>;

} // namespace vsomeip_v3
