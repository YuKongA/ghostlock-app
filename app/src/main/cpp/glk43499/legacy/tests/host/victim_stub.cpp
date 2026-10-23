#include "host_attack_script.hpp"
#include "session/victim_process.hpp"

#include <optional>

/* Host stubs for the victim protocol: no fork, no pipes. spawn returns a
 * scripted pid/task and the stage verifies return scripted verdicts. */
namespace ghostlock::session::victim {
    std::optional<VictimSpawn> spawn_victim(VictimContext &pipes) {
        host::script().record("spawn");
        const host::HostAttackScript::Spawn spawn = host::script().next_spawn();
        /* Keep the session pid in sync with the scripted child, so the W3 and
         * handoff waitpid() checks see a consistent (non -1) pid. */
        pipes.set_child(spawn.pid);
        return VictimSpawn{spawn.pid, spawn.task};
    }

    int32_t verify_selinux_stage(void *context) {
        (void) context;
        host::script().record("verify_selinux");
        return host::script().next_selinux();
    }

    int32_t verify_w2_stage(void *context) {
        (void) context;
        host::script().record("verify_w2");
        return host::script().next_w2();
    }

    int32_t verify_seccomp_probe_stage(void *context) {
        (void) context;
        host::script().record("verify_seccomp");
        return host::script().next_seccomp();
    }

    int32_t verify_leaf_dir_stage(void *context) {
        host::script().record("verify_leaf");
        const int32_t result = host::script().next_leaf();
        if (result) {
            auto *stage = static_cast<struct w3_stage_context *>(context);
            stage->leaf_to_target8 = 0;
        }
        return result;
    }
} // namespace ghostlock::session::victim
