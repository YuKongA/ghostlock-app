#ifndef GHOSTLOCK_HOST_ATTACK_OPS_HPP
#define GHOSTLOCK_HOST_ATTACK_OPS_HPP

/*
 * Host shadow of attack/ops.hpp (selected by the test's -I order).
 *
 * Production keeps the inline real probes; the host data-flow test needs them
 * scripted, so this header declares the same `ghostlock::attack` surface with
 * the side-effecting probes implemented by tests/host/attack_stub.cpp. The
 * backend and frontend sources are compiled unmodified and resolve to these
 * declarations.
 */

#include "common.h"
#include "support/status.hpp"

#include <cstdint>

namespace ghostlock::attack {
    int32_t in_direct_map(uintptr_t target);

    int32_t check_selinux_off(void);

    int32_t enforce_readable(void);

    int32_t process_has_seccomp(void);

    void log_execution_settings(const struct ghostlock::profile::kernel_offsets *profile);

    void resolve_profile_addresses(void);

    void install_profile(const struct ghostlock::profile::kernel_offsets &decoded);

    void apply_iomem_cache(void);

    void slab_drain(void);

    void write_root_script(void);

    uintptr_t perf_find_task(void);

    void timer_reset(void);

    void timer_mark(const char *label);
} // namespace ghostlock::attack

#endif
