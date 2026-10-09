#pragma once
/** @file support/neutrals.hpp
 *  The neutral support surface -- and nothing else.
 *
 *  Lesson encoded here: in the old tree fourteen attack-specific helpers
 *  (spray / payload / route / kernel-write plumbing) had been parked in
 *  support and later had to be moved out. Support therefore carries only
 *  policy-free vocabulary: a status word and a scope-exit guard, neither of
 *  which knows about backends, kernels, routes, payloads or windows.
 *
 *  The purity is machine-checked: "make -C src check" greps this directory for
 *  forbidden tokens (spray, payload, route_, kernel_, exploit, ...), so a
 *  misplaced helper fails the gate instead of quietly living here.
 *
 *  Numbers policy (Q2 rule 1): no geometry, offset, timing or tuning value.
 */
#include <cstddef>

namespace ghostlock::support {
    /** Outcome of a step whose result carries no data. */
    enum class Status : unsigned char {
        Ok = 0,
        Failed = 1,
    };

    [[nodiscard]] constexpr bool succeeded(Status status) noexcept {
        return status == Status::Ok;
    }
    [[nodiscard]] constexpr bool failed(Status status) noexcept {
        return status == Status::Failed;
    }

    /** Neutral RAII guard: runs the caller's action once, at scope exit.
     *  Ownership: the guard owns the action; moving transfers it, copying is
     *  denied. It performs no allocation and calls nothing indirectly. */
    template <class Action>
    class ScopeExit final {
      public:
        explicit ScopeExit(Action action) noexcept : action_(action) {}
        ScopeExit(const ScopeExit &) = delete;
        ScopeExit &operator=(const ScopeExit &) = delete;
        ScopeExit(ScopeExit &&other) noexcept : action_(other.action_), armed_(other.armed_) {
            other.armed_ = false;
        }
        ScopeExit &operator=(ScopeExit &&) = delete;
        ~ScopeExit() noexcept {
            if (armed_) {
                action_();
            }
        }
        /** Disarm: the caller takes over the responsibility. */
        void dismiss() noexcept { armed_ = false; }

      private:
        Action action_;
        bool armed_ = true;
    };

    /** Deduction helper so callers never spell the action type. */
    template <class Action>
    [[nodiscard]] ScopeExit<Action> on_scope_exit(Action action) noexcept {
        return ScopeExit<Action>{action};
    }
}  // namespace ghostlock::support
