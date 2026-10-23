#include "glk_session.h"
#include "glk43499_api.h"

#define GLK_NO_KNOWN_OFFSETS 1
#include "../kernels/offsets.h"
static const struct kernel_offsets fixed_offsets[] = {
#include "5.15.189-android13-8-00016-g51bba4309aac-ab14546557/offsets.h"
};

#include <stdio.h>
#include <string.h>

#define TEMP_RELEASE "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"
#define TEMP_HOME "/data/local/tmp"

#ifndef TEMP_W1_ONLY
#define TEMP_W1_ONLY 0
#endif
#ifndef TEMP_RESIDENT_5X
#define TEMP_RESIDENT_5X 0
#endif
#ifndef TEMP_PHASE1_PROBE
#define TEMP_PHASE1_PROBE 0
#endif

static int temp_fixed_target(Glk43499Target *target) {
  const struct kernel_offsets *src = &fixed_offsets[0];
  glk43499_target_init(target, src, TEMP_RELEASE);
  target->layout.init_cred_image =
      target->layout.kimage_text_base + target->offsets.off_init_cred;
  target->layout.p0_kernel_phys_load = P0_KERNEL_PHYS_LOAD;
  return 0;
}

static void temp_fixed_config(Glk43499Config *config) {
  memset(config, 0, sizeof(*config));
  glk43499_config_defaults(config);
  config->main_cpu = 0;
  config->consumer_cpu = 1;
  config->route = GLK_ROUTE_AUTO;
  config->tcp_route = true;
  config->w1_only = TEMP_W1_ONLY != 0;
  config->resident_5x = TEMP_RESIDENT_5X != 0;
  config->phase1_probe = TEMP_PHASE1_PROBE != 0;
  snprintf(config->home, sizeof(config->home), "%s", TEMP_HOME);
  snprintf(config->root_script, sizeof(config->root_script),
           "%s/.ghostlock_root.sh", TEMP_HOME);
  snprintf(config->ksu_log, sizeof(config->ksu_log),
           "%s/.ghostlock_ksu.log", TEMP_HOME);
}

/* Temporary JNI-shaped entry: create/run/destroy. */
static Glk43499Handle *temp_jni_create(void) {
  Glk43499Target target;
  Glk43499Config config;
  Glk43499Env env;
  memset(&target, 0, sizeof(target));
  memset(&env, 0, sizeof(env));
  if (temp_fixed_target(&target) != 0) return NULL;
  temp_fixed_config(&config);
  glk43499_write_root_script(&config);
  return glk43499_handle_create(&target, &config, &env);
}

static int temp_jni_run(Glk43499Handle *handle) {
  return glk43499_handle_run(handle);
}

static void temp_jni_destroy(Glk43499Handle *handle) {
  glk43499_handle_destroy(handle);
}

int main(void) {
  set_unbuffer();
  gl_log_set_min_level(GL_LOG_DEBUG);
  Glk43499Handle *handle = temp_jni_create();
  if (!handle) return 1;
  int rc = temp_jni_run(handle);
  temp_jni_destroy(handle);
  return rc;
}
