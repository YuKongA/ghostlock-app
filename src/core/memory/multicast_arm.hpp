#pragma once
/** @file memory/multicast_arm.hpp
 *  Placing the two words a multicast arm needs, inside the buffer it owns.
 *
 *  The multicast route hands the kernel a buffer whose fields sit at offsets the
 *  profile declares: a fake task at waiter_offset plus task_offset, and a fake
 *  lock at waiter_offset plus lock_offset. Everything about that is geometry, so
 *  it is checked here rather than trusted: the two fields must fit inside the
 *  buffer, must not overlap each other, and a geometry that fails either test is
 *  refused before a byte is written.
 *
 *  All or nothing, as everywhere else on this path: a refusal leaves the buffer
 *  exactly as it was, because a half-armed buffer is worse than an unarmed one.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::memory {
    /** The geometry of one multicast buffer, from the profile. */
    struct MulticastGeometry final {
        std::size_t waiter_offset = 0U;
        std::size_t task_offset = 0U;
        std::size_t lock_offset = 0U;
        std::size_t buffer_size = 0U;
    };

    /** Why a geometry or a store was refused. Names follow the single table. */
    enum class ArmVerdict : unsigned char {
        Ok = 0,
        /** A field would fall outside the buffer. */
        OutOfBuffer = 1,
        /** The two fields would land on each other. */
        FieldsOverlap = 2,
        /** No buffer to write into. */
        NoBuffer = 3,
    };

    [[nodiscard]] constexpr const char *arm_verdict_name(ArmVerdict verdict) noexcept {
        switch (verdict) {
            case ArmVerdict::Ok: return "ok";
            case ArmVerdict::OutOfBuffer: return "budget-exceeded";
            case ArmVerdict::FieldsOverlap: return "param-invalid";
            case ArmVerdict::NoBuffer: return "param-invalid";
        }
        return "unknown";
    }

    /** How many bytes a buffer must hold for this geometry, or zero when the
     *  geometry cannot hold its own fields. */
    [[nodiscard]] std::size_t required_bytes(const MulticastGeometry &geometry,

                                             std::size_t word_bytes) noexcept;

    /** Check the geometry against a buffer of this size. */
    [[nodiscard]] ArmVerdict check_geometry(const MulticastGeometry &geometry,

                                            std::size_t buffer_size,

                                            std::size_t word_bytes) noexcept;

    /** Write the two words. Nothing is written unless every check passes. */
    [[nodiscard]] ArmVerdict encode_multicast_arm(std::uint8_t *buffer,

                                                  std::size_t buffer_size,

                                                  const MulticastGeometry &geometry,

                                                  std::uint64_t fake_task,

                                                  std::uint64_t fake_lock,

                                                  std::size_t word_bytes) noexcept;
}  // namespace ghostlock::memory
