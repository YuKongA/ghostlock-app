#ifndef GHOSTLOCK_RUNTIME_CONFIG_H
#define GHOSTLOCK_RUNTIME_CONFIG_H

#include <stdbool.h>

struct target_profile;

struct runtime_config {
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
};

/* Compatibility authority for S02. Later stages pass a const config pointer
 * through ExploitSession and remove direct access to this process snapshot. */
#ifdef __cplusplus
extern struct runtime_config &g_runtime_config;
#else
extern struct runtime_config g_runtime_config;
#endif

int runtime_config_init(struct runtime_config *config);
int runtime_config_apply_profile(
    struct runtime_config *config, const struct target_profile *profile);
void runtime_config_log(const struct runtime_config *config);

/* Legacy entry retained while callers outside main migrate. */
void init_cpu_config(void);

#endif
