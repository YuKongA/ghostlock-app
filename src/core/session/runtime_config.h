#ifndef GHOSTLOCK_RUNTIME_CONFIG_H
#define GHOSTLOCK_RUNTIME_CONFIG_H

#include "profile.h"


#include <string>

namespace ghostlock {

/* One runtime snapshot captured once from the environment. Paths own their
 * storage; callers convert to c_str() only at syscall/exec boundaries so no
 * temporary string pointer can outlive the snapshot. */
class RuntimeConfig final {
public:
    int main_cpu = 0;
    int consumer_cpu = 1;
    std::string home_dir = "/data/local/tmp";
    std::string root_script_path = "/data/local/tmp/.ghostlock_root.sh";
    /* Per-run KernelSU log (U01-S14): Kotlin passes GHOSTLOCK_KSU_LOG so a
   * previous run's markers can never be misread; falls back to the legacy
   * home-dir name for CLI runs. */
    std::string ksu_log_path = "/data/local/tmp/.ghostlock_ksu.log";
    /* Kernel-log dump directory from --dump-kernel-log; empty disables the dump. */
    std::string debug_dir;

    RuntimeConfig() noexcept = default;

    /* Capture the process environment exactly once. */
    int init();
    /* Merge the profile's recommended CPU pair (validated). */
    int apply_profile(const ghostlock::profile::TargetProfile *profile);
    void log() const;
};

using runtime_config = RuntimeConfig;


/* The process snapshot is owned by ExploitSession. Callers reach it through
 * this accessor; the public reference alias is gone (CPP12/SESSION-01). */
RuntimeConfig &runtime_config_snapshot() noexcept;

} // namespace ghostlock

#endif
