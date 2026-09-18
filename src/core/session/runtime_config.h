#ifndef GHOSTLOCK_RUNTIME_CONFIG_H
#define GHOSTLOCK_RUNTIME_CONFIG_H

#include <stdbool.h>
#include "profile.h"


#include <string>

/* One runtime snapshot captured once from the environment. Paths own their
 * storage; callers convert to c_str() only at syscall/exec boundaries so no
 * temporary string pointer can outlive the snapshot. */
class RuntimeConfig final {
 public:
  int main_cpu = 0;
  int consumer_cpu = 1;
  bool tcp_zerocopy_enabled = true;
  bool multicast_resident_enabled = false;
  bool multicast_phase1_probe = false;
  bool w1_only = false;
  bool verbose_debug = false;
  bool main_cpu_explicit = false;
  bool consumer_cpu_explicit = false;
  std::string home_dir = "/data/local/tmp";
  std::string root_script_path = "/data/local/tmp/.ghostlock_root.sh";
  /* Per-run KernelSU log (U01-S14): Kotlin passes GHOSTLOCK_KSU_LOG so a
   * previous run's markers can never be misread; falls back to the legacy
   * home-dir name for CLI runs. */
  std::string ksu_log_path = "/data/local/tmp/.ghostlock_ksu.log";

  RuntimeConfig() noexcept = default;
};

using runtime_config = RuntimeConfig;


/* The process snapshot is owned by ExploitSession. Callers reach it through
 * this accessor; the public reference alias is gone (CPP12/SESSION-01). */
RuntimeConfig &runtime_config_snapshot() noexcept;

int runtime_config_init(runtime_config *config);

int runtime_config_apply_profile(
        runtime_config *config, const TargetProfile *profile);

void runtime_config_log(const runtime_config *config);

#endif
