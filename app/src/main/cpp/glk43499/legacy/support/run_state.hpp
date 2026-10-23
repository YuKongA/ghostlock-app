#ifndef GHOSTLOCK_SUPPORT_RUN_STATE_HPP
#define GHOSTLOCK_SUPPORT_RUN_STATE_HPP

/*
 * Attack step status reporting over stdio, enabled by --enable-status-record.
 *
 * The native process never writes the app's state file: it emits one marker
 * line per step on stdout and waits (bounded) for the app's ACK on stdin. The
 * app persists the status, so a kernel panic that kills this process still
 * leaves the last "in_progress" step on disk for the next launch to read.
 *
 * When disabled every call is a no-op and stdin/stdout keep the legacy
 * behaviour.
 */
#include <string>
#include <string_view>

namespace ghostlock::support::run_state {
    /* Protocol helpers (pure, host-testable). */
    std::string format_event(std::string_view step, std::string_view status);

    [[nodiscard]] bool is_ack(std::string_view line);

    void configure(bool enabled) noexcept;

    [[nodiscard]] bool enabled() noexcept;

    /* Announce that `step` is starting and wait for the app to persist it. */
    void enter(const char *step) noexcept;

    /* Announce that `step` finished and wait for the app to persist it. */
    void complete(const char *step) noexcept;
} // namespace ghostlock::support::run_state

#endif
