#ifndef GHOSTLOCK_STAGE_TYPES_HPP
#define GHOSTLOCK_STAGE_TYPES_HPP

#include <cstdint>

namespace ghostlock::session {
    /* Outcome of one orchestration stage. Failed maps to exit code 1, Continue
     * proceeds to the next stage and Done stops with exit code 0 (diagnostics). */
    enum class StageResult {
        Failed,
        Continue,
        Done,
    };

    /* Outcome of one W2/W3 victim round. */
    enum class VictimRound {
        Failed,
        Retry,
        Rooted,
    };

    /* State handed from the W2/W3 victim chain to the frontend handoff. The
     * child pid lives in VictimContext (session.victim) and is retired or
     * transferred there; only the stage flags remain here. */
    struct VictimChain {
        int32_t child_alive = 1;
        int32_t seccomp_ok = 0;
        int32_t ever_rooted = 0;
    };

    using write_stage_verify_fn = int32_t (*)(void *context);
} // namespace ghostlock::session

#endif
