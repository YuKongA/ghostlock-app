#ifndef GHOSTLOCK_RUNTIME_CONFIG_H
#define GHOSTLOCK_RUNTIME_CONFIG_H

#include <stdbool.h>
#include "profile.h"

#ifdef __cplusplus

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

  RuntimeConfig() noexcept = default;
};

using runtime_config = RuntimeConfig;

#else /* C façade for callers that have not migrated */

typedef struct runtime_config {
    int main_cpu;
    int consumer_cpu;
    bool tcp_zerocopy_enabled;
    bool multicast_resident_enabled;
    bool multicast_phase1_probe;
    bool w1_only;
    bool verbose_debug;
    bool main_cpu_explicit;
    bool consumer_cpu_explicit;
    char home_dir[256];
    char root_script_path[300];
} runtime_config;

#endif

/* Compatibility authority for S02. Later stages pass a const config pointer
 * through ExploitSession and remove direct access to this process snapshot. */
#ifdef __cplusplus
extern RuntimeConfig &g_runtime_config;
#else
extern runtime_config g_runtime_config;
#endif

int runtime_config_init(runtime_config *config);

int runtime_config_apply_profile(
        runtime_config *config, const TargetProfile *profile);

void runtime_config_log(const runtime_config *config);

/* Legacy entry retained while callers outside main migrate. */
void init_cpu_config(void);

#endif
