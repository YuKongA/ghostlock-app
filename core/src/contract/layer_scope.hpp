#pragma once
/** @file contract/layer_scope.hpp
 *  A0 skeleton: the design-family layer catalog.
 *
 *  Every layer of the rewrite is listed once here with the resource face it may
 *  hold. The three-way classification itself comes from the P4 contract
 *  (contract/resource_scope.hpp); this catalog only fixes WHICH layer is allowed
 *  to reach which face, so a later phase cannot quietly widen a layer's reach.
 *
 *  Mechanism constant exception (Q2 rule 2): layer names and their allowed
 *  resource face are structure, not tuning values -- they describe the design
 *  family (rewrite-plan-v2 section 2), so they are defined in C++ on purpose.
 *
 *  Invariants (compile-time, and each one can fail):
 *   - exactly ten layers, all names unique and non-empty;
 *   - exactly one layer may own window-scoped resources (pipeline, per P4
 *     section 1: AttackWindow is the only owner);
 *   - exactly one layer is script-scoped (script, per ADR-0010).
 */
#include <cstddef>

#include "contract/resource_scope.hpp"

namespace ghostlock::contract {
    /** One row of the layer catalog. */
    struct LayerEntry final {
        const char *name;
        /** The most privileged resource face this layer may hold. */
        ResourceScope scope;
    };

    /** The design family (rewrite-plan-v2 section 2), frozen in one place. */
    inline constexpr LayerEntry kLayers[] = {
        {"contract", ResourceScope::Op},
        {"support", ResourceScope::Op},
        {"memory", ResourceScope::Op},
        {"backend", ResourceScope::Op},
        {"pipeline", ResourceScope::Window},
        {"terminal", ResourceScope::Op},
        {"script", ResourceScope::Script},
        {"session", ResourceScope::Op},
        {"profile", ResourceScope::Op},
    };
    inline constexpr std::size_t kLayerCount = sizeof(kLayers) / sizeof(kLayers[0]);

    /** constexpr string equality for the uniqueness check. */
    [[nodiscard]] constexpr bool layer_same_text(const char *a, const char *b) noexcept {
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        return *a == *b;
    }

    [[nodiscard]] constexpr bool layer_names_are_unique() noexcept {
        for (std::size_t i = 0; i < kLayerCount; ++i) {
            if (kLayers[i].name == nullptr || kLayers[i].name[0] == '\0') {
                return false;
            }
            for (std::size_t j = i + 1; j < kLayerCount; ++j) {
                if (layer_same_text(kLayers[i].name, kLayers[j].name)) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] constexpr std::size_t layers_with_scope(ResourceScope scope) noexcept {
        std::size_t count = 0;
        for (std::size_t i = 0; i < kLayerCount; ++i) {
            if (kLayers[i].scope == scope) {
                ++count;
            }
        }
        return count;
    }

    /* Nine, corrected from ten: the race is not a layer of its own. It is 43499's
     *  own mechanism and lives inside that backend, which is where the retired tree
     *  kept its futex machinery too. */
    static_assert(kLayerCount == 9, "the design family has nine layers");
    static_assert(layer_names_are_unique(), "layer names must be unique and non-empty");
    static_assert(layers_with_scope(ResourceScope::Window) == 1,
                  "exactly one layer owns window-scoped resources (pipeline)");
    static_assert(layers_with_scope(ResourceScope::Script) == 1,
                  "exactly one layer is script-scoped (script)");
    static_assert(layers_with_scope(ResourceScope::Op) == 7, "the remaining seven layers are op-scoped");
}  // namespace ghostlock::contract
