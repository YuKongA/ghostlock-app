#pragma once
/** @file terminal/execution.hpp
 *  A6-4: the declarative finishing -- what runs, when, and how it reports.
 *
 *  The plan declares at most one payload per moment, and the moment decides
 *  whether it runs at all: a run that succeeded reaches on_success and always, a
 *  run that failed reaches on_failure and always. Nothing else runs, and nothing
 *  is inferred at runtime -- a moment with no payload is a moment that does
 *  nothing, not one that falls back to something else (the kernel fallback is
 *  the one exception, and it lives on the C++ side of this boundary).
 *
 *  Running is the only part of the terminal path that starts a process, and the
 *  payload arrives mode 0600 from the store: the owner-executable bit is added
 *  here, immediately before the spawn, and nowhere else.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::terminal {
    /** The three declared moments. Order matters: success, then always. */
    enum class FinishMoment : unsigned char {
        OnSuccess = 0,
        Always = 1,
        OnFailure = 2,
    };

    inline constexpr std::size_t kFinishMoments = 3U;

    [[nodiscard]] constexpr const char *finish_moment_name(FinishMoment moment) noexcept {
        switch (moment) {
            case FinishMoment::OnSuccess: return "on_success";
            case FinishMoment::Always: return "always";
            case FinishMoment::OnFailure: return "on_failure";
        }
        return "unknown";
    }

    /** One declared payload per moment; nullptr means the moment does nothing. */
    struct FinishDeclaration final {
        const char *on_success = nullptr;
        const char *always = nullptr;
        const char *on_failure = nullptr;
    };

    /** What the plan declared for one moment, or nullptr when it declared none. */
    [[nodiscard]] inline const char *declared_payload(const FinishDeclaration &declaration,
                                                      FinishMoment moment) noexcept {
        switch (moment) {
            case FinishMoment::OnSuccess: return declaration.on_success;
            case FinishMoment::Always: return declaration.always;
            case FinishMoment::OnFailure: return declaration.on_failure;
        }
        return nullptr;
    }

    /** True when this moment runs for this outcome. A moment with no payload
     *  never runs, whatever the outcome. */
    [[nodiscard]] inline bool moment_runs(const FinishDeclaration &declaration,
                                          FinishMoment moment, bool succeeded) noexcept {
        if (declared_payload(declaration, moment) == nullptr) {
            return false;
        }
        switch (moment) {
            case FinishMoment::OnSuccess: return succeeded;
            case FinishMoment::OnFailure: return !succeeded;
            case FinishMoment::Always: return true;
        }
        return false;
    }

    /** The outcome of running one payload. */
    struct ExecResult final {
        bool ok = false;
        /** The payload's exit status, valid when ok is true. */
        int exit_code = 0;
        /** A registered refusal name (see UML 3.2), never an invented one. */
        const char *reason = "executor-unavailable";
    };

    /** Add the owner-executable bit and run the payload, waiting for it. */
    [[nodiscard]] ExecResult run_payload(const char *path) noexcept;
}  // namespace ghostlock::terminal
