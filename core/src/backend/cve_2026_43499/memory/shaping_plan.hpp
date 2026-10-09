#pragma once
/** @file backend/cve_2026_43499/memory/shaping_plan.hpp
 *  The shape of the socket work a page preparation does: which pair, which end.
 *
 *  The retired tree opened two pairs and did different things to different ends of
 *  them: the reclaim pair has its first end given a socket buffer and made
 *  non-blocking, and the shaping pair has its first end send one buffer of the send
 *  size. Which end of which pair is part of the mechanism -- configuring the other
 *  end would leave the send and the buffer on opposite sides -- so it is written
 *  down as a plan rather than left to whoever calls next.
 *
 *  The buffer size and the send size are inputs: the retired tree had the buffer as a
 *  literal, which is exactly the kind of number this tree keeps out of its code.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** Which end of a pair a step acts on. */
    enum class ShapingEnd : unsigned char { First = 0, Second = 1 };

    /** What one pair is for. */
    enum class ShapingPairRole : unsigned char { Reclaim = 0, Shaping = 1 };

    /** One piece of socket work. */
    struct ShapingAction final {
        ShapingPairRole role = ShapingPairRole::Reclaim;
        ShapingEnd end = ShapingEnd::First;
        /** Give this end a socket buffer of that many bytes. */
        bool set_buffer = false;
        std::int32_t buffer_bytes = 0;
        /** Make this end non-blocking. */
        bool nonblocking = false;
        /** Send one buffer of that many bytes from this end. */
        bool sends = false;
        std::size_t send_bytes = 0U;
    };

    /** The sizes the shape is built from. */
    struct ShapingInputs final {
        std::int32_t socket_buffer = 0;
        std::size_t send_size = 0U;
    };

    /** Why a shape could not be built. Names follow the single table. */
    enum class ShapingVerdict : unsigned char { Ok = 0, InputsInvalid = 1 };

    [[nodiscard]] constexpr const char *shaping_verdict_name(ShapingVerdict verdict) noexcept {
        return verdict == ShapingVerdict::Ok ? "ok" : "param-invalid";
    }

    /** The actions, in the order the retired tree performed them. */
    inline constexpr std::size_t kShapingActionCount = 3U;

    struct ShapingPlan final {
        ShapingAction actions[kShapingActionCount] = {};
        std::size_t action_count = 0U;
    };

    /** Build the shape. Both sizes have to be positive: a buffer of nothing and a
     *  send of nothing are shapes nobody can act on, not shapes to run hopefully. */
    [[nodiscard]] ShapingVerdict build_shaping_plan(const ShapingInputs &inputs,
                                                    ShapingPlan &out) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::memory
