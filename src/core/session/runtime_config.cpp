#include "common.h"
#include "session/runtime_config.h"
#include "session/runtime_paths.h"

static bool environment_flag(const char *name, bool default_value) {
    const char *value = getenv(name);
    if (!value || !value[0]) return default_value;
    return strcmp(value, "0") != 0;
}

static bool environment_present(const char *name) {
    return getenv(name) != NULL;
}

static void runtime_config_init_cpus(runtime_config *config) {
    config->main_cpu = 0;
    config->consumer_cpu = 1;

    const char *value = getenv("GHOSTLOCK_CORE");
    if (value && value[0]) {
        config->main_cpu_explicit = true;
        long parsed = strtol(value, NULL, 10);
        if (parsed >= 0 && parsed < CPU_SETSIZE) {
            config->main_cpu = (int) parsed;
        } else {
            pr_warning("invalid GHOSTLOCK_CORE=%s, using %d\n", value,
                    config->main_cpu);
        }
    }

    value = getenv("GHOSTLOCK_CONSUMER_CORE");
    if (value && value[0]) {
        config->consumer_cpu_explicit = true;
        long parsed = strtol(value, NULL, 10);
        if (parsed >= 0 && parsed < CPU_SETSIZE) {
            config->consumer_cpu = (int) parsed;
        } else {
            pr_warning("invalid GHOSTLOCK_CONSUMER_CORE=%s, using %d\n", value,
                    config->consumer_cpu);
        }
    } else {
        config->consumer_cpu = config->main_cpu + 1;
    }

    if (config->main_cpu == config->consumer_cpu) {
        pr_warning("main and consumer cores are the same (%d); falling back\n",
                config->main_cpu);
        config->main_cpu = 0;
        config->consumer_cpu = 1;
    }

    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
            (!CPU_ISSET(config->main_cpu, &allowed) ||
                    !CPU_ISSET(config->consumer_cpu, &allowed))) {
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
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
            (!CPU_ISSET(config->main_cpu, &allowed) ||
                    !CPU_ISSET(config->consumer_cpu, &allowed))) {
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
    if (!config->main_cpu_explicit)
        config->main_cpu = (int) e->recommended_main_cpu;
    if (!config->consumer_cpu_explicit && !config->main_cpu_explicit)
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

    config->home_dir = ghostlock::runtime_paths::normalize_home_dir(home);
    config->root_script_path =
            ghostlock::runtime_paths::root_script_file(config->home_dir);
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
    config->tcp_zerocopy_enabled = false;
    config->multicast_resident_enabled = false;
    config->multicast_phase1_probe = false;
    config->w1_only = false;
    config->verbose_debug = false;
    config->main_cpu_explicit = false;
    config->consumer_cpu_explicit = false;
    config->home_dir.clear();
    config->root_script_path.clear();

    runtime_config_init_cpus(config);
    runtime_config_init_paths(config);
    config->tcp_zerocopy_enabled =
            environment_flag("GHOSTLOCK_TCP_ROUTE", true);
    config->multicast_resident_enabled =
            environment_present("GHOSTLOCK_5X_RESIDENT");
    config->multicast_phase1_probe =
            environment_present("GHOSTLOCK_5X_PHASE1_PROBE");
    config->w1_only = environment_present("GHOSTLOCK_W1_ONLY");
    config->verbose_debug = environment_present("GHOSTLOCK_VERBOSE_DEBUG");

    return 0;
}

/* Log the immutable runtime snapshot. Input: initialized config; output: logs. */
void runtime_config_log(const runtime_config *config) {
    if (!config) return;
    pr_info("cpu pair: main=%d consumer=%d\n", config->main_cpu,
            config->consumer_cpu);
    pr_info("runtime home=%s script=%s\n", config->home_dir.c_str(),
            config->root_script_path.c_str());
    pr_info("runtime verbose_debug=%d\n", config->verbose_debug);
}

/* Legacy compatibility adapter. Input: process environment; output: refreshed
 * global RuntimeConfig. New orchestration calls runtime_config_init directly. */
void init_cpu_config(void) {
    if (runtime_config_init(&g_runtime_config) == 0) {
        runtime_config_log(&g_runtime_config);
    }
}
