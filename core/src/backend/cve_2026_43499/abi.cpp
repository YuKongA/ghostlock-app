/** @file backend/cve_2026_43499/abi.cpp
 *  Reading the four task_struct offsets, or saying which one is absent.
 */
#include "backend/cve_2026_43499/abi.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    namespace {
        /** A task_struct field is a 32-bit offset; anything wider is not one. */
        constexpr std::uint64_t kMaxOffset = 0xFFFFFFFFULL;

        OffsetLookup take(const memory::KeyView &view, const char *field,

                          std::uint32_t &slot) noexcept {
            OffsetLookup lookup{};
            lookup.field = field;
            const memory::KeyLookup found = memory::read_key(view, std::string_view(field));
            if (!found.ok) {
                lookup.verdict = AbiVerdict::FieldMissing;
                return lookup;
            }
            if (found.value > kMaxOffset) {
                lookup.verdict = AbiVerdict::FieldTooWide;
                return lookup;
            }
            slot = static_cast<std::uint32_t>(found.value);
            lookup.verdict = AbiVerdict::Ok;
            lookup.value = slot;
            return lookup;
        }
    }  // namespace

    OffsetLookup fill_task_offsets(const memory::KeyView &view, TaskOffsets &out) noexcept {
        /* Everything is read into a local first: the caller's structure is only
         * touched once all four have been read. */
        TaskOffsets staged{};
        OffsetLookup result = take(view, kTaskFields[0U], staged.pi_lock);
        if (result.verdict != AbiVerdict::Ok) {
            return result;
        }
        result = take(view, kTaskFields[1U], staged.pi_waiters);
        if (result.verdict != AbiVerdict::Ok) {
            return result;
        }
        result = take(view, kTaskFields[2U], staged.pi_top_task);
        if (result.verdict != AbiVerdict::Ok) {
            return result;
        }
        result = take(view, kTaskFields[3U], staged.pi_blocked_on);
        if (result.verdict != AbiVerdict::Ok) {
            return result;
        }
        out = staged;
        return result;
    }
}  // namespace ghostlock::backend::cve_2026_43499
