#include "backend/cve_2026_43499/bootstrap.hpp"
#include "backend/cve_2026_43499/primitives.hpp"
#include "backend/cve_2026_43499/route/route_middleware.hpp"
#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "session/core_session.hpp"
#include "host_attack_script.hpp"

/* Host stubs for the attack-path externals: no syscalls, no kernel work. */
namespace ghostlock::backend {
    void slab_drain(void) {}

    uintptr_t perf_find_task(void) { return 0; }
} // namespace ghostlock::backend

namespace ghostlock::terminal {
    void write_root_script(bool) {}
} // namespace ghostlock::terminal

namespace ghostlock::backend {
    /* state_from now owns the bind; the profile install/logging is a no-op on
     * the host (no uname match, no runtime_config apply). */
    void install_profile() {}
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

/* Host stand-in for the non-template base's write primitives (T4): the Android
 * unit keeps the real heap spray + PI race, while the host script only needs the
 * route outcome. run_middleware_route is itself a host stub. */
namespace ghostlock::backend {
    template <class M>
    Status Cve43499Primitives::attack_write(session::CoreSession &session,
                                            const memory::WriteRequest &request,
                                            const char *) {
        return ghostlock::backend::cve_2026_43499::route::middleware::run_middleware_route(
            session, request);
    }

    template <class M>
    Status Cve43499Primitives::zero_word(uintptr_t, const char *) {
        return true;
    }

    template Status Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(
        session::CoreSession &, const memory::WriteRequest &, const char *);
    template Status Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(
        session::CoreSession &, const memory::WriteRequest &, const char *);
    template Status Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(
        session::CoreSession &, const memory::WriteRequest &, const char *);

    template Status Cve43499Primitives::zero_word<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(
        uintptr_t, const char *);
    template Status Cve43499Primitives::zero_word<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(
        uintptr_t, const char *);
    template Status Cve43499Primitives::zero_word<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(
        uintptr_t, const char *);
} // namespace ghostlock::backend
