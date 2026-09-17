// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <ios>
#include <utility>
#include <vsomeip/runtime.hpp>

#include "../include/logger_impl.hpp"
#include "../../configuration/include/configuration.hpp"
#include "../../configuration/include/configuration_impl.hpp"

namespace vsomeip_v3::logger {

namespace {

logger_impl::config to_config(const configuration& _configuration) {
    return {_configuration.has_console_log(), _configuration.has_dlt_log(), _configuration.has_file_log(), _configuration.get_loglevel(),
            _configuration.get_logfile()};
}

logger_impl::config load_configuration() {
    return to_config(*cfg::configuration_impl::get_logger_configuration());
}

#ifdef USE_DLT

constexpr const char* VSOMEIP_LOG_DEFAULT_CONTEXT_ID = "VSIP";
constexpr const char* VSOMEIP_LOG_DEFAULT_CONTEXT_NAME = "vSomeIP context";

// Unregisters the eternal logger's DLT context at exit, decoupled from the immortal logger instance.
// This sets an internal pointer to null, which will cause libdlt to discard any subsequent attempt to log,
// mitigating any potential libdlt-side issues in log-after-main scenarios.
struct context_unregistrar {
    DltContext* context;
    ~context_unregistrar() { DLT_UNREGISTER_CONTEXT(*context); }
};

#endif

} // namespace

logger_impl::logger_impl(config _config) : config_{std::move(_config)} {
    if (config_.file_enabled) {
        log_file_ = std::ofstream{config_.logfile, std::ios_base::out | std::ios_base::app};
    }

#ifdef USE_DLT
    auto context_id = runtime::get_property("LogContext");
    if (context_id == "") {
        context_id = VSOMEIP_LOG_DEFAULT_CONTEXT_ID;
    }
    DLT_REGISTER_CONTEXT(dlt_context_, context_id.c_str(), VSOMEIP_LOG_DEFAULT_CONTEXT_NAME);

    // Registered after libdlt's own atexit (via DLT_REGISTER_CONTEXT above), so it runs first and
    // unregisters before libdlt tears itself down.
    static context_unregistrar const unregistrar{&dlt_context_};
#endif
}

const logger_impl::config& logger_impl::get_configuration() const {
    return config_;
}

void logger_impl::log_to_file(std::string_view _msg) {
    std::scoped_lock its_lock{log_file_mutex_};
    if (log_file_.is_open()) {
        log_file_ << _msg << std::flush;
    }
}

#ifdef USE_DLT

// Note: _msg is expected to include a terminating null byte
void logger_impl::log_to_dlt(level_e _level, std::string_view _msg) {
    // Prepare log level
    DltLogLevelType its_level{};
    switch (_level) {
    case level_e::LL_FATAL:
        its_level = DLT_LOG_FATAL;
        break;
    case level_e::LL_ERROR:
        its_level = DLT_LOG_ERROR;
        break;
    case level_e::LL_WARNING:
        its_level = DLT_LOG_WARN;
        break;
    case level_e::LL_INFO:
        its_level = DLT_LOG_INFO;
        break;
    case level_e::LL_DEBUG:
        its_level = DLT_LOG_DEBUG;
        break;
    case level_e::LL_VERBOSE:
        its_level = DLT_LOG_VERBOSE;
        break;
    default:
        its_level = DLT_LOG_DEFAULT;
    };

#if defined(DLT_STRING_PUBLIC)
    // If libdlt supports privacy-aware logging, ensure that all messages are marked
    // as public. Trace data containing message payloads (and thus, potentially sensitive
    // data) is logged through a different code path and remains private.
    DLT_LOG(dlt_context_, its_level, DLT_STRING_PUBLIC(_msg.data()));
#elif defined(DLT_SIZED_CSTRING)
    // Some versions of libdlt provide support for sized strings, which is more optimal than
    // DLT_LOG_STRING as it saves a call to strlen().
    DLT_LOG(dlt_context_, its_level, DLT_SIZED_CSTRING(_msg.data(), static_cast<uint16_t>(_msg.size())));
#else
    // Fallback to legacy log macro
    DLT_LOG_STRING(dlt_context_, its_level, _msg.data());
#endif
}

#endif

logger_impl* logger_impl::get() {
    return get_or_create(nullptr);
}

void logger_impl::init(const configuration& _configuration) {
    static_cast<void>(get_or_create(&_configuration));
}

logger_impl* logger_impl::get_or_create(const configuration* _configuration) {
    // Immortal singleton, so detached threads may keep logging after main() with no shutdown races.
    static auto* const instance = new logger_impl(
            _configuration != nullptr ? to_config(*_configuration)
                                      : load_configuration()); // NOSONAR: False positive S6018, also 'new' and "leak" is intentional
    return instance;
}

} // namespace vsomeip_v3::logger
