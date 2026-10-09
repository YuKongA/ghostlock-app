#pragma once
/** @file contract/step_catalog.hpp
 *  The step vocabulary and each step set's declared order.
 *
 *  Declaration only, and the one place a step name is spelled: an executor order
 *  is bound to this table by static_assert, and the exported manifest is built
 *  from it, so an order that drifts from the declaration fails the build rather
 *  than shipping a plan that disagrees with what the other end read.
 *
 *  A step's effect is its side-effect domain: diagnostics and warning text, never
 *  dispatch.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/kind.hpp"

namespace ghostlock::contract {
    /** One step. Unknown is the zero value and is never part of an order. */
    enum class StepId : std::uint8_t {
        Unknown = 0,
        W1 = 1,
        W2 = 2,
        W3 = 3,
        PageCacheWrite = 4,
    };
    inline constexpr std::size_t kStepIdCount = 5U;

    /** What a step is for, in the vocabulary the diagnostics use. */
    enum class StepEffect : std::uint8_t {
        None = 0,
        Selinux = 1,
        Credentials = 2,
        Seccomp = 3,
        PageCache = 4,
    };

    struct StepSpec final {
        StepId id;
        const char *token;
        StepEffect effect;
    };

    /** Every step, with the token the manifest exports for it. */
    inline constexpr StepSpec kStepCatalog[] = {
        {StepId::Unknown, "unknown", StepEffect::None},
        {StepId::W1, "w1", StepEffect::Selinux},
        {StepId::W2, "w2", StepEffect::Credentials},
        {StepId::W3, "w3", StepEffect::Seccomp},
        {StepId::PageCacheWrite, "pagecache_write", StepEffect::PageCache},
    };
    inline constexpr std::size_t kStepCount = sizeof(kStepCatalog) / sizeof(kStepCatalog[0]);

    /** One step set's declared order: index zero runs first. */
    struct StepSetOrder final {
        const char *step_set;
        const StepId *steps;
        std::size_t count;
    };

    /* The three orders. Design section 4.1: w1w3 has no filter to clear, w1w2 is
     * the same chain without seccomp, and a page cache write is a single step. */
    inline constexpr StepId kOrderW1W3[] = {StepId::W1, StepId::W2, StepId::W3};
    inline constexpr StepId kOrderW1W2[] = {StepId::W1, StepId::W2};
    inline constexpr StepId kOrderPageCache[] = {StepId::PageCacheWrite};
    inline constexpr StepSetOrder kStepSetOrders[] = {
        {"w1w3", kOrderW1W3, sizeof(kOrderW1W3) / sizeof(kOrderW1W3[0])},
        {"w1w2", kOrderW1W2, sizeof(kOrderW1W2) / sizeof(kOrderW1W2[0])},
        {"pagecache", kOrderPageCache, sizeof(kOrderPageCache) / sizeof(kOrderPageCache[0])},
    };
    inline constexpr std::size_t kStepSetCount =
        sizeof(kStepSetOrders) / sizeof(kStepSetOrders[0]);

    [[nodiscard]] constexpr const char *step_token(StepId id) noexcept {
        for (std::size_t i = 0U; i < kStepCount; ++i) {
            if (kStepCatalog[i].id == id) {
                return kStepCatalog[i].token;
            }
        }
        return "unknown";
    }

    [[nodiscard]] constexpr StepEffect step_effect_of(StepId id) noexcept {
        for (std::size_t i = 0U; i < kStepCount; ++i) {
            if (kStepCatalog[i].id == id) {
                return kStepCatalog[i].effect;
            }
        }
        return StepEffect::None;
    }

    /** The declared order of a step set, or nullptr when none is declared. */
    [[nodiscard]] constexpr const StepSetOrder *step_set_order(
        std::string_view step_set) noexcept {
        for (std::size_t i = 0U; i < kStepSetCount; ++i) {
            const std::string_view declared(kStepSetOrders[i].step_set);
            if (declared.size() == step_set.size() && declared == step_set) {
                return &kStepSetOrders[i];
            }
        }
        return nullptr;
    }

    /* ---- what must hold, checked at compile time ---- */

    /** No two steps share a token, and none carries the zero id. */
    [[nodiscard]] constexpr bool step_tokens_are_unique() noexcept {
        for (std::size_t i = 0U; i < kStepCount; ++i) {
            if (kStepCatalog[i].id == StepId::Unknown) {
                continue;
            }
            for (std::size_t j = i + 1U; j < kStepCount; ++j) {
                const std::string_view a(kStepCatalog[i].token);
                const std::string_view b(kStepCatalog[j].token);
                if (a.size() == b.size() && a == b) {
                    return false;
                }
            }
        }
        return true;
    }

    /** Every step set the component catalogue offers has a declared order, and
     *  every order is non-empty and free of the zero id. */
    [[nodiscard]] constexpr bool step_sets_are_covered() noexcept {
        for (std::size_t i = 0U; i < kComponentCount; ++i) {
            if (kComponents[i].kind != ComponentKind::Stepset) {
                continue;
            }
            const StepSetOrder *order = step_set_order(kComponents[i].token);
            if (order == nullptr || order->count == 0U) {
                return false;
            }
            for (std::size_t j = 0U; j < order->count; ++j) {
                if (order->steps[j] == StepId::Unknown) {
                    return false;
                }
            }
        }
        return true;
    }

    /** How many step sets this table declares an order for. */
    [[nodiscard]] constexpr std::size_t kStepSetDetermined() noexcept { return kStepSetCount; }

    static_assert(step_tokens_are_unique(), "two steps share a token");
    static_assert(step_sets_are_covered(),
                  "a step set the catalogue offers has no declared order");
}  // namespace ghostlock::contract
