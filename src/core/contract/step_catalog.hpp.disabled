#ifndef GHOSTLOCK_CONTRACT_STEP_CATALOG_HPP
#define GHOSTLOCK_CONTRACT_STEP_CATALOG_HPP

/* Step vocabulary for the step-queue design (docs/analysis/step-queue-design.md
 * section 4.1; M1).
 *
 * M1 SCOPE (hard boundary): this header is DECLARATION ONLY. It adds the
 * per-step vocabulary, the compile-time registration rules and the alias mapping
 * onto today's step sets. It changes no executor, no Pipeline and no
 * orchestrator assertion, so user-visible behaviour is unchanged; the per-step
 * executor split and the queue/normalization land in M2.
 *
 * What is compile-checked here:
 *   1. the catalogue is well formed (unique ids and tokens, contiguous slots per
 *      backend, dependencies only on lower slots of the same backend);
 *   2. every catalogued step is covered by at least one step-set alias -- a new
 *      catalogue row that no execution surface covers does NOT COMPILE;
 *   3. the alias table covers exactly the catalogued StepSetKind values and
 *      keeps the canonical (prefix) order required by design doc U4;
 *   4. `StepExecution` is the M2 registration point: a per-step policy that
 *      declares `static constexpr StepId step_id` satisfies it, and M2 replaces
 *      the alias-coverage fold with the executor fold, so an unregistered step
 *      id then fails to compile (design doc sections 4.1/4.2).
 *
 * Layering: contract/ only -- host-compilable, no backend/pipeline/platform
 * includes (R1 include firewall). */

#include "contract/identity.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string_view>

namespace ghostlock::contract {
    /* One executable step. Unknown = 0 is the fail-closed sentinel (absent or
     * unrecognized step), mirroring StepSetKind::Unknown. */
    enum class StepId : std::uint8_t {
        Unknown = 0,
        W1 = 1,
        W2 = 2,
        W3 = 3,
        PageCacheWrite = 4,
    };

    /* Side-effect domain of a step: diagnostics, the experimental warning text
     * and future per-step availability checks -- never dispatch. */
    enum class StepEffect : std::uint8_t {
        Selinux = 1,
        Credentials = 2,
        Seccomp = 3,
        PageCache = 4,
    };

    /* Field order is padding-optimal (the two 16-byte views first, then the
     * one-byte ids and flags) so clang-analyzer's performance.Padding check
     * stays clean; the aggregate initialisers below follow this order. */
    struct StepSpec final {
        std::string_view token;   /* HOCON spelling: "w1" / "pagecache_write" */
        std::string_view display; /* human-readable, for diagnostics and UI */
        StepId id;
        BackendKind backend;
        std::uint8_t slot;      /* position in the backend's canonical order */
        std::uint8_t deps_mask; /* bit i => slot i is a prerequisite */
        bool skippable;
        bool available;         /* has a device-verified execution path */
        StepEffect effect;
    };

    inline constexpr StepSpec kStepCatalog[] = {
        {"w1", "SELinux bypass", StepId::W1, BackendKind::Cve2026_43499, 0U, 0b000U,
         false, true, StepEffect::Selinux},
        {"w2", "credential and uid 0", StepId::W2, BackendKind::Cve2026_43499, 1U,
         0b001U, false, true, StepEffect::Credentials},
        {"w3", "seccomp bypass", StepId::W3, BackendKind::Cve2026_43499, 2U, 0b011U,
         false, true, StepEffect::Seccomp},
        {"pagecache_write", "page cache write", StepId::PageCacheWrite,
         BackendKind::Cve2026_43284, 0U, 0b000U, false, true, StepEffect::PageCache},
    };
    inline constexpr std::size_t kStepCount = std::size(kStepCatalog);

    [[nodiscard]] constexpr std::size_t backend_step_count(BackendKind backend) noexcept {
        std::size_t count = 0;
        for (const StepSpec &spec : kStepCatalog) {
            if (spec.backend == backend) ++count;
        }
        return count;
    }

    [[nodiscard]] constexpr const StepSpec *step_at_slot(BackendKind backend,
                                                         std::uint8_t slot) noexcept {
        for (const StepSpec &spec : kStepCatalog) {
            if (spec.backend == backend && spec.slot == slot) return &spec;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr const StepSpec *step_spec(StepId id) noexcept {
        for (const StepSpec &spec : kStepCatalog) {
            if (spec.id == id) return &spec;
        }
        return nullptr;
    }

    /* Exact lookup (no trimming, no case folding), scoped to one backend: the
     * owner section already names the backend, so a token from another backend
     * is not a match. */
    [[nodiscard]] constexpr const StepSpec *step_spec_from_token(
            std::string_view token, BackendKind backend) noexcept {
        for (const StepSpec &spec : kStepCatalog) {
            if (spec.token == token && spec.backend == backend) return &spec;
        }
        return nullptr;
    }

    /* ---- alias mapping onto today's step sets (M1: alias only) ---------- */
    inline constexpr StepId kW1W3StepIds[] = {StepId::W1, StepId::W2, StepId::W3};
    inline constexpr StepId kW1W2StepIds[] = {StepId::W1, StepId::W2};
    inline constexpr StepId kPageCacheWriteStepIds[] = {StepId::PageCacheWrite};

    struct StepSetAlias final {
        StepSetKind kind;
        BackendKind backend;
        std::span<const StepId> steps;
    };

    inline constexpr StepSetAlias kStepSetAliases[] = {
        {StepSetKind::W1W3, BackendKind::Cve2026_43499, kW1W3StepIds},
        {StepSetKind::W1W2, BackendKind::Cve2026_43499, kW1W2StepIds},
        {StepSetKind::PageCacheWrite, BackendKind::Cve2026_43284, kPageCacheWriteStepIds},
    };
    inline constexpr std::size_t kStepSetAliasCount = std::size(kStepSetAliases);

    [[nodiscard]] constexpr const StepSetAlias *step_set_alias(StepSetKind kind) noexcept {
        for (const StepSetAlias &alias : kStepSetAliases) {
            if (alias.kind == kind) return &alias;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr bool step_is_covered(StepId id) noexcept {
        for (const StepSetAlias &alias : kStepSetAliases) {
            for (const StepId alias_id : alias.steps) {
                if (alias_id == id) return true;
            }
        }
        return false;
    }

    /* M2 registration point: a per-step executor declares its id. */
    template<typename Executor>
    concept StepExecution = requires {
        { Executor::step_id } -> std::convertible_to<StepId>;
    };

    namespace step_detail {
        [[nodiscard]] constexpr bool ids_unique() noexcept {
            for (std::size_t i = 0; i < kStepCount; ++i) {
                for (std::size_t j = i + 1; j < kStepCount; ++j) {
                    if (kStepCatalog[i].id == kStepCatalog[j].id) return false;
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool tokens_unique() noexcept {
            for (std::size_t i = 0; i < kStepCount; ++i) {
                for (std::size_t j = i + 1; j < kStepCount; ++j) {
                    if (kStepCatalog[i].token == kStepCatalog[j].token) return false;
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool backend_slots_complete(BackendKind backend) noexcept {
            const std::size_t count = backend_step_count(backend);
            for (std::size_t slot = 0; slot < count; ++slot) {
                if (step_at_slot(backend, static_cast<std::uint8_t>(slot)) == nullptr) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool slots_contiguous() noexcept {
            for (const StepSpec &spec : kStepCatalog) {
                if (step_at_slot(spec.backend, spec.slot) != &spec) return false;
                if (!backend_slots_complete(spec.backend)) return false;
            }
            return true;
        }

        [[nodiscard]] constexpr bool deps_valid() noexcept {
            for (const StepSpec &spec : kStepCatalog) {
                for (std::uint8_t bit = 0; bit < 8U; ++bit) {
                    const std::uint8_t mask = static_cast<std::uint8_t>(1U << bit);
                    if ((spec.deps_mask & mask) == 0U) continue;
                    /* Only lower slots, and only inside the same backend. */
                    if (bit >= spec.slot) return false;
                    if (step_at_slot(spec.backend, bit) == nullptr) return false;
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool aliases_valid() noexcept {
            for (const StepSetAlias &alias : kStepSetAliases) {
                for (std::size_t i = 0; i < alias.steps.size(); ++i) {
                    const StepSpec *spec = step_spec(alias.steps[i]);
                    if (spec == nullptr || spec->backend != alias.backend) return false;
                    /* U4: canonical (prefix) order -- element i is slot i. */
                    if (spec->slot != i) return false;
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool aliases_cover_catalog() noexcept {
            for (const StepSpec &spec : kStepCatalog) {
                if (!step_is_covered(spec.id)) return false;
            }
            return true;
        }

        [[nodiscard]] constexpr bool alias_kinds_complete() noexcept {
            for (const StepSetKind kind : {StepSetKind::W1W2, StepSetKind::W1W3,
                                           StepSetKind::PageCacheWrite}) {
                if (step_set_alias(kind) == nullptr) return false;
            }
            return true;
        }
    } // namespace step_detail

    static_assert(step_detail::ids_unique(), "step ids must be unique");
    static_assert(step_detail::tokens_unique(), "step tokens must be unique");
    static_assert(step_detail::slots_contiguous(),
                  "each backend needs contiguous slots starting at 0");
    static_assert(step_detail::deps_valid(),
                  "deps must reference lower slots of the same backend");
    static_assert(step_detail::aliases_valid(),
                  "aliases must use the canonical (prefix) order of their backend");
    static_assert(step_detail::aliases_cover_catalog(),
                  "every catalogued step must be covered by an execution surface");
    static_assert(step_detail::alias_kinds_complete(),
                  "every catalogued StepSetKind needs an alias");
} // namespace ghostlock::contract

#endif