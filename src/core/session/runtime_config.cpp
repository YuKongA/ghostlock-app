#include "common.h"
#include "session/runtime_config.h"
#include "session/runtime_paths.h"

using namespace ghostlock;

static void runtime_config_init_cpus(runtime_config *config) {
    config->main_cpu = 0;
    config->consumer_cpu = 1;

    if (config->main_cpu == config->consumer_cpu) {
        config->main_cpu = 0;
        config->consumer_cpu = 1;
    }

    cpu_set_t allowed;
    /* both CPU ids are range-checked against CPU_SETSIZE by init_cpus */
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
        (!CPU_ISSET(config->main_cpu, &allowed) || // NOLINT(clang-analyzer-security.ArrayBound)
         !CPU_ISSET(config->consumer_cpu, &allowed))) { // NOLINT(clang-analyzer-security.ArrayBound)
        pr_warning("cores %d/%d not in allowed cpuset; falling back to 0/1\n",
                   config->main_cpu, config->consumer_cpu);
        config->main_cpu = 0;
        config->consumer_cpu = 1;
    }
}

static int runtime_config_validate_cpus(runtime_config *config) {
    if (config->main_cpu == config->consumer_cpu) {
        pr_warning("main and consumer cores are the same (%d)\n", config->main_cpu);
        return -1;
    }
    cpu_set_t allowed;
    /* both CPU ids are range-checked against CPU_SETSIZE by init_cpus */
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
        (!CPU_ISSET(config->main_cpu, &allowed) || // NOLINT(clang-analyzer-security.ArrayBound)
         !CPU_ISSET(config->consumer_cpu, &allowed))) { // NOLINT(clang-analyzer-security.ArrayBound)
        pr_warning("profile cores %d/%d not in allowed cpuset\n",
                   config->main_cpu, config->consumer_cpu);
        return -1;
    }
    return 0;
}

/* Apply profile CPU recommendations only where Kotlin/environment did not
 * make an explicit selection. Existing explicit choices remain authoritative. */
int runtime_config_apply_profile(
    runtime_config *config, const TargetProfile *profile) {
    const struct execution_settings *e = target_profile_execution(profile);
    if (!config || !e) return -1;
    int old_main = config->main_cpu;
    int old_consumer = config->consumer_cpu;
    config->main_cpu = (int) e->recommended_main_cpu;
    config->consumer_cpu = (int) e->recommended_consumer_cpu;
    if (runtime_config_validate_cpus(config) != 0) {
        config->main_cpu = old_main;
        config->consumer_cpu = old_consumer;
    }
    return 0;
}

static void runtime_config_init_paths(runtime_config *config) {
    const char *home = getenv("GHOSTLOCK_HOME");
    if (!home || !home[0]) home = getenv("TMPDIR");
    if (!home || !home[0]) home = "/data/local/tmp";

    config->home_dir = runtime_paths::normalize_home_dir(home);
    config->root_script_path =
            runtime_paths::root_script_file(config->home_dir);

    const char *ksu_log = getenv("GHOSTLOCK_KSU_LOG");
    config->ksu_log_path = (ksu_log && ksu_log[0])
                               ? std::string(ksu_log)
                               : config->home_dir + "/.ghostlock_ksu.log";
}

/* Capture all process environment and CPU/path choices exactly once. Input:
 * writable config; output: 0/-1 with the validated CPU pair. */
int runtime_config_init(runtime_config *config) {
    if (!config) {
        errno = EINVAL;
        return -1;
    }

    config->main_cpu = 0;
    config->consumer_cpu = 1;
    config->home_dir.clear();
    config->root_script_path.clear();
    config->ksu_log_path.clear();
    config->debug_dir.clear();

    runtime_config_init_cpus(config);
    runtime_config_init_paths(config);

    return 0;
}

/* Log the immutable runtime snapshot. Input: initialized config; output: logs. */
void runtime_config_log(const runtime_config *config) {
    if (!config) return;
    pr_info("cpu pair: main=%d consumer=%d\n", config->main_cpu,
            config->consumer_cpu);
    pr_info("runtime home=%s script=%s\n", config->home_dir.c_str(),
            config->root_script_path.c_str());
}
