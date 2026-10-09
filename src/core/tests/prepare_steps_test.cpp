/** @file tests/prepare_steps_test.cpp
 *  The page preparation's order, pinned.
 *
 *  The header asserts what must hold (twelve steps, each once, and three orderings
 *  the mechanism depends on). What a test adds is the sequence itself: the exact
 *  order the retired tree ran, byte for byte, so that a reordering that happens to
 *  keep the three asserted relations still fails.
 */
#include <cstddef>
#include <cstdio>

#include "backend/cve_2026_43499/memory/prepare_steps.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::kPrepareOrder;
    using ghostlock::backend::cve_2026_43499::memory::kPrepareStepCount;
    using ghostlock::backend::cve_2026_43499::memory::PrepareStep;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_order_is_the_one_the_tree_ran() {
        const PrepareStep expected[12U] = {
            PrepareStep::Cleanup,       PrepareStep::Contexts,  PrepareStep::PreChildren,
            PrepareStep::SprayChildren, PrepareStep::Snitch,    PrepareStep::PostChildren,
            PrepareStep::KillPartials,  PrepareStep::LeakLoop,  PrepareStep::Shaping,
            PrepareStep::PinAndYield,   PrepareStep::Reclaim,   PrepareStep::Align,
        };
        expect(kPrepareStepCount == 12U, "a preparation is twelve steps");
        for (std::size_t i = 0U; i < 12U && i < kPrepareStepCount; ++i) {
            expect(kPrepareOrder[i] == expected[i],
                   "each step is where the retired tree put it");
        }
    }
}  // namespace

int main() {
    std::printf("prepare steps: the order\n");
    the_order_is_the_one_the_tree_ran();
    if (g_failures != 0U) {
        std::printf("prepare steps: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("prepare steps: PASS (twelve steps, in the tree's order)\n");
    return 0;
}
