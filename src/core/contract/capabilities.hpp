#ifndef GHOSTLOCK_CONTRACT_CAPABILITIES_HPP
#define GHOSTLOCK_CONTRACT_CAPABILITIES_HPP

/* Umbrella for the contract capability vocabulary (alpha batch).
 *
 * Authority: docs/analysis/contract-design.md sections 5 and 11.
 *
 * The concrete Ops handles and the virtual interfaces now live in their own
 * headers; this header preserves the historical include path and adds the
 * non-owning aggregate the composition root fills in. Existing consumers that
 * include contract/capabilities.hpp keep compiling and behaving unchanged:
 * KernelMemoryOps / FileCacheWriteOps are re-exported by the includes below
 * with their definitions moved verbatim.
 *
 * Lifetime: Capabilities does not own the implementations. The composition root
 * must keep them alive for at least as long as the chain; destruction order is
 * chain first, then the capability implementations.
 */

#include "contract/address_discovery.hpp"
#include "contract/capability.hpp"
#include "contract/child_task.hpp"
#include "contract/file_cache.hpp"
#include "contract/kernel_alias.hpp"
#include "contract/kernel_memory.hpp"

namespace ghostlock::contract {

    /* Non-owning view of what a backend provides. A null member means "not
     * provided"; callers either test supports() first or call and handle
     * CapabilityError::Unsupported (both are equivalent; R7 prefers the latter). */
    struct Capabilities final {
        KernelMemory *kernel = nullptr;
        FileCacheWrite *file_cache = nullptr;
        AddressDiscovery *address = nullptr;
        KernelAlias *alias = nullptr;
        ChildTask *child = nullptr;

        [[nodiscard]] CapabilitySet declared() const noexcept;
        [[nodiscard]] bool supports(CapabilityKind kind) const noexcept;
    };

    inline CapabilitySet Capabilities::declared() const noexcept {
        CapabilitySet set{};
        if (kernel != nullptr) {
            /* KernelMemory exposes both the read and the write primitive. */
            set.add(CapabilityKind::KernelRead).add(CapabilityKind::KernelWrite);
        }
        if (file_cache != nullptr) {
            set.add(CapabilityKind::FileCacheWrite);
        }
        if (address != nullptr) {
            set.add(CapabilityKind::AddressDiscovery);
        }
        if (alias != nullptr) {
            set.add(CapabilityKind::KernelAlias);
        }
        if (child != nullptr) {
            set.add(CapabilityKind::ChildTask);
        }
        return set;
    }

    inline bool Capabilities::supports(CapabilityKind kind) const noexcept {
        return declared().has(kind);
    }

} // namespace ghostlock::contract

#endif
