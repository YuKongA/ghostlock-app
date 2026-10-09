/** @file tests/run_plan_test.cpp
 *  Resolving a document into the run it asks for.
 *
 *  The first section resolves the document the device produced for 43499, so the
 *  resolution is measured against a real one rather than one built here; the rest
 *  cover the queue-driven path and the refusals.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "mpack.h"
#include "profile/run_plan.hpp"

#ifndef DEVICE_DOCUMENT_HEX
#define DEVICE_DOCUMENT_HEX "../profile-core/src/test/resources/device-document-fixture.hex"
#endif

namespace {
    using ghostlock::contract::StepId;
    using ghostlock::profile::Document;
    using ghostlock::profile::DocumentVerdict;
    using ghostlock::profile::plan_verdict_name;
    using ghostlock::profile::PlanVerdict;
    using ghostlock::profile::resolve_run_plan;
    using ghostlock::profile::RunPlanResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    int hex_digit(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    }

    std::size_t load_device_document(std::uint8_t *out, std::size_t capacity) {
        std::FILE *file = std::fopen(DEVICE_DOCUMENT_HEX, "rb");
        if (file == nullptr) {
            std::printf("FAIL: cannot read %s\n", DEVICE_DOCUMENT_HEX);
            ++g_failures;
            return 0U;
        }
        char text[8192];
        const std::size_t got = std::fread(text, 1U, sizeof(text), file);
        std::fclose(file);
        std::size_t digits = 0U;
        while (digits < got && hex_digit(text[digits]) >= 0) {
            ++digits;
        }
        if (digits == 0U || digits % 2U != 0U || digits / 2U > capacity) {
            ++g_failures;
            return 0U;
        }
        for (std::size_t i = 0U; i < digits / 2U; ++i) {
            out[i] = static_cast<std::uint8_t>((hex_digit(text[i * 2U]) << 4) |
                                               hex_digit(text[i * 2U + 1U]));
        }
        return digits / 2U;
    }

    /** A document shaped like the device's own 43284 export, queue included. */
    char *build_43284(const char *const *steps, std::size_t count, std::size_t *out_size) {
        char *data = nullptr;
        std::size_t size = 0U;
        mpack_writer_t writer;
        mpack_writer_init_growable(&writer, &data, &size);
        mpack_start_map(&writer, 5U);
        mpack_write_cstr(&writer, "backend");
        mpack_write_cstr(&writer, "cve_2026_43284");
        mpack_write_cstr(&writer, "terminal");
        mpack_write_cstr(&writer, "umh_forward");
        mpack_write_cstr(&writer, "schema");
        mpack_write_uint(&writer, 3U);
        mpack_write_cstr(&writer, "kernel_major");
        mpack_write_uint(&writer, 5U);
        mpack_write_cstr(&writer, "sections");
        mpack_start_map(&writer, 1U);
        mpack_write_cstr(&writer, "backend.cve_2026_43284");
        mpack_start_map(&writer, 1U);
        mpack_write_cstr(&writer, "queue");
        mpack_start_array(&writer, static_cast<std::uint32_t>(count));
        for (std::size_t i = 0U; i < count; ++i) {
            mpack_start_map(&writer, 1U);
            mpack_write_cstr(&writer, "step");
            mpack_write_cstr(&writer, steps[i]);
            mpack_finish_map(&writer);
        }
        mpack_finish_array(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        expect(mpack_writer_destroy(&writer) == mpack_ok, "the document was written");
        *out_size = size;
        return data;
    }

    /* ---- 1: the document the device produced ---- */
    void the_device_document_resolves() {
        static std::uint8_t bytes[4096U];
        const std::size_t size = load_device_document(bytes, sizeof(bytes));
        if (size == 0U) {
            return;
        }
        Document document;
        expect(document.parse(bytes, size) == DocumentVerdict::Ok,
               "the device's document parses");
        const RunPlanResult resolved = resolve_run_plan(document);
        expect(resolved.verdict == PlanVerdict::Ok, "and resolves into a run");
        expect(resolved.plan.chain != nullptr &&
                   std::strcmp(resolved.plan.chain->token, "mcast_rootchild") == 0,
               "the chain is the multicast root-child one");
        expect(resolved.plan.step_count == 3U && resolved.plan.steps[0U] == StepId::W1 &&
                   resolved.plan.steps[1U] == StepId::W2 && resolved.plan.steps[2U] == StepId::W3,
               "and the steps are w1, w2, w3 in order");
        expect(!resolved.plan.from_fallback,
               "and the queue the document carries is what selected them");
        document.reset();
    }

    /* ---- 2: a document that carries its queue ---- */
    void a_queued_document_resolves() {
        const char *steps[] = {"pagecache_write"};
        std::size_t size = 0U;
        char *data = build_43284(steps, 1U, &size);
        Document document;
        expect(document.parse(reinterpret_cast<const std::uint8_t *>(data), size) ==
                   DocumentVerdict::Ok,
               "the 43284 document parses");
        const RunPlanResult resolved = resolve_run_plan(document);
        expect(resolved.verdict == PlanVerdict::Ok, "and resolves");
        expect(resolved.plan.chain != nullptr &&
                   std::strcmp(resolved.plan.chain->token, "umh") == 0,
               "to the umh chain");
        expect(resolved.plan.chain != nullptr && resolved.plan.chain->wired, "which is wired");
        expect(resolved.plan.step_count == 1U && resolved.plan.steps[0U] == StepId::PageCacheWrite,
               "with the single page cache step");
        expect(!resolved.plan.from_fallback,
               "and the queue is what selected it, not the fallback");
        MPACK_FREE(data);
    }

    /* ---- 3: what cannot be resolved is refused by name ---- */
    void refusals_are_named() {
        const char *bad[] = {"w9"};
        std::size_t size = 0U;
        char *bad_data = build_43284(bad, 1U, &size);
        Document bad_doc;
        expect(bad_doc.parse(reinterpret_cast<const std::uint8_t *>(bad_data), size) ==
                   DocumentVerdict::Ok,
               "the document with the unknown step parses");
        const RunPlanResult bad_plan = resolve_run_plan(bad_doc);
        expect(bad_plan.verdict == PlanVerdict::StepSetUnknown, "an unknown step stops the plan");
        expect(std::string_view(plan_verdict_name(bad_plan.verdict)) == std::string_view("unknown-op"),
               "under its own name");
        MPACK_FREE(bad_data);

        const char *partial[] = {"w2"};
        std::size_t partial_size = 0U;
        char *partial_data = build_43284(partial, 1U, &partial_size);
        Document partial_doc;
        expect(partial_doc.parse(reinterpret_cast<const std::uint8_t *>(partial_data), partial_size) ==
                   DocumentVerdict::Ok,
               "the document with the partial step set parses");
        expect(resolve_run_plan(partial_doc).verdict == PlanVerdict::StepSetUnknown,
               "steps that are not a declared set stop the plan");
        MPACK_FREE(partial_data);

        Document empty;
        expect(resolve_run_plan(empty).verdict == PlanVerdict::DocumentInvalid,
               "an unparsed document resolves to nothing");
    }
}  // namespace

int main() {
    std::printf("run plan: the device document\n");
    the_device_document_resolves();
    std::printf("run plan: a queued document\n");
    a_queued_document_resolves();
    std::printf("run plan: refusals\n");
    refusals_are_named();
    if (g_failures != 0U) {
        std::printf("run plan: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("run plan: PASS (device document resolves + queue drives + refusals named)\n");
    return 0;
}
