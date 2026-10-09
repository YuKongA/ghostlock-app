/** @file tests/plan_delivery_test.cpp
 *  The plan a document delivers: the family's checks plus this reader's two.
 *
 *  Documents are built here rather than borrowed, so the reader is measured
 *  against the wire shape it will actually meet. The cases are the ones the
 *  design names: a plan that is there, one that is absent, one too large, one
 *  whose digest does not match the bytes, one named for another terminal, and the
 *  two boundaries -- exactly the ceiling, and one byte over it.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "profile/glkv3_document.hpp"
#include "profile/plan_delivery.hpp"
#include "support/sha256.hpp"

#include "mpack.h"

namespace {
    using ghostlock::profile::Document;
    using ghostlock::profile::kPlanSuffix;
    using ghostlock::profile::PlanDelivery;
    using ghostlock::profile::plan_verdict_name;
    using ghostlock::profile::PlanVerdict;
    using ghostlock::profile::read_plan_delivery;

    constexpr std::size_t kCeiling = 64U * 1024U;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    std::string hex_of(const std::string &bytes) {
        const ghostlock::support::Sha256Digest digest =
            ghostlock::support::sha256(reinterpret_cast<const std::uint8_t *>(bytes.data()),
                                       bytes.size());
        char hex[ghostlock::support::kSha256HexBytes];
        ghostlock::support::sha256_hex(digest, hex);
        return std::string(hex);
    }

    /** One document carrying one payload named <terminal>_plan (or a given name). */
    std::string build_document(const std::string &name, const std::string &script,
                              const std::string &digest, bool with_kind = true) {
        char *data = nullptr;
        std::size_t size = 0U;
        mpack_writer_t writer;
        mpack_writer_init_growable(&writer, &data, &size);
        /* The wire keeps segments in one flat map at the root, and the payload
         * family is a map inside that map: sections[segment] = { four keys }. */
        const std::string segment = std::string("backend.cve_2026_43499.payload.") + name;
        mpack_start_map(&writer, 2U);
        mpack_write_cstr(&writer, "schema");
        mpack_write_uint(&writer, 3U);
        mpack_write_cstr(&writer, "sections");
        mpack_start_map(&writer, 1U);
        mpack_write_cstr(&writer, segment.c_str());
        mpack_start_map(&writer, with_kind ? 4U : 3U);
        mpack_write_cstr(&writer, "bin");
        mpack_write_bin(&writer, script.data(), static_cast<std::uint32_t>(script.size()));
        mpack_write_cstr(&writer, "sha256");
        mpack_write_cstr(&writer, digest.c_str());
        mpack_write_cstr(&writer, "size");
        mpack_write_uint(&writer, static_cast<std::uint64_t>(script.size()));
        if (with_kind) {
            mpack_write_cstr(&writer, "kind");
            mpack_write_cstr(&writer, "plan");
        }
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        expect(mpack_writer_destroy(&writer) == mpack_ok, "the document was written");
        const std::string document(data, size);
        MPACK_FREE(data);
        return document;
    }

    PlanVerdict read(const std::string &document, PlanDelivery &out,
                     std::string_view terminal = std::string_view("root_child")) {
        Document parsed;
        if (parsed.parse(reinterpret_cast<const std::uint8_t *>(document.data()), document.size()) !=
            ghostlock::profile::DocumentVerdict::Ok) {
            return PlanVerdict::DocumentInvalid;
        }
        return read_plan_delivery(parsed, std::string_view("cve_2026_43499"), terminal, out);
    }

    void a_plan_that_is_there() {
        const std::string script = "-- the plan\nreturn 1\n";
        PlanDelivery out;
        const std::string document =
            build_document(std::string("root_child") + kPlanSuffix, script, hex_of(script));
        expect(read(document, out) == PlanVerdict::Ok, "a plan that is there is delivered");
        expect(out.size == script.size(), "with its length");
        expect(out.script != nullptr &&
                   std::string_view(reinterpret_cast<const char *>(out.script), out.size) == script,
               "and its bytes exactly as the document carried them");
        expect(std::strlen(out.declared_sha256) == 64U, "and the digest it declared");
        /* The boundary the design names: exactly the ceiling is allowed. */
        const std::string at_ceiling(kCeiling, 'x');
        const std::string big =
            build_document(std::string("root_child") + kPlanSuffix, at_ceiling,
                           hex_of(at_ceiling));
        PlanDelivery large;
        expect(read(big, large) == PlanVerdict::Ok, "exactly the ceiling is allowed");
        expect(large.size == kCeiling, "and it is the whole script");
    }

    void what_is_not_a_plan() {
        PlanDelivery out;
        /* Absent. */
        const std::string empty_document = build_document("root_child", "", hex_of(""));
        expect(read(empty_document, out) == PlanVerdict::PlanMissing,
               "a document that carries no plan delivers none");
        /* One byte over the ceiling. */
        const std::string over(kCeiling + 1U, 'y');
        const std::string too_big =
            build_document(std::string("root_child") + kPlanSuffix, over, hex_of(over));
        expect(read(too_big, out) == PlanVerdict::BudgetExceeded,
               "one byte over the ceiling is refused as too large");
        expect(std::string_view(plan_verdict_name(PlanVerdict::BudgetExceeded)) ==
                   std::string_view("budget-exceeded"),
               "by name");
        /* A digest that does not match the bytes. */
        const std::string script = "-- the plan\n";
        const std::string wrong =
            build_document(std::string("root_child") + kPlanSuffix, script, hex_of("other"));
        expect(read(wrong, out) == PlanVerdict::PlanMissing,
               "a plan that is not what it declared itself to be is not a plan");
        expect(std::string_view(plan_verdict_name(PlanVerdict::PlanMissing)) ==
                   std::string_view("plan-missing"),
               "and carries the single name by design");
        /* Named for another terminal. */
        const std::string elsewhere =
            build_document(std::string("umh_forward") + kPlanSuffix, script, hex_of(script));
        expect(read(elsewhere, out) == PlanVerdict::PlanMissing,
               "a plan for another terminal is not this terminal's plan");
        /* The name rule the family enforces: a bare terminal is not admissible. */
        const std::string bare = build_document("root_child", script, hex_of(script));
        expect(read(bare, out) == PlanVerdict::PlanMissing,
               "a payload under a bare terminal name is not a plan");
        /* A payload whose kind was never declared. */
        const std::string no_kind =
            build_document(std::string("root_child") + kPlanSuffix, script, hex_of(script), false);
        expect(read(no_kind, out) == PlanVerdict::PlanMissing,
               "a payload without its kind declared is not a plan either");
    }
}  // namespace

int main() {
    std::printf("plan delivery: a plan that is there\n");
    a_plan_that_is_there();
    std::printf("plan delivery: what is not a plan\n");
    what_is_not_a_plan();
    if (g_failures != 0U) {
        std::printf("plan delivery: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("plan delivery: PASS (delivered + ceiling + digest + naming)\n");
    return 0;
}
