/* Contract vocabulary test (alpha batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3, 3.6 and 11.
 *
 * Header-only, host-safe. Locks four things:
 *   1. CapabilityKind bits 0..6 mirror the C plugin ABI bit-for-bit;
 *   2. the explicit CapabilityState satisfies() truth table (including Closed
 *      and NotSupported satisfying nothing);
 *   3. the four-state / six-error vocabulary values;
 *   4. CapabilitySet semantics and the frozen Ops layout.
 */

#include "contract/capability.hpp"
#include "contract/file_cache.hpp"
#include "contract/kernel_memory.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <span>
#include <type_traits>
#include <utility>

using ghostlock::contract::CapabilityError;
using ghostlock::contract::CapabilityKind;
using ghostlock::contract::CapabilitySet;
using ghostlock::contract::CapabilityState;
using ghostlock::contract::FileCacheWriteOps;
using ghostlock::contract::KernelMemoryOps;

/* 1. Bit-for-bit alignment with contract/abi/glk_contract_abi.h (the C ABI). */
static_assert(std::to_underlying(CapabilityKind::KernelRead) ==
              static_cast<std::uint32_t>(GLK_CAP_KERNEL_READ));
static_assert(std::to_underlying(CapabilityKind::KernelWrite) ==
              static_cast<std::uint32_t>(GLK_CAP_KERNEL_WRITE));
static_assert(std::to_underlying(CapabilityKind::KernelAlias) ==
              static_cast<std::uint32_t>(GLK_CAP_ALIAS));
static_assert(std::to_underlying(CapabilityKind::ChildTask) ==
              static_cast<std::uint32_t>(GLK_CAP_CHILD_TASK));
static_assert(std::to_underlying(CapabilityKind::FileCacheWrite) ==
              static_cast<std::uint32_t>(GLK_CAP_FILE_CACHE_WRITE));
static_assert(std::to_underlying(CapabilityKind::Exec) ==
              static_cast<std::uint32_t>(GLK_CAP_EXEC));
static_assert(std::to_underlying(CapabilityKind::KernelHook) ==
              static_cast<std::uint32_t>(GLK_CAP_KERNEL_HOOK));

/* 2. Explicit satisfies() table; Closed and NotSupported satisfy nothing. */
constexpr bool satisfies_table() {
    using S = CapabilityState;
    const auto satisfies = ghostlock::contract::satisfies;

    if (!satisfies(S::Available, S::Available)) return false;
    if (satisfies(S::Available, S::NotAvailable)) return false;
    if (satisfies(S::Available, S::NotSupported)) return false;
    if (satisfies(S::Available, S::Closed)) return false;

    if (!satisfies(S::NotAvailable, S::NotAvailable)) return false;
    if (satisfies(S::NotAvailable, S::Available)) return false;
    if (satisfies(S::NotAvailable, S::NotSupported)) return false;
    if (satisfies(S::NotAvailable, S::Closed)) return false;

    if (satisfies(S::NotSupported, S::Available)) return false;
    if (satisfies(S::NotSupported, S::NotAvailable)) return false;
    if (satisfies(S::NotSupported, S::NotSupported)) return false;
    if (satisfies(S::NotSupported, S::Closed)) return false;

    if (satisfies(S::Closed, S::Available)) return false;
    if (satisfies(S::Closed, S::NotAvailable)) return false;
    if (satisfies(S::Closed, S::NotSupported)) return false;
    if (satisfies(S::Closed, S::Closed)) return false;
    return true;
}
static_assert(satisfies_table());

/* 4. CapabilitySet add/has/satisfies/bits. */
constexpr bool set_semantics() {
    CapabilitySet empty{};
    if (empty.bits() != std::uint32_t{0}) return false;
    if (empty.has(CapabilityKind::KernelRead)) return false;

    CapabilitySet set{};
    set.add(CapabilityKind::KernelRead).add(CapabilityKind::AddressDiscovery);
    if (!set.has(CapabilityKind::KernelRead)) return false;
    if (!set.has(CapabilityKind::AddressDiscovery)) return false;
    if (set.has(CapabilityKind::KernelWrite)) return false;
    if (set.bits() != static_cast<std::uint32_t>((1u << 0) | (1u << 7))) return false;

    CapabilitySet required{};
    required.add(CapabilityKind::KernelRead);
    if (!set.satisfies(required)) return false;
    required.add(CapabilityKind::Exec);
    if (set.satisfies(required)) return false;

    /* Any set satisfies the empty requirement. */
    if (!set.satisfies(CapabilitySet{})) return false;
    return true;
}
static_assert(set_semantics());

/* Ops layout must stay trivially copyable and standard-layout (C boundary). */
static_assert(std::is_trivially_copyable_v<KernelMemoryOps>);
static_assert(std::is_standard_layout_v<KernelMemoryOps>);
static_assert(std::is_trivially_copyable_v<FileCacheWriteOps>);
static_assert(std::is_standard_layout_v<FileCacheWriteOps>);
static_assert(noexcept(KernelMemoryOps{}.available()));
static_assert(noexcept(FileCacheWriteOps{}.available()));

/* A provider that only exposes the pure primitives: the beta default lifecycle
 * must give it the fail-closed state/establish and the no-op close without any
 * extra boilerplate. */
namespace {
    class MinimalKernelMemory final : public ghostlock::contract::KernelMemory {
    public:
        [[nodiscard]] ghostlock::contract::CapabilityResult<std::uint64_t>
        read(std::uint64_t, std::span<std::byte>,
             ghostlock::contract::MemoryChannel) noexcept override {
            return std::unexpected(ghostlock::contract::CapabilityError::Unsupported);
        }

        ghostlock::contract::CapabilityStatus
        write(std::uint64_t, std::span<const std::byte>,
              ghostlock::contract::MemoryChannel) noexcept override {
            return std::unexpected(ghostlock::contract::CapabilityError::Unsupported);
        }

        ghostlock::contract::CapabilityStatus
        write_zero(std::uint64_t) noexcept override {
            return {};
        }

        [[nodiscard]] bool supports(
                ghostlock::contract::MemoryChannel) const noexcept override {
            return false;
        }
        [[nodiscard]] bool supports(
                ghostlock::contract::CarrierKind) const noexcept override {
            return false;
        }
    };
} // namespace

int32_t main(void) {
    /* 3. Four-state / six-error vocabulary values. */
    assert(std::to_underlying(CapabilityState::NotSupported) == std::uint8_t{0});
    assert(std::to_underlying(CapabilityState::NotAvailable) == std::uint8_t{1});
    assert(std::to_underlying(CapabilityState::Available) == std::uint8_t{2});
    assert(std::to_underlying(CapabilityState::Closed) == std::uint8_t{3});

    assert(std::to_underlying(CapabilityError::None) == std::uint8_t{0});
    assert(std::to_underlying(CapabilityError::Unsupported) == std::uint8_t{1});
    assert(std::to_underlying(CapabilityError::Unavailable) == std::uint8_t{2});
    assert(std::to_underlying(CapabilityError::Faulted) == std::uint8_t{3});
    assert(std::to_underlying(CapabilityError::Rejected) == std::uint8_t{4});
    assert(std::to_underlying(CapabilityError::Closed) == std::uint8_t{5});

    /* Exercise the compile-time tables through the runtime path too. */
    assert(satisfies_table());
    assert(!ghostlock::contract::satisfies(CapabilityState::Closed,
                                           CapabilityState::Available));
    assert(set_semantics());

    CapabilitySet declared{};
    declared.add(CapabilityKind::KernelRead).add(CapabilityKind::FileCacheWrite);
    assert(declared.has(CapabilityKind::FileCacheWrite));
    assert(!declared.has(CapabilityKind::KernelWrite));
    assert(declared.bits() == static_cast<std::uint32_t>((1u << 0) | (1u << 4)));

    /* Beta lifecycle defaults: a provider that only implements the pure
     * primitives inherits NotSupported state, Unsupported establish and a no-op
     * close. The primitives themselves remain pure virtual. */
    MinimalKernelMemory minimal;
    assert(minimal.state(ghostlock::contract::MemoryChannel::Fops) ==
           CapabilityState::NotSupported);
    assert(minimal.state(ghostlock::contract::CarrierKind::Ashmem) ==
           CapabilityState::NotSupported);
    const auto establish = minimal.establish(
            ghostlock::contract::MemoryChannel::Fops,
            ghostlock::contract::CarrierKind::Ashmem);
    assert(!establish.has_value());
    assert(establish.error() == CapabilityError::Unsupported);
    assert(minimal.close(ghostlock::contract::CarrierKind::Ashmem).has_value());

    puts("contract_vocabulary_test: ok");
    return 0;
}
