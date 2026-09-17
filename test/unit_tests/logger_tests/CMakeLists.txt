# Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

project("unit_tests_logger_tests" LANGUAGES CXX)

# ----------------------------------------------------------------------------
# Executables and libraries to link
# ----------------------------------------------------------------------------
# The logger configuration is process-wide and immutable, so the sinks-enabled,
# no-sink and application-first handoff scenarios each need their own executable.
add_executable(unit_tests_logger_tests main.cpp ut_logger.cpp)
add_executable(unit_tests_logger_no_sink_tests main_no_sinks.cpp ut_logger_no_sinks.cpp)
add_executable(unit_tests_logger_configuration_handoff_tests main_configuration_handoff.cpp ut_logger_configuration_handoff.cpp)

foreach(target unit_tests_logger_tests unit_tests_logger_no_sink_tests unit_tests_logger_configuration_handoff_tests)
    target_link_libraries(
        ${target}
        vsomeip3-test
        ${Boost_LIBRARIES}
        ${DL_LIBRARY}
        gtest
        vsomeip_utilities
    )

    add_test(NAME ${target} COMMAND ${target})

    add_dependencies(build_unit_tests ${target})
endforeach()
