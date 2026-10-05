#ifndef GHOSTLOCK_PLATFORM_VIVO_SCHEMA_HPP
#define GHOSTLOCK_PLATFORM_VIVO_SCHEMA_HPP

#include <cstdint>

#include "contract/model.hpp"

namespace ghostlock::platform::vivo {
    /* Caller-filled view of the vendor facts the vivo policies read. Plain
     * values only - no backend, session or middleware type - so a host test can
     * build one directly and the policies stay independent of where the facts
     * came from. `tracepoint_funcs == 0` means the image did not yield
     * offsetof(struct tracepoint, funcs) and the guard fails closed. */
    struct View final {
        bool guard_enabled = false;
        uint64_t sys_exit_tp = 0;
        uint32_t tracepoint_funcs = 0;
    };

    /* profile -> view adapter. Kept next to the View so the one place that
     * knows both the frozen profile accessors and the vendor vocabulary owns
     * the mapping; the backend just hands the result to the injected policies. */
    [[nodiscard]] inline View make_view(
        const profile::TargetProfile &profile) noexcept {
        const profile::VrGuardLayout layout = profile.vr_guard_layout();
        return View{
                .guard_enabled = profile.vr_guard_enabled(),
                .sys_exit_tp = profile.vr_sys_exit_tp(),
                .tracepoint_funcs = layout.tracepoint_funcs.value_or(0),
        };
    }
} // namespace ghostlock::platform::vivo

#endif
