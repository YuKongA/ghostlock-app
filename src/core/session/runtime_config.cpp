#include "common.h"
#include "session/runtime_config.h"
#include "session/runtime_paths.h"

using namespace ghostlock;
using namespace ghostlock::profile;
using namespace ghostlock::config;

static void runtime_config_init_cpus(RuntimeConfig *config) {
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

static int runtime_config_validate_cpus(RuntimeConfig *config) {
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
int RuntimeConfig::apply_profile(const TargetProfile *profile) {
    const struct execution_settings *e = profile->execution();
    if (!e) return -1;
    int old_main = main_cpu;
    int old_consumer = consumer_cpu;
    main_cpu = (int) e->recommended_main_cpu;
    consumer_cpu = (int) e->recommended_consumer_cpu;
    if (runtime_config_validate_cpus(this) != 0) {
        main_cpu = old_main;
        consumer_cpu = old_consumer;
    }
    return 0;
}

static void runtime_config_init_paths(RuntimeConfig *config) {
    const char *home = getenv("GHOSTLOCK_HOME");
    if (!home || !home[0]) home = getenv("TMPDIR");
    if (!home || !home[0]) home = "/data/local/tmp";

    config->home_dir = ghostlock::config::normalize_home_dir(home);
    config->root_script_path =
            ghostlock::config::root_script_file(config->home_dir);

    const char *ksu_log = getenv("GHOSTLOCK_KSU_LOG");
    config->ksu_log_path = (ksu_log && ksu_log[0])
                               ? std::string(ksu_log)
                               : config->home_dir + "/.ghostlock_ksu.log";
}

/* Capture all process environment and CPU/path choices exactly once. Input:
 * writable config; output: 0/-1 with the validated CPU pair. */
int RuntimeConfig::init() {
    main_cpu = 0;
    consumer_cpu = 1;
    home_dir.clear();
    root_script_path.clear();
    ksu_log_path.clear();
    debug_dir.clear();

    runtime_config_init_cpus(this);
    runtime_config_init_paths(this);

    return 0;
}

/* Log the immutable runtime snapshot. Input: initialized config; output: logs. */
void RuntimeConfig::log() const {
    pr_info("cpu pair: main=%d consumer=%d\n", main_cpu, consumer_cpu);
    pr_info("runtime home=%s script=%s\n", home_dir.c_str(),
            root_script_path.c_str());
}
