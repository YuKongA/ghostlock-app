/** @file tests/abi_payload_offsets_test.cpp
 *  Filling the five payload offsets, or naming the one that is absent.
 *
 *  The rule the task offsets already follow applies here: all-or-nothing. A caller that gets a
 *  refusal must find its structure untouched, because a half-read layout that looks filled is worse
 *  than an empty one. Each refusal is checked for the field it names, so the missing asset entry is
 *  identified rather than guessed, and the width refusal is checked separately from the missing one.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/abi.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::AbiVerdict;
    using ghostlock::backend::cve_2026_43499::fill_payload_offsets;
    using ghostlock::backend::cve_2026_43499::kPayloadFieldCount;
    using ghostlock::backend::cve_2026_43499::kPayloadFields;
    using ghostlock::backend::cve_2026_43499::OffsetLookup;
    using ghostlock::backend::cve_2026_43499::PayloadOffsets;
    using ghostlock::memory::KeyValue;
    using ghostlock::memory::KeyView;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The five keys with distinct values, so a mis-mapping shows up as a wrong number. */
    struct Pairs final {
        KeyValue items[8U] = {};
        std::size_t count = 0U;
        void add(const char *key, std::uint64_t value) {
            if (count < 8U) {
                items[count] = KeyValue{std::string_view(key), value};
                ++count;
            }
        }
        KeyView view() const { return KeyView{items, count}; }
    };

    Pairs complete() {
        Pairs p;
        for (std::size_t i = 0U; i < kPayloadFieldCount; ++i) {
            p.add(kPayloadFields[i], 0x100U * (i + 1U));
        }
        return p;
    }

    void all_five_fill_and_land_in_their_own_members() {
        const Pairs pairs = complete();
        PayloadOffsets out{};
        const OffsetLookup r = fill_payload_offsets(pairs.view(), out);
        expect(r.verdict == AbiVerdict::Ok, "a complete set fills");
        expect(out.lock_offset == 0x100U, "the first field lands in the first member");
        expect(out.waiter_w0_offset == 0x200U, "and the second in the second");
        expect(out.fake_task_offset == 0x300U, "and so on");
        expect(out.cred_copy_offset == 0x400U, "and so on");
        expect(out.fops_table_offset == 0x500U, "through the last");
        std::printf("abi payload offsets: %zu fields\n", kPayloadFieldCount);
    }

    void each_missing_field_is_named_and_leaves_the_caller_untouched() {
        for (std::size_t skip = 0U; skip < kPayloadFieldCount; ++skip) {
            Pairs pairs;
            for (std::size_t i = 0U; i < kPayloadFieldCount; ++i) {
                if (i != skip) {
                    pairs.add(kPayloadFields[i], 0x10U * (i + 1U));
                }
            }
            PayloadOffsets out{};
            out.lock_offset = 0xDEADU;   /* a value that must survive a refusal */
            const OffsetLookup r = fill_payload_offsets(pairs.view(), out);
            expect(r.verdict == AbiVerdict::FieldMissing, "a missing field is refused");
            expect(r.field != nullptr && std::string_view(r.field) == kPayloadFields[skip],
                   "and the refusal names the field that is absent");
            expect(out.lock_offset == 0xDEADU,
                   "and the caller structure is untouched: a half layout is never handed over");
        }
    }

    void an_empty_source_is_a_missing_field_not_a_zero() {
        const KeyView empty{};
        PayloadOffsets out{};
        const OffsetLookup r = fill_payload_offsets(empty, out);
        expect(r.verdict == AbiVerdict::FieldMissing, "an empty source refuses");
        expect(r.field != nullptr && std::string_view(r.field) == kPayloadFields[0U],
               "and it names the first field, which is the first thing it looked for");
    }

    void a_value_wider_than_an_offset_is_refused_by_name() {
        Pairs pairs = complete();
        pairs.items[2U].value = 0x1FFFFFFFFULL;   /* wider than 32 bits */
        PayloadOffsets out{};
        const OffsetLookup r = fill_payload_offsets(pairs.view(), out);
        expect(r.verdict == AbiVerdict::FieldTooWide, "an over-wide value is refused");
        expect(r.field != nullptr && std::string_view(r.field) == kPayloadFields[2U],
               "and the refusal names the field that is too wide");
    }
}  // namespace

int main() {
    std::printf("abi payload offsets: the complete set\n");
    all_five_fill_and_land_in_their_own_members();
    std::printf("abi payload offsets: a missing field\n");
    each_missing_field_is_named_and_leaves_the_caller_untouched();
    std::printf("abi payload offsets: an empty source\n");
    an_empty_source_is_a_missing_field_not_a_zero();
    std::printf("abi payload offsets: an over-wide value\n");
    a_value_wider_than_an_offset_is_refused_by_name();
    if (g_failures != 0U) {
        std::printf("abi payload offsets: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("abi payload offsets: PASS (fills five, names what is absent, leaves nothing half-read)\n");
    return 0;
}
