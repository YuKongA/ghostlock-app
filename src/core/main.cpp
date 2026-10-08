/** @file main.cpp
 *  A0 host entry: the skeleton proves it is alive by printing its identity and
 *  the design-family layer list.
 *
 *  What it is not: it reads no configuration, owns no window resource and
 *  contains no numeric tuning value (Q2 rule 1: numbers live in the profile).
 *  The layer catalog it prints is the compile-time structure from
 *  contract/layer_scope.hpp; the three-way resource classification comes from
 *  the restored P4 contract (contract/resource_scope.hpp).
 *
 *  Build: make -C src host   (see src/Makefile; the tree is host-first, the
 *  Android cross build is added by a later phase).
 */
#include <cstddef>
#include <cstdio>

#include "backend/layer.hpp"
#include "contract/layer.hpp"
#include "contract/layer_scope.hpp"
#include "contract/resource_scope.hpp"
#include "memory/layer.hpp"
#include "pipeline/layer.hpp"
#include "profile/layer.hpp"
#include "race/layer.hpp"
#include "script/layer.hpp"
#include "session/layer.hpp"
#include "support/layer.hpp"
#include "terminal/layer.hpp"

namespace {
    /** Printable name of the resource face a layer may hold. */
    [[nodiscard]] const char *scope_name(ghostlock::contract::ResourceScope scope) noexcept {
        using ghostlock::contract::ResourceScope;
        switch (scope) {
            case ResourceScope::Window: return "window";
            case ResourceScope::Op: return "op";
            case ResourceScope::Script: return "script";
        }
        return "unknown";
    }
}  // namespace

int main() {
    std::printf("ghostlock skeleton (A0)\n");
    std::printf("layers total=%zu\n", ghostlock::contract::kLayerCount);
    for (const ghostlock::contract::LayerEntry &entry : ghostlock::contract::kLayers) {
        std::printf("  layer %-9s scope=%s\n", entry.name, scope_name(entry.scope));
    }
    std::printf("resource-scope rules=%zu exit-paths=%zu\n", ghostlock::contract::kScopeRuleCount,
                ghostlock::contract::kExitPathCount);
    return 0;
}
