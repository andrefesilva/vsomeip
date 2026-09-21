// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "common/test_main.hpp"

int main(int argc, char** argv) {
    // These tests exercise configuration source resolution, which gives the
    // process-wide VSOMEIP_CONFIGURATION variable priority over the supplied
    // path. The generated default logger configuration must not be injected.
    return test_main(argc, argv, /*_abort_on_failure=*/true, /*_setup=*/{}, /*_tearDown=*/{},
                     /*_default_logger_config=*/false);
}
