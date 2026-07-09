// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../include/service_instance_map.hpp"
#include "../include/utility.hpp"

namespace vsomeip_v3 {

std::ostream& operator<<(std::ostream& _os, versioned_service_instance_t const& _s) {
    return _os << hex4(_s.service) << "." << hex4(_s.instance) << "." << static_cast<int>(_s.major);
}
}
