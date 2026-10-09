/** @file backend/cve_2026_43499/memory/shaping_plan.cpp
 *  The three pieces of socket work, in the order the retired tree did them.
 */
#include "backend/cve_2026_43499/memory/shaping_plan.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    ShapingVerdict build_shaping_plan(const ShapingInputs &inputs, ShapingPlan &out) noexcept {
        out = ShapingPlan{};
        if (inputs.socket_buffer <= 0 || inputs.send_size == 0U) {
            return ShapingVerdict::InputsInvalid;
        }
        /* The reclaim pair: its first end gets the buffer and stops blocking. The
         *  order of the two flags is the order the retired tree applied them. */
        out.actions[0].role = ShapingPairRole::Reclaim;
        out.actions[0].end = ShapingEnd::First;
        out.actions[0].set_buffer = true;
        out.actions[0].buffer_bytes = inputs.socket_buffer;
        out.actions[1].role = ShapingPairRole::Reclaim;
        out.actions[1].end = ShapingEnd::First;
        out.actions[1].nonblocking = true;
        /* And the shaping pair sends one buffer from its first end. */
        out.actions[2].role = ShapingPairRole::Shaping;
        out.actions[2].end = ShapingEnd::First;
        out.actions[2].sends = true;
        out.actions[2].send_bytes = inputs.send_size;
        out.action_count = kShapingActionCount;
        return ShapingVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
