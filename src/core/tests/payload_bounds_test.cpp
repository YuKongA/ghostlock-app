/* Host test for the task_struct.usage payload bound shared by the load-time
 * profile validation (profile/entry.cpp) and mirrored by the Kotlin editor.
 * Requires TARGET_CONFIG_H because the geometry lives in kernel/constants.hpp. */

#include "kernel/payload_bounds.h"

#include <cassert>
#include <cstdint>

using namespace ghostlock;

/* Exact per-route headroom, derived from the payload chunk geometry:
 *   ORDER3_SIZE - chunk_bias - fake_task_off - sizeof(uint32_t). */
static_assert(kernel::task_usage_offset_limit(profile::kRouteSelectStack) == 0x6d7c);
static_assert(kernel::task_usage_offset_limit(profile::kRouteMulticastWaiter) == 0x6d7c);
static_assert(kernel::task_usage_offset_limit(profile::kRouteTcpZerocopy) == 0x197c);

/* The bound is tight: the usage write at the limit exactly fills the last
 * chunk, and one byte more overruns the skb buffer. */
static_assert(kernel::SKB_SEND_SIZE - kernel::ORDER3_SIZE + kernel::SKB_FRAG_BIAS +
                      kernel::FAKE_TASK_OFF +
                      kernel::task_usage_offset_limit(profile::kRouteMulticastWaiter) +
                      sizeof(uint32_t) ==
              kernel::SKB_SEND_SIZE);
static_assert(kernel::SKB_SEND_SIZE - kernel::ORDER3_SIZE + kernel::TCP_SKB_FRAG_BIAS +
                      kernel::TCP_FAKE_TASK_OFF +
                      kernel::task_usage_offset_limit(profile::kRouteTcpZerocopy) +
                      sizeof(uint32_t) ==
              kernel::SKB_SEND_SIZE);

int32_t main() {
    for (const uint8_t route : {profile::kRouteSelectStack, profile::kRouteMulticastWaiter,
                              profile::kRouteTcpZerocopy}) {
        /* 0/absent keeps the legacy fallback and is always accepted. */
        assert(kernel::task_usage_offset_valid(route, 0));
        /* Device-verified vivo value and the legacy fallback offset fit. */
        assert(kernel::task_usage_offset_valid(route, 56));
        assert(kernel::task_usage_offset_valid(route, kernel::FAKE_TASK_USAGE_OFF));
        const uint32_t limit = kernel::task_usage_offset_limit(route);
        assert(kernel::task_usage_offset_valid(route, limit));
        assert(!kernel::task_usage_offset_valid(route, limit + 1));
        /* The reported exploit vector (usage = 32768) is rejected everywhere. */
        assert(!kernel::task_usage_offset_valid(route, 32768));
        assert(!kernel::task_usage_offset_valid(route, 0xffffffffu));
    }
    return 0;
}