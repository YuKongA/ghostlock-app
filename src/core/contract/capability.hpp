#ifndef GHOSTLOCK_CONTRACT_CAPABILITY_HPP
#define GHOSTLOCK_CONTRACT_CAPABILITY_HPP

/* Neutral capability vocabulary for the contract layer (alpha batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3, 3.6, 9 and 11.
 *
 * This header is declaration-only: it introduces the capability-kind bit set,
 * the typed error/state vocabulary and the optional polymorphic marker base.
 * No production translation unit consumes it yet (alpha is pure addition, zero
 * behavior change); contract/ stays host-compilable under the R1 include
 * firewall, so no backend/, pipeline/, platform/ or terminal/ header may be
 * pulled in here.
 *
 * CapabilityKind bits 0..6 mirror the C plugin ABI (contract/abi/glk_contract_abi.h)
 * bit-for-bit; the static_asserts below turn any drift into a build failure.
 * Bits 7 and 8 are this tree's extension (leak discovery and direct-map access).
 */

#include <cstdint>
#include <expected>
#include <utility>

#include "contract/abi/glk_contract_abi.h"

namespace ghostlock::contract {

    /* Union of every backend capability (R7). The low seven bits are frozen
     * against glk_capability; never renumber them, only append. */
    enum class CapabilityKind : std::uint32_t {
        KernelRead = 1u << 0,
        KernelWrite = 1u << 1,
        KernelAlias = 1u << 2,
        ChildTask = 1u << 3,
        FileCacheWrite = 1u << 4,
        Exec = 1u << 5,
        KernelHook = 1u << 6,
        /* Tree extensions (access-domain split, section 3.5). */
        AddressDiscovery = 1u << 7, /* leak: 43499 kernelsnitch */
        KernelDirectMap = 1u << 8,  /* direct-map alias access / image translation */
    };

    /* The C ABI owns these bit values; a mismatch here would silently break a
     * plugin's required_caps comparison, so assert every frozen bit. */
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
    static_assert(std::to_underlying(CapabilityKind::AddressDiscovery) == (1u << 7));
    static_assert(std::to_underlying(CapabilityKind::KernelDirectMap) == (1u << 8));

    /* Unsupported is a first-class error (R7): never a silent zero and never a
     * silent mechanism swap. Closed is distinct from Unavailable so a "used
     * after close" defect is detectable instead of masked. */
    enum class CapabilityError : std::uint8_t {
        None = 0,
        Unsupported, /* backend never declares this capability */
        Unavailable, /* declared, but not ready yet (channel unbuilt / device) */
        Faulted,     /* provided, but the execution failed */
        Rejected,    /* bad argument / precondition, fail-closed */
        Closed,      /* explicitly closed, terminal; reuse is a defect */
    };

    template <class T>
    using CapabilityResult = std::expected<T, CapabilityError>;
    using CapabilityStatus = CapabilityResult<void>;

    /* Lifecycle of one capability/mechanism (section 3.6). Not a simple numeric
     * order: NotAvailable means "not opened yet" while Closed means "opened and
     * already torn down"; both are unusable but must stay distinguishable. */
    enum class CapabilityState : std::uint8_t {
        NotSupported = 0, /* static fact: this backend never provides it */
        NotAvailable = 1, /* provided, not ready / device does not support it */
        Available = 2,    /* usable right now */
        Closed = 3,       /* explicitly closed, terminal */
    };

    /* Explicit truth table, deliberately not a numeric comparison:
     *   NotSupported -> nothing satisfies it (not even NotSupported)
     *   NotAvailable -> only a NotAvailable requirement
     *   Available    -> only an Available requirement
     *   Closed       -> nothing satisfies it
     */
    [[nodiscard]] constexpr bool satisfies(CapabilityState have,
                                           CapabilityState need) noexcept {
        if (have == CapabilityState::Available) {
            return need == CapabilityState::Available;
        }
        if (have == CapabilityState::NotAvailable) {
            return need == CapabilityState::NotAvailable;
        }
        return false;
    }

    /* Compile-time set: a backend declares what it provides, a step declares
     * what it needs. */
    class CapabilitySet final {
    public:
        constexpr CapabilitySet &add(CapabilityKind kind) noexcept {
            bits_ |= std::to_underlying(kind);
            return *this;
        }

        [[nodiscard]] constexpr bool has(CapabilityKind kind) const noexcept {
            const std::uint32_t bit = std::to_underlying(kind);
            return (bits_ & bit) == bit;
        }

        [[nodiscard]] constexpr bool satisfies(CapabilitySet required) const noexcept {
            return (bits_ & required.bits_) == required.bits_;
        }

        [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return bits_; }

    private:
        std::uint32_t bits_ = 0;
    };

    /* Optional marker base for polymorphic holding (registries, unique_ptr
     * containers). Concrete consumers keep using the specific interface, not
     * this base. -fno-rtti is fine: no dynamic_cast/typeid is used anywhere. */
    class CapabilityInterface {
    public:
        virtual ~CapabilityInterface() = default;

        [[nodiscard]] virtual CapabilityKind kind() const noexcept = 0;
    };

} // namespace ghostlock::contract

#endif
