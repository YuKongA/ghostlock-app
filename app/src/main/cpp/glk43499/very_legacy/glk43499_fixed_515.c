#include "../glk_session.h"
#include "../kernels/offsets.h"

#define GLK_FIXED_RELEASE "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"

static const struct kernel_offsets fixed_515[] = {
#include "5.15.189-android13-8-00016-g51bba4309aac-ab14546557/offsets.h"
};

void glk43499_fixed_config_515(Glk43499Config *config, const char *home) {
  memset(config, 0, sizeof(*config));
  glk43499_config_defaults(config);
  config->main_cpu = 0;
  config->consumer_cpu = 1;
  config->route = GLK_ROUTE_AUTO;
  config->tcp_route = true;
  snprintf(config->home, sizeof(config->home), "%s",
           home && home[0] ? home : "/data/local/tmp");
  size_t n = strlen(config->home);
  while (n > 1 && config->home[n - 1] == '/') config->home[--n] = '\0';
  snprintf(config->root_script, sizeof(config->root_script),
           "%s/.ghostlock_root.sh", config->home);
  snprintf(config->ksu_log, sizeof(config->ksu_log),
           "%s/.ghostlock_ksu.log", config->home);
}

int glk43499_fixed_target_515(Glk43499Target *target) {
  if (!target) return -1;
  glk43499_target_init(target, &fixed_515[0], GLK_FIXED_RELEASE);
  target->layout.init_cred_image =
      target->layout.kimage_text_base + target->offsets.off_init_cred;
  target->layout.p0_kernel_phys_load = P0_KERNEL_PHYS_LOAD;
  return 0;
}
