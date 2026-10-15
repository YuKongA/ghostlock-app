#pragma once
/** @file race/pi_window.hpp
 *  The PI window host: the prohibitions that hold while a waiter is parked, and
 *  the reclaim discipline that must hold on every exit path.
 *
 *  Authority: docs/analysis/pi-window-constraints.md (measured, see task-74).
 *  Measured failure mode that motivates the prohibitions: a syscall issued from
 *  inside the window (sched_setattr -> __sched_setscheduler -> rt_mutex_adjust_pi
 *  -> rt_mutex_adjust_prio_chain) walked the corrupted PI chain and oopsed at
 *  address 0. The window therefore runs with a fixed, checked shape.
 *
 *  Prohibitions (each is machine-checked by "make -C src check"):
 *   - no Lua reference (the window never interprets a script);
 *   - no heap allocation inside PI;
 *   - no logging/stdio inside PI (both allocate and may block, and either lengthens the window);
 *   - no indirect dispatch inside PI: a vtable or function-pointer hop makes the window shape
 *     unverifiable. (Rewritten 2026-10-09 under the user ruling in ADR-0014: what is banned is
 *     the hop taken from inside the window, not a vtable that exists in the image.)
 *   - no tuning number: parameters arrive from the profile (Q2 rule 1).
 *
 *  What PI is, and why the scope is this (2026-10-12 user ruling). The prohibitions bind INSIDE
 *  PI -- the interval a thread spends on the chain the kernel walks -- and not the whole window
 *  body: the body may print once its PI span is over, and the readings a syscall produced inside
 *  PI are carried out in MulticastRouteShared and printed after the last actor stopped (one
 *  flush per attempt: landing/multicast_route.hpp report_pi_readings). Per actor the span is:
 *  waiter_main from the PI-IN mark (the last sync before the park) to the PI-OUT mark (the
 *  completed disarm); owner_main from its target lock to its last unlock; consumer_main and
 *  trigger in full, because both run while the waiter is parked. judge() is outside PI by call
 *  site (it is reached only after stop_threads()), so it prints directly and is a NAMED
 *  exemption in the span ledger rather than a silent hole (tools/window_body_guard.py).
 *  The measured failure and the syscall whitelist: docs/analysis/pi-window-constraints.md.
 *
 *  Earlier text said "the window BODY issues no syscall", which was wider than the hazard it
 *  guards and was already false for this route (the park, the unlock and the trigger's setattr
 *  are the protocol): the 2026-10-12 ruling replaced it with the PI scope above.
 *
 *  Reclaim: the guard owns the window scope and reclaims on EVERY exit path --
 *  success / failure / retry / cancel / early return. The evidence test drives
 *  all five and asserts the reclaim count, so a missing reclaim fails the gate.
 */
#include <cstddef>

#include "contract/resource_scope.hpp"

namespace ghostlock::backend::cve_2026_43499::race {
    using ExitPath = ghostlock::contract::ExitPath;

    /** What the window forbids; one row per prohibition, for the report and the
     *  scan names. */
    struct Prohibition final {
        const char *name;
        const char *why;
    };
    inline constexpr Prohibition kWindowProhibitions[] = {
        {"lua", "the window never interprets a script (structural: this layer has no Lua)"},
        {"allocation", "an allocator call inside PI can block and lengthen the window"},
        {"logging", "stdio and logging allocate and may block while the waiter is parked"},
        {"indirect-dispatch", "a vtable or function-pointer hop inside PI makes the shape unverifiable"},
        {"tuning-number", "parameters come from the profile; the window carries no tuning value"},
    };
    inline constexpr std::size_t kWindowProhibitionCount =
        sizeof(kWindowProhibitions) / sizeof(kWindowProhibitions[0]);

    /** Violation counters: the window body reports what it did, and the guard
     *  turns any non-zero counter into a refused window. */
    struct WindowViolations final {
        unsigned allocations = 0;
        unsigned logs = 0;
        unsigned indirect_calls = 0;
        unsigned lua_calls = 0;

        [[nodiscard]] constexpr bool clean() const noexcept {
            return allocations == 0 && logs == 0 && indirect_calls == 0 && lua_calls == 0;
        }
        [[nodiscard]] constexpr unsigned total() const noexcept {
            return allocations + logs + indirect_calls + lua_calls;
        }
    };

    /** The window guard: entry, violation accounting, reclaim on every path. */
    class WindowGuard final {
      public:
        WindowGuard() noexcept = default;
        ~WindowGuard() noexcept { reclaim(); }
        WindowGuard(const WindowGuard &) = delete;
        WindowGuard &operator=(const WindowGuard &) = delete;

        /** Enter the window on a named exit path. */
        void enter(ExitPath path) noexcept {
            path_ = path;
            open_ = true;
            violations_ = WindowViolations{};
        }
        /** The window body reports a forbidden act; the guard refuses the run. */
        void note_allocation() noexcept { ++violations_.allocations; }
        void note_log() noexcept { ++violations_.logs; }
        void note_indirect_call() noexcept { ++violations_.indirect_calls; }
        void note_lua_call() noexcept { ++violations_.lua_calls; }

        /** Evidence hook: an external counter the reclaim also feeds, so a test
         *  can observe the reclaim that happens in the destructor. Plain pointer,
         *  no call, no allocation. */
        void observe_reclaim(std::size_t *sink) noexcept { sink_ = sink; }

        /** Idempotent reclaim: success, failure, retry, cancel, early return all
         *  reach here (the destructor is the early-return safety net). */
        void reclaim() noexcept {
            if (!open_) {
                return;
            }
            open_ = false;
            reclaimed_ = true;
            ++reclaim_count_;
            if (sink_ != nullptr) {
                ++*sink_;
            }
        }

        [[nodiscard]] bool open_here() const noexcept { return open_; }
        [[nodiscard]] bool reclaimed() const noexcept { return reclaimed_; }
        [[nodiscard]] std::size_t reclaim_count() const noexcept { return reclaim_count_; }
        [[nodiscard]] ExitPath path() const noexcept { return path_; }
        [[nodiscard]] const WindowViolations &violations() const noexcept { return violations_; }
        [[nodiscard]] bool clean_run() const noexcept { return violations_.clean(); }

      private:
        WindowViolations violations_{};
        std::size_t *sink_ = nullptr;
        ExitPath path_ = ExitPath::Success;
        bool open_ = false;
        bool reclaimed_ = false;
        std::size_t reclaim_count_ = 0;
    };

    static_assert(kWindowProhibitionCount == 5, "the window prohibition set is frozen at five");
    static_assert(sizeof(WindowGuard) <= 64, "the window guard stays small and allocation-free");
    static_assert(ghostlock::contract::kExitPathCount == 5, "the window has five exit paths");
}  // namespace ghostlock::backend::cve_2026_43499::race
