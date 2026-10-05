#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_CAPABILITY_ADAPTERS_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_CAPABILITY_ADAPTERS_HPP

/* Tier 1 capability adapters for the cve_2026_43499 backend (beta batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.9, 5, 7(b), 9 and 11.
 *
 * These adapters are the bridge between the neutral contract interfaces and the
 * backend's compile-time-bound route primitives. They are intentionally built
 * at the call site and stack-local: Capabilities is a NON-OWNING view, so the
 * adapter objects must outlive the controller call and must never be stored in
 * the session or any global.
 *
 * Section 7(b): the orchestration layer (the ancillary controller and the
 * behaviors) speaks the virtual contract, while write_zero() binds
 * Cve43499Primitives::zero_word<M> at compile time for the route M. There is no
 * virtual dispatch inside the write primitive itself; the only indirect call is
 * the one the capability interface already makes, and the ancillary stages sit
 * outside the PI race window.
 *
 * R7: read()/write() are not implemented by 43499 (it has no general kernel
 * read) and return CapabilityError::Unsupported. They never return a fake 0.
 */

#include <cstdint>
#include <expected>
#include <span>

#include "backend/cve_2026_43499/primitives.hpp"
#include "contract/capabilities.hpp"

namespace ghostlock::backend::cve_2026_43499 {

    /* Tier 1 KernelMemory: the single-word zero bootstrap bound to route M.
     * The channel/carrier lifecycle has no meaning here, so the inherited
     * default state()/establish()/close() answer NotSupported / Unsupported /
     * no-op. */
    template <class M>
    class Tier1KernelMemory final : public contract::KernelMemory {
    public:
        explicit Tier1KernelMemory(const char *desc = "43499: tier-1 write_zero") noexcept
            : desc_(desc) {}

        [[nodiscard]] contract::CapabilityResult<std::uint64_t>
        read(std::uint64_t, std::span<std::byte>, contract::MemoryChannel) noexcept override {
            /* 43499 has no general kernel read primitive (design section 3.7). */
            return std::unexpected(contract::CapabilityError::Unsupported);
        }

        contract::CapabilityStatus
        write(std::uint64_t, std::span<const std::byte>,
              contract::MemoryChannel) noexcept override {
            return std::unexpected(contract::CapabilityError::Unsupported);
        }

        contract::CapabilityStatus write_zero(std::uint64_t address) noexcept override {
            /* Compile-time binding: M is the instantiated route policy, so this
             * is a direct call to zero_word<M>, not a function-pointer hop. The
             * bool Status maps to the typed contract result (R7: failure is an
             * explicit Faulted error, never a silent success). */
            if (Cve43499Primitives::template zero_word<M>(address, desc_)) {
                return {};
            }
            return std::unexpected(contract::CapabilityError::Faulted);
        }

        [[nodiscard]] bool supports(contract::MemoryChannel) const noexcept override {
            return false;
        }
        [[nodiscard]] bool supports(contract::CarrierKind) const noexcept override {
            return false;
        }

    private:
        const char *desc_;
    };

    /* ChildTask for a single step: the value is captured at the PostSpawn call
     * site and refreshed by rebuilding the adapter, never cached in the session.
     * A zero/absent task is reported as Unavailable (the old magic-value
     * convention upgraded to an explicit state, design section 3.9 conclusion 4). */
    class StepChildTask final : public contract::ChildTask {
    public:
        explicit StepChildTask(std::uintptr_t task = 0) noexcept : task_(task) {}

        [[nodiscard]] contract::CapabilityResult<std::uint64_t>
        current() const noexcept override {
            if (task_ == 0) {
                return std::unexpected(contract::CapabilityError::Unavailable);
            }
            return static_cast<std::uint64_t>(task_);
        }

    private:
        std::uintptr_t task_;
    };

} // namespace ghostlock::backend::cve_2026_43499

#endif
