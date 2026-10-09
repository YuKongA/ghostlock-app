/** @file tests/abi_offsets_test.cpp
 *  The task_struct offsets, from a document to the backend.
 *
 *  This walks the path the runtime walks: a document written here, read by the
 *  profile layer, bridged into pairs and handed to the backend, which refuses by
 *  name when a field is absent or does not fit a 32-bit offset.
 *
 *  Sections: 1. the four offsets come out in their own members; 2. an absent
 *  field names itself and leaves the caller's structure untouched; 3. a value
 *  that is not an offset is refused as a width problem rather than truncated.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "mpack.h"
#include "backend/cve_2026_43499/abi.hpp"
#include "profile/document_pairs.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::abi_verdict_name;
    using ghostlock::backend::cve_2026_43499::AbiVerdict;
    using ghostlock::backend::cve_2026_43499::fill_task_offsets;
    using ghostlock::backend::cve_2026_43499::kTaskFields;
    using ghostlock::backend::cve_2026_43499::kTaskFieldCount;
    using ghostlock::backend::cve_2026_43499::OffsetLookup;
    using ghostlock::backend::cve_2026_43499::TaskOffsets;
    using ghostlock::memory::KeyValue;
    using ghostlock::memory::KeyView;
    using ghostlock::profile::Document;
    using ghostlock::profile::DocumentVerdict;
    using ghostlock::profile::PairsVerdict;
    using ghostlock::profile::read_section_pairs;

    constexpr const char *kSegment = "backend.cve_2026_43499.abi.task_struct";
    constexpr std::size_t kKeys = 4U;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A document with the four offsets; pi_waiters carries the caller's value. */
    char *build(std::uint64_t pi_waiters, std::size_t *out_size) {
        const std::uint64_t values[] = {0x18U, pi_waiters, 0x10U, 0x28U};
        char *data = nullptr;
        std::size_t size = 0U;
        mpack_writer_t writer;
        mpack_writer_init_growable(&writer, &data, &size);
        mpack_start_map(&writer, 2U);
        mpack_write_cstr(&writer, "schema");
        mpack_write_uint(&writer, 3U);
        mpack_write_cstr(&writer, "sections");
        mpack_start_map(&writer, 1U);
        mpack_write_cstr(&writer, kSegment);
        mpack_start_map(&writer, static_cast<std::uint32_t>(kKeys));
        for (std::size_t i = 0U; i < kKeys; ++i) {
            mpack_write_cstr(&writer, kTaskFields[i]);
            mpack_write_uint(&writer, values[i]);
        }
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        expect(mpack_writer_destroy(&writer) == mpack_ok, "the document was written");
        *out_size = size;
        return data;
    }

    /** Bridge the segment into pairs through the profile layer. */
    bool bridge(const char *data, std::size_t size, const char *const *keys,
                std::size_t key_count, KeyValue *pairs, KeyView &view) {
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the document parses");
        const auto bridged = read_section_pairs(document, std::string_view(kSegment), keys,
                                                key_count, pairs, kKeys);
        if (bridged.verdict != PairsVerdict::Ok) {
            return false;
        }
        view.items = pairs;
        view.count = bridged.filled;
        return true;
    }

    /* ---- 1: the offsets come out in their own members ---- */
    void the_offsets_come_out() {
        std::size_t size = 0U;
        char *data = build(0x20U, &size);
        KeyValue pairs[kKeys];
        KeyView view;
        expect(bridge(data, size, kTaskFields, kKeys, pairs, view), "the segment bridges");
        TaskOffsets offsets;
        const OffsetLookup lookup = fill_task_offsets(view, offsets);
        expect(lookup.verdict == AbiVerdict::Ok, "the four offsets are read");
        expect(offsets.pi_lock == 0x18U, "pi_lock lands in its own member");
        expect(offsets.pi_waiters == 0x20U, "pi_waiters lands in its own member");
        expect(offsets.pi_top_task == 0x10U, "pi_top_task lands in its own member");
        expect(offsets.pi_blocked_on == 0x28U, "pi_blocked_on lands in its own member");
        MPACK_FREE(data);
    }

    /* ---- 2: an absent field names itself, and nothing is written ---- */
    void an_absent_field_is_named() {
        std::size_t size = 0U;
        char *data = build(0x20U, &size);
        const char *three[] = {"pi_lock", "pi_waiters", "pi_blocked_on"};
        KeyValue pairs[kKeys];
        KeyView view;
        expect(bridge(data, size, three, 3U, pairs, view), "three fields bridge");
        TaskOffsets offsets;
        offsets.pi_lock = 0xAAU;
        offsets.pi_waiters = 0xBBU;
        offsets.pi_top_task = 0xCCU;
        offsets.pi_blocked_on = 0xDDU;
        const TaskOffsets before = offsets;
        const OffsetLookup lookup = fill_task_offsets(view, offsets);
        expect(lookup.verdict == AbiVerdict::FieldMissing, "the absent field is refused");
        expect(lookup.field != nullptr && std::strcmp(lookup.field, "pi_top_task") == 0,
               "and the refusal names it");
        expect(std::string_view(abi_verdict_name(lookup.verdict)) == std::string_view("tuning-missing"),
               "with the registered name");
        expect(offsets.pi_lock == before.pi_lock && offsets.pi_waiters == before.pi_waiters &&
                   offsets.pi_top_task == before.pi_top_task &&
                   offsets.pi_blocked_on == before.pi_blocked_on,
               "and the caller's structure was not touched at all");
        MPACK_FREE(data);
    }

    /* ---- 3: a value that is not an offset ---- */
    void an_impossible_offset_is_refused() {
        std::size_t size = 0U;
        char *data = build(0x100000000ULL, &size);
        KeyValue pairs[kKeys];
        KeyView view;
        expect(bridge(data, size, kTaskFields, kKeys, pairs, view), "the segment bridges");
        TaskOffsets offsets;
        const OffsetLookup lookup = fill_task_offsets(view, offsets);
        expect(lookup.verdict == AbiVerdict::FieldTooWide,
               "a value wider than a 32-bit offset is refused");
        expect(lookup.field != nullptr && std::strcmp(lookup.field, "pi_waiters") == 0,
               "and that field is named");
        expect(std::string_view(abi_verdict_name(lookup.verdict)) == std::string_view("width-mismatch"),
               "with the width refusal's name");
        MPACK_FREE(data);
    }
}  // namespace

int main() {
    std::printf("abi offsets: reading them\n");
    the_offsets_come_out();
    std::printf("abi offsets: an absent field\n");
    an_absent_field_is_named();
    std::printf("abi offsets: an impossible value\n");
    an_impossible_offset_is_refused();
    if (g_failures != 0U) {
        std::printf("abi offsets: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("abi offsets: PASS (typed offsets + named refusals + all or nothing)\n");
    return 0;
}
