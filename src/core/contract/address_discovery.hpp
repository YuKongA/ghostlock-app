#ifndef GHOSTLOCK_CONTRACT_ADDRESS_DISCOVERY_HPP
#define GHOSTLOCK_CONTRACT_ADDRESS_DISCOVERY_HPP

/* Optional address-discovery capability (ADR-0004 T3 / ADR-0001 section 17).
 *
 * Discovery runs before any attack write (R13) and produces kernel addresses a
 * backend needs: the KASLR slide base, init_task, the target task and its
 * mm_struct. It is a capability handle in the same family as KernelMemoryOps
 * (contract/capabilities.hpp): availability is derived from the handle itself,
 * never from a separate bool flag.
 *
 * Fail-closed contract (T3)
 * -------------------------
 * discover() returns non-zero if and only if it filled a valid result. On any
 * failure -- unsupported field, sentinel result, partial measurement -- it must
 * write the canonical discovery_failed() value (every field zero) and return 0.
 * A failed result never keeps a partial address and no field is ever guessed:
 * a zero field under ok == true means "this provider did not supply it", not
 * "address 0". This makes the two known providers agree:
 *   - kernelsnitch: context_result() reports ~0 on failure -> discovery_mm_struct()
 *   - perf_find_task: returns 0 on failure                 -> discovery_target_task()
 *
 * This header is host-safe and depends on the standard library only, so the
 * contract test can exercise it on the host. It must not include backend/,
 * pipeline/, platform/ or terminal/ (ADR-0004 R1). */

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "contract/capability.hpp"

namespace ghostlock::contract {
    /* All-or-nothing discovery result. A zero field with ok == true is "not
     * supplied"; ok == false is always the all-zero canonical failure. */
    struct AddressDiscoveryResult final {
        bool ok = false;
        std::uintptr_t kaslr_base = 0;
        std::uintptr_t init_task = 0;
        std::uintptr_t target_task = 0;
        std::uintptr_t mm_struct = 0;
    };

    /* Canonical failure value: every field zero, ok == false. Providers write
     * exactly this on failure so a consumer can never observe a partial result. */
    [[nodiscard]] constexpr AddressDiscoveryResult discovery_failed() noexcept {
        return AddressDiscoveryResult{};
    }

    /* Fail-closed invariant: a failed result carries no partial address. The
     * predicate is constexpr so both the test and a future provider can assert
     * it; it deliberately allows a successful result to leave fields it does
     * not own at zero. */
    [[nodiscard]] constexpr bool fail_closed(
        const AddressDiscoveryResult &result) noexcept {
        if (result.ok) return true;
        return result.kaslr_base == 0 && result.init_task == 0 &&
               result.target_task == 0 && result.mm_struct == 0;
    }

    /* Provider adapters for the two discovery outputs. A zero value (the native
     * failure/sentinel) maps to the canonical failure; a non-zero value maps to
     * a successful result that supplies only that field. Neither guesses. */
    [[nodiscard]] constexpr AddressDiscoveryResult discovery_mm_struct(
        std::uintptr_t mm_struct) noexcept {
        AddressDiscoveryResult result{};
        if (mm_struct == 0) return discovery_failed();
        result.ok = true;
        result.mm_struct = mm_struct;
        return result;
    }

    [[nodiscard]] constexpr AddressDiscoveryResult discovery_target_task(
        std::uintptr_t target_task) noexcept {
        AddressDiscoveryResult result{};
        if (target_task == 0) return discovery_failed();
        result.ok = true;
        result.target_task = target_task;
        return result;
    }

    /* Optional capability handle (ADR-0004 T1/T3). discover() runs one discovery
     * attempt for ctx; it returns non-zero on success and, on failure, writes
     * discovery_failed() into out before returning 0. A null handle reports
     * unavailable, so a caller can skip discovery without a second fact source. */
    struct AddressDiscoveryOps final {
        void *ctx = nullptr;
        std::int32_t (*discover)(void *ctx,
                                 AddressDiscoveryResult *out) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return discover != nullptr;
        }
    };

    /* The concept below lets a provider template validate an equivalent handle
     * without naming the concrete type (ADR-0001 section 17 / ADR-0004 T3). It
     * is named AddressDiscoveryProvider, not AddressDiscovery, because the
     * contract-design.md section 4.2 interface takes the latter name and a
     * concept and a class cannot share a name in one namespace. */
    template <class Ops>
    concept AddressDiscoveryProvider = requires(Ops &ops, AddressDiscoveryResult *out) {
        { ops.available() } -> std::same_as<bool>;
        { ops.discover(ops.ctx, out) } -> std::same_as<std::int32_t>;
    };

    static_assert(AddressDiscoveryProvider<AddressDiscoveryOps>);

    /* Virtual-first address discovery (contract-design.md section 4.2). A
     * polymorphic provider is held by the capabilities aggregate; the *Ops
     * handle above stays for the C ABI / plugin boundary. On failure discover()
     * must return an error and must not write a partial result. */
    class AddressDiscovery : public CapabilityInterface {
    public:
        [[nodiscard]] virtual CapabilityResult<AddressDiscoveryResult>
        discover() noexcept = 0;

        [[nodiscard]] CapabilityKind kind() const noexcept final {
            return CapabilityKind::AddressDiscovery;
        }

    protected:
        ~AddressDiscovery() override = default; /* non-owning use: never deleted via base */
    };
} // namespace ghostlock::contract

#endif
