/** @file tests/step_queue_test.cpp
 *  Reading a backend's step queue out of a document.
 *
 *  The queue is the selection's step list: an array of entries, each naming one
 *  step by the token the step catalogue spells. Sections: 1. the device's own
 *  shape reads back in order as step ids; 2. a token the catalogue does not know
 *  is refused and names itself; 3. what cannot be a queue is refused, and nothing
 *  is handed over on a refusal.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "mpack.h"
#include "profile/glkv3_document.hpp"

namespace {
    using ghostlock::contract::StepId;
    using ghostlock::profile::Document;
    using ghostlock::profile::DocumentVerdict;


    constexpr const char *kSegment = "backend.cve_2026_43499";

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A document whose section carries the given step tokens as a queue. */
    char *build(const char *const *steps, std::size_t count, bool wrap_in_array,
                std::size_t *out_size) {
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
        mpack_start_map(&writer, 1U);
        mpack_write_cstr(&writer, "queue");
        if (!wrap_in_array) {
            mpack_write_cstr(&writer, "not-a-queue");
        } else {
            mpack_start_array(&writer, static_cast<std::uint32_t>(count));
            for (std::size_t i = 0U; i < count; ++i) {
                mpack_start_map(&writer, 1U);
                mpack_write_cstr(&writer, "step");
                mpack_write_cstr(&writer, steps[i]);
                mpack_finish_map(&writer);
            }
            mpack_finish_array(&writer);
        }
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        expect(mpack_writer_destroy(&writer) == mpack_ok, "the document was written");
        *out_size = size;
        return data;
    }

    /* ---- 1: the device's own shape ---- */
    void the_queue_reads() {
        const char *steps[] = {"w1", "w2", "w3"};
        std::size_t size = 0U;
        char *data = build(steps, 3U, true, &size);
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the document parses");
        StepId out[4] = {};
        const auto result = document.step_queue(std::string_view(kSegment), out, 4U);
        expect(result.verdict == Document::QueueVerdict::Ok && result.count == 3U,
               "three steps are read");
        expect(out[0U] == StepId::W1 && out[1U] == StepId::W2 && out[2U] == StepId::W3,
               "and they are w1, w2, w3 in the order the document gave them");
        MPACK_FREE(data);
    }

    /* ---- 2: a token the catalogue does not know ---- */
    void an_unknown_step_is_named() {
        const char *steps[] = {"w1", "w9", "w3"};
        std::size_t size = 0U;
        char *data = build(steps, 3U, true, &size);
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the document parses");
        StepId out[4] = {};
        const auto result = document.step_queue(std::string_view(kSegment), out, 4U);
        expect(result.verdict == Document::QueueVerdict::StepUnknown,
               "a step the catalogue does not declare is refused");
        expect(std::strcmp(result.bad_step, "w9") == 0,
               "and the refusal names the token that failed, as a copy this side owns");
        expect(std::string_view(Document::queue_verdict_name(result.verdict)) ==
                   std::string_view("unknown-op"),
               "with the registered name");
        expect(result.count == 0U, "and nothing is handed over");
        MPACK_FREE(data);
    }

    /* ---- 3: what is not a queue ---- */
    void what_is_not_a_queue_is_refused() {
        const char *steps[] = {"w1"};
        std::size_t size = 0U;
        char *data = build(steps, 1U, false, &size);
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the document parses");
        StepId out[4] = {};
        const auto not_an_array = document.step_queue(std::string_view(kSegment), out, 4U);
        expect(not_an_array.verdict == Document::QueueVerdict::QueueMissing,
               "a queue that is not an array has no plan");
        expect(std::string_view(Document::queue_verdict_name(not_an_array.verdict)) ==
                   std::string_view("plan-missing"),
               "and is named so");
        MPACK_FREE(data);

        std::size_t many_size = 0U;
        char *many = build(steps, 1U, true, &many_size);
        Document other;
        expect(other.parse(reinterpret_cast<const std::uint8_t *>(many), many_size) ==
                   DocumentVerdict::Ok,
               "the second document parses");
        const auto no_room = other.step_queue(std::string_view(kSegment), out, 0U);
        expect(no_room.verdict == Document::QueueVerdict::TooManySteps,
               "a caller with no room is refused rather than overrun");
        expect(no_room.count == 0U, "and gets nothing");
        Document closed;
        expect(closed.step_queue(std::string_view(kSegment), out, 4U).verdict ==
                   Document::QueueVerdict::DocumentInvalid,
               "an unparsed document has no queue either");
        MPACK_FREE(many);
    }
}  // namespace

int main() {
    std::printf("step queue: reading it\n");
    the_queue_reads();
    std::printf("step queue: an unknown step\n");
    an_unknown_step_is_named();
    std::printf("step queue: what is not a queue\n");
    what_is_not_a_queue_is_refused();
    if (g_failures != 0U) {
        std::printf("step queue: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("step queue: PASS (ordered steps + named refusals + all or nothing)\n");
    return 0;
}
