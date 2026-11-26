#ifndef GHOSTLOCK_KERNEL_PAYLOAD_BOUNDS_H
#define GHOSTLOCK_KERNEL_PAYLOAD_BOUNDS_H

#include <cstddef>
#include <cstdint>

#include "kernel/constants.hpp"
#include "kernel/target.h"
#include "profile/model.h"

namespace ghostlock::kernel {
    /* Upper bound for a profile-provided `task_struct.usage` (fake-task
     * refcount offset) so the usage write in prepare_skb_payload() stays inside
     * the skb buffer. The write lands at
     *   chunk + chunk_bias + fake_task_off + usage   (+ sizeof(uint32_t) bytes)
     * within the SKB_SEND_SIZE buffer; the worst case is the final chunk
     * (SKB_SEND_SIZE - ORDER3_SIZE). Derived from the same geometry constants
     * the payload builder uses, per route layout. */
    [[nodiscard]] constexpr uint32_t task_usage_offset_limit(uint8_t route) noexcept {
        const bool tcp = route == profile::kRouteTcpZerocopy;
        const std::size_t chunk_bias =
                tcp ? static_cast<std::size_t>(TCP_SKB_FRAG_BIAS)
                    : static_cast<std::size_t>(SKB_FRAG_BIAS);
        const std::size_t fake_task_off = tcp ? TCP_FAKE_TASK_OFF : FAKE_TASK_OFF;
        const std::size_t last_chunk = SKB_SEND_SIZE - ORDER3_SIZE;
        const std::size_t headroom =
                SKB_SEND_SIZE - last_chunk - chunk_bias - fake_task_off - sizeof(uint32_t);
        return static_cast<uint32_t>(headroom);
    }

    /* `usage == 0` means absent -> the compiled FAKE_TASK_USAGE_OFF fallback,
     * which always fits; any provided value must not exceed the route bound. */
    [[nodiscard]] constexpr bool task_usage_offset_valid(uint8_t route,
                                                         uint32_t usage) noexcept {
        return usage == 0 || usage <= task_usage_offset_limit(route);
    }
} // namespace ghostlock::kernel

#endif