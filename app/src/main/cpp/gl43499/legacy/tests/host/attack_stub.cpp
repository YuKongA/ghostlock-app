#include "attack/ops.hpp"

#include "host_attack_script.hpp"

/* Host stubs for the ghostlock::attack surface: no syscalls, no kernel work.
 * Probes return the scripted verdict; side-effecting primitives are no-ops. */
namespace ghostlock::attack {
    int32_t in_direct_map(uintptr_t target) {
        (void) target;
        return 1;
    }

    int32_t check_selinux_off(void) {
        return host::script().selinux_off_initial;
    }

    int32_t enforce_readable(void) {
        return host::script().enforce_readable_value;
    }

    int32_t process_has_seccomp(void) {
        return host::script().process_has_seccomp_value;
    }

    void log_execution_settings(const struct ghostlock::profile::kernel_offsets *profile) {
        (void) profile;
    }

    void resolve_profile_addresses(void) {}

    void install_profile(const struct ghostlock::profile::kernel_offsets &decoded) {
        (void) decoded;
    }

    void apply_iomem_cache(void) {}

    void slab_drain(void) {}

    void write_root_script(void) {}

    uintptr_t perf_find_task(void) { return 0; }

    void timer_reset(void) {}

    void timer_mark(const char *label) { (void) label; }
} // namespace ghostlock::attack
