/** @file tests/document_pairs_test.cpp
 *  From a document segment to the pairs the memory layer reads.
 *
 *  Sections: 1. the keys come out in the order asked for, with their values, and
 *  the memory layer's read path finds them; 2. a key that is not there, or is
 *  there as something other than a number, is refused with its own name; 3. a
 *  request larger than the caller's buffer is refused, and a refusal never hands
 *  over a half-filled list.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "mpack.h"
#include "memory/target_profile.hpp"
#include "profile/document_pairs.hpp"

namespace {
    using ghostlock::memory::read_key;
    using ghostlock::memory::KeyValue;
    using ghostlock::memory::KeyView;
    using ghostlock::memory::ProfileRejection;
    using ghostlock::profile::Document;
    using ghostlock::profile::DocumentVerdict;
    using ghostlock::profile::pairs_verdict_name;
    using ghostlock::profile::PairsResult;
    using ghostlock::profile::PairsVerdict;
    using ghostlock::profile::read_section_pairs;

    constexpr const char *kSegment = "backend.cve_2026_43499.route.multicast_waiter";

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The segment every section below reads, with one key deliberately typed
     *  wrong so the type refusal has something to refuse. */
    char *build(std::size_t *out_size) {
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
        mpack_start_map(&writer, 4U);
        mpack_write_cstr(&writer, "attempts");
        mpack_write_uint(&writer, 128U);
        mpack_write_cstr(&writer, "arm_sequence");
        mpack_write_uint(&writer, 16U);
        mpack_write_cstr(&writer, "arm_hold");
        mpack_write_uint(&writer, 20000U);
        mpack_write_cstr(&writer, "as_text");
        mpack_write_cstr(&writer, "128");
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        expect(mpack_writer_destroy(&writer) == mpack_ok, "the document was written");
        *out_size = size;
        return data;
    }

    /* ---- 1: the pairs come out, and the memory layer reads them ---- */
    void the_pairs_come_out(const char *data, std::size_t size) {
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the document parses");
        const char *keys[] = {"attempts", "arm_sequence", "arm_hold"};
        KeyValue pairs[3];
        const PairsResult result =
            read_section_pairs(document, std::string_view(kSegment), keys, 3U, pairs, 3U);
        expect(result.verdict == PairsVerdict::Ok && result.filled == 3U,
               "three keys are answered");
        expect(std::string_view(pairs[0U].key) == std::string_view("attempts") &&
                   pairs[0U].value == 128U,
               "the first pair is the key that was asked for first");
        expect(pairs[2U].value == 20000U, "and the last carries its value");
        KeyView view;
        view.items = pairs;
        view.count = result.filled;
        const auto hold = read_key(view, std::string_view("arm_hold"));
        expect(hold.ok && hold.value == 20000U,
               "the memory layer finds the value through its own read path");
        expect(!read_key(view, std::string_view("not_declared")).ok,
               "and refuses one that is not in the list");
        expect(read_key(view, std::string_view("not_declared")).reason ==
                   ProfileRejection::MissingKey,
               "with its own named reason");
    }

    /* ---- 2: what is not there, or is the wrong kind ---- */
    void missing_keys_are_named(const char *data, std::size_t size) {
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the document parses");
        KeyValue pairs[3];
        const char *absent[] = {"attempts", "not_declared"};
        const PairsResult missing =
            read_section_pairs(document, std::string_view(kSegment), absent, 2U, pairs, 3U);
        expect(missing.verdict == PairsVerdict::KeyUnavailable, "an absent key is refused");
        expect(missing.missing_key != nullptr &&
                   std::strcmp(missing.missing_key, "not_declared") == 0,
               "and the refusal names the key that failed");
        expect(missing.filled == 0U, "and hands over nothing at all");
        expect(std::string_view(pairs_verdict_name(missing.verdict)) ==
                   std::string_view("tuning-missing"),
               "the refusal carries the registered name");

        const char *wrong_type[] = {"as_text"};
        const PairsResult typed =
            read_section_pairs(document, std::string_view(kSegment), wrong_type, 1U, pairs, 3U);
        expect(typed.verdict == PairsVerdict::KeyUnavailable,
               "a key that is not a number is refused");
        expect(typed.missing_key != nullptr && std::strcmp(typed.missing_key, "as_text") == 0,
               "and it is named too");

        const char *other_segment[] = {"attempts"};
        expect(read_section_pairs(document, std::string_view("backend.other"), other_segment, 1U,
                                  pairs, 3U)
                       .verdict == PairsVerdict::KeyUnavailable,
               "a segment that is not there has no keys either");
    }

    /* ---- 3: the caller's buffer ---- */
    void a_short_buffer_is_refused(const char *data, std::size_t size) {
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the document parses");
        const char *keys[] = {"attempts", "arm_sequence", "arm_hold"};
        KeyValue one[1];
        const PairsResult too_many =
            read_section_pairs(document, std::string_view(kSegment), keys, 3U, one, 1U);
        expect(too_many.verdict == PairsVerdict::TooManyKeys, "a short buffer is refused");
        expect(std::string_view(pairs_verdict_name(too_many.verdict)) ==
                   std::string_view("budget-exceeded"),
               "and named");
        expect(too_many.filled == 0U, "and nothing was written into it");
        Document closed;
        expect(read_section_pairs(closed, std::string_view(kSegment), keys, 1U, one, 1U).verdict ==
                   PairsVerdict::DocumentInvalid,
               "an unparsed document fills nothing");
    }
}  // namespace

int main() {
    std::size_t size = 0U;
    char *data = build(&size);
    std::printf("document pairs: the pairs come out\n");
    the_pairs_come_out(data, size);
    std::printf("document pairs: refusals\n");
    missing_keys_are_named(data, size);
    std::printf("document pairs: the caller's buffer\n");
    a_short_buffer_is_refused(data, size);
    MPACK_FREE(data);
    if (g_failures != 0U) {
        std::printf("document pairs: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("document pairs: PASS (ordered pairs + named refusals + all or nothing)\n");
    return 0;
}
