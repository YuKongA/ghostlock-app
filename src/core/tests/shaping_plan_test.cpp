/** @file tests/shaping_plan_test.cpp
 *  Which end of which pair, in what order.
 *
 *  The interesting part of this rule is not that sockets are opened but which end of
 *  each pair is configured: a buffer and a non-blocking flag on the reclaim pair's
 *  first end, and the send from the shaping pair's first end. Putting either on the
 *  other end leaves the send and the buffer on opposite sides, which the test pins
 *  by naming every field of every action in order.
 */
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/memory/shaping_plan.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::build_shaping_plan;
    using ghostlock::backend::cve_2026_43499::memory::ShapingEnd;
    using ghostlock::backend::cve_2026_43499::memory::ShapingInputs;
    using ghostlock::backend::cve_2026_43499::memory::ShapingPairRole;
    using ghostlock::backend::cve_2026_43499::memory::ShapingPlan;
    using ghostlock::backend::cve_2026_43499::memory::shaping_verdict_name;
    using ghostlock::backend::cve_2026_43499::memory::ShapingVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    ShapingInputs inputs() {
        ShapingInputs made;
        made.socket_buffer = 4096;
        made.send_size = 2048U;
        return made;
    }

    void the_shape_is_the_one_the_tree_used() {
        ShapingPlan plan;
        expect(build_shaping_plan(inputs(), plan) == ShapingVerdict::Ok, "the shape is built");
        expect(plan.action_count == 3U, "from three actions");
        expect(plan.actions[0].role == ShapingPairRole::Reclaim &&
                   plan.actions[0].end == ShapingEnd::First && plan.actions[0].set_buffer &&
                   plan.actions[0].buffer_bytes == 4096 && !plan.actions[0].sends,
               "the reclaim pair's first end gets the buffer, and sends nothing");
        expect(plan.actions[1].role == ShapingPairRole::Reclaim &&
                   plan.actions[1].end == ShapingEnd::First && plan.actions[1].nonblocking &&
                   !plan.actions[1].set_buffer,
               "then the same end stops blocking, and is not given a second buffer");
        expect(plan.actions[2].role == ShapingPairRole::Shaping &&
                   plan.actions[2].end == ShapingEnd::First && plan.actions[2].sends &&
                   plan.actions[2].send_bytes == 2048U && !plan.actions[2].set_buffer,
               "and the shaping pair's first end sends the send size");
        /* No action ever names the second end: the retired tree configured one side of
         *  each pair, and a shape that did both would be a different mechanism. */
        for (std::size_t i = 0U; i < plan.action_count; ++i) {
            expect(plan.actions[i].end != ShapingEnd::Second, "no action touches the second end");
        }
    }

    void what_cannot_be_shaped() {
        ShapingPlan plan;
        ShapingInputs no_buffer = inputs();
        no_buffer.socket_buffer = 0;
        expect(build_shaping_plan(no_buffer, plan) == ShapingVerdict::InputsInvalid,
               "a buffer of nothing is not a shape");
        ShapingInputs no_send = inputs();
        no_send.send_size = 0U;
        expect(build_shaping_plan(no_send, plan) == ShapingVerdict::InputsInvalid,
               "nor is a send of nothing");
        expect(plan.action_count == 0U, "and a refusal hands over nothing to walk");
        expect(std::string_view(shaping_verdict_name(ShapingVerdict::InputsInvalid)) ==
                   std::string_view("param-invalid"),
               "under the tree's name for a bad parameter");
    }
}  // namespace

int main() {
    std::printf("shaping plan: the shape\n");
    the_shape_is_the_one_the_tree_used();
    std::printf("shaping plan: what cannot be shaped\n");
    what_cannot_be_shaped();
    if (g_failures != 0U) {
        std::printf("shaping plan: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("shaping plan: PASS (which pair, which end, in order)\n");
    return 0;
}
