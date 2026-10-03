#include "backend/cve_2026_43499/bootstrap.hpp"
#include "host_attack_script.hpp"

/* Host stubs for the attack-path externals: no syscalls, no kernel work. */
namespace ghostlock::backend {
    void slab_drain(void) {}

    uintptr_t perf_find_task(void) { return 0; }
} // namespace ghostlock::backend

namespace ghostlock::terminal {
    void write_root_script(void) {}
} // namespace ghostlock::terminal

namespace ghostlock::backend {
    void install_profile(const ghostlock::profile::kernel_offsets &decoded) {
        (void) decoded;
    }
} // namespace ghostlock::backend

/* Scripted platform runtime probes (mirrors the attack stubs above). */
namespace ghostlock::platform::runtime {
    void apply_iomem_cache(const char *, const char *) {}

    int32_t check_selinux_off(void) {
        return host::script().selinux_off_initial;
    }

    int32_t enforce_readable(void) {
        return host::script().enforce_readable_value;
    }

    int32_t process_has_seccomp(void) {
        return host::script().process_has_seccomp_value;
    }
} // namespace ghostlock::platform::runtime
