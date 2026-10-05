#pragma once

/* GhostLock adapter: KernelSnitch as an optional contract::AddressDiscoveryOps
 * implementation (ADR-0001 section 17 / ADR-0004 T3).
 *
 * This header adds no algorithm. It binds the frozen upstream KernelSnitch
 * provider (kernelsnitch.h and its subcomponents) to the fail-closed contract.
 * The KernelSnitch algorithm, constants and upstream attribution stay in
 * kernelsnitch.h / futex_hash.h, unchanged.
 *
 * The caller keeps the existing two-phase ownership: context_init -> (forked
 * leak child) context_find_collisions -> context_scan -> context_result ->
 * context_destroy, all owned by KernelSnitchOwner. context_discover() is the
 * final map from a completed collision phase to the contract result; it does not
 * run collision discovery itself, so it preserves the live spray's process
 * split. On failure it writes the canonical all-zero result and never exposes
 * the ~0 leak sentinel or any partial address. */

#include "kernelsnitch.h"

#include "contract/address_discovery.hpp"

#include <cstddef>
#include <cstdint>

namespace ghostlock::kernelsnitch {
    /* Map a KernelSnitch context that already reached the collision phase into
     * the contract result. Failure (wrong state, scan failure, the ~0 leak
     * sentinel) writes discovery_failed() and returns 0; success restores the
     * canonical VA from the tag nibble exactly as the live spray does. */
    inline std::int32_t context_discover(
        void *opaque,
        ghostlock::contract::AddressDiscoveryResult *out) noexcept {
        if (out == nullptr) return 0;
        auto *ks = static_cast<KernelSnitchContext *>(opaque);
        if (ks == nullptr || ks->state != KERNELSNITCH_COLLISIONS_FOUND) {
            *out = ghostlock::contract::discovery_failed();
            return 0;
        }
        if (context_scan(ks) != 0) {
            *out = ghostlock::contract::discovery_failed();
            return 0;
        }
        const std::size_t mm_struct = context_result(ks);
        if (mm_struct == static_cast<std::size_t>(-1)) {
            *out = ghostlock::contract::discovery_failed();
            return 0;
        }
        /* the tag nibble replaces bits 56-59; 0xf restores the canonical VA */
        const std::uintptr_t canonical =
            static_cast<std::uintptr_t>(mm_struct) |
            (static_cast<std::uintptr_t>(0xf) << 56);
        *out = ghostlock::contract::discovery_mm_struct(canonical);
        return out->ok ? 1 : 0;
    }

    /* Build the optional capability handle bound to one KernelSnitch context. */
    [[nodiscard]] inline ghostlock::contract::AddressDiscoveryOps
    address_discovery_ops(KernelSnitchContext *ks) noexcept {
        ghostlock::contract::AddressDiscoveryOps ops{};
        ops.ctx = ks;
        ops.discover = &context_discover;
        return ops;
    }
} // namespace ghostlock::kernelsnitch
