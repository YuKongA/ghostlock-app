#pragma once
/** @file backend/cve_2026_43499/abi.hpp
 *  The task_struct field offsets this backend reads, and where they come from.
 *
 *  The offsets are data: they live in the document under the backend's abi
 *  segment and arrive here as resolved pairs. This file names the fields it
 *  needs and holds each one as its own member, so a caller cannot pass an offset
 *  read for one field where another was meant -- the whole point of a typed view
 *  over four numbers that look alike.
 *
 *  Nothing defaults. A field the pairs do not carry is a named refusal, and a
 *  value that does not fit a 32-bit field offset is refused as a width problem
 *  rather than truncated.
 *
 *  Direction: the backend layer may not reach the profile layer (the profile
 *  layer is the last one and reads the document), so this takes the resolved
 *  pairs that the composition root hands down.
 */
#include <cstddef>
#include <cstdint>

#include "memory/target_profile.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** The task_struct fields this backend reads an offset for. A field is added
     *  here when a mechanism needs it, never in advance. */
    inline constexpr const char *kTaskFields[] = {"pi_lock", "pi_waiters", "pi_top_task",
                                                  "pi_blocked_on"};
    inline constexpr std::size_t kTaskFieldCount = sizeof(kTaskFields) / sizeof(kTaskFields[0]);

    /** Where those fields sit, each in its own member. */
    struct TaskOffsets final {
        std::uint32_t pi_lock = 0U;
        std::uint32_t pi_waiters = 0U;
        std::uint32_t pi_top_task = 0U;
        std::uint32_t pi_blocked_on = 0U;
    };

    /** Why the offsets could not be filled. Names come from the single table. */
    enum class AbiVerdict : unsigned char {
        Ok = 0,
        /** A field the pairs should carry is not there. */
        FieldMissing = 1,
        /** A field is wider than a 32-bit offset can hold. */
        FieldTooWide = 2,
    };

    [[nodiscard]] constexpr const char *abi_verdict_name(AbiVerdict verdict) noexcept {
        switch (verdict) {
            case AbiVerdict::Ok: return "ok";
            case AbiVerdict::FieldMissing: return "tuning-missing";
            case AbiVerdict::FieldTooWide: return "width-mismatch";
        }
        return "unknown";
    }

    /** The offset of one field, or a refusal saying which one was missing. */
    struct OffsetLookup final {
        AbiVerdict verdict = AbiVerdict::FieldMissing;
        const char *field = nullptr;
        std::uint32_t value = 0U;
    };

    /** Fill all four offsets from the pairs. All-or-nothing: on any refusal the
     *  caller's offsets are left untouched, so a half-read layout can never be
     *  used by mistake. */
    [[nodiscard]] OffsetLookup fill_task_offsets(const memory::KeyView &view,

                                                 TaskOffsets &out) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499
