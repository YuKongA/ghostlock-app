#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_STAGE_TYPES_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_STAGE_TYPES_HPP

#include <cstdint>

namespace ghostlock::backend {
    /* Outcome of one W2/W3 victim round. */
    enum class VictimRound {
        Failed,
        Retry,
        Rooted,
    };

    /* State handed from the W2/W3 victim chain to the terminal handoff
     * (Batch 4 review P4). Ownership contract:
     *   - session.victim / session.parked_victim* are the authority for the
     *     child pid, its pipes and the parked child;
     *   - VictimChain is a single-threaded, transferred stage summary: written
     *     by the backend before run() returns it and read by the terminal
     *     afterwards on the same thread (ever_rooted = a child was rooted,
     *     seccomp_ok = W3 result, child_alive = last observation). The handoff
     *     still acts on the session's fds/pid; the chain only selects the path,
     *     so there is no stale-state window. If concurrency is ever introduced,
     *     the chain must become read-only accessors over the session. */
    struct VictimChain {
        int32_t child_alive = 1;
        int32_t seccomp_ok = 0;
        int32_t ever_rooted = 0;
    };

    using write_stage_verify_fn = int32_t (*)(void *context);
} // namespace ghostlock::backend

#endif
