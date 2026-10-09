/** @file tests/run_plan_test.cpp
 *  Resolving a document into the run it asks for: a chain, and a plan script.
 *
 *  The first section feeds the document the device actually produced, and the
 *  answer it now gets is the point of the retirement: that document carries a step
 *  queue, and a document that still carries one is refused on sight rather than
 *  quietly ignored. The rest build their documents here, because the shape a plan
 *  travels in is exactly what these cases are about.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "mpack.h"
#include "profile/run_plan.hpp"
#include "support/sha256.hpp"

#ifndef DEVICE_DOCUMENT_HEX
#define DEVICE_DOCUMENT_HEX "../profile-core/src/test/resources/device-document-fixture.hex"
#endif

namespace {
    using ghostlock::profile::Document;
    using ghostlock::profile::DocumentVerdict;
    using ghostlock::profile::resolve_run_plan;
    using ghostlock::profile::RunPlanResult;
    using ghostlock::profile::RunPlanVerdict;
    using ghostlock::profile::run_plan_verdict_name;

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

    std::string read_device_document() {
        std::FILE *file = std::fopen(DEVICE_DOCUMENT_HEX, "rb");
        if (file == nullptr) {
            return {};
        }
        std::string hex;
        char buffer[4096];
        while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), file) != nullptr) {
            hex += buffer;
        }
        std::fclose(file);
        std::string bytes;
        for (std::size_t i = 0U; i + 1U < hex.size(); i += 2U) {
            const int high = hex_digit(hex[i]);
            const int low = hex_digit(hex[i + 1U]);
            if (high < 0 || low < 0) {
                break;
            }
            bytes.push_back(static_cast<char>((high << 4) | low));
        }
        return bytes;
    }

    std::string hex_of(const std::string &bytes) {
        const ghostlock::support::Sha256Digest digest =
            ghostlock::support::sha256(reinterpret_cast<const std::uint8_t *>(bytes.data()),
                                       bytes.size());
        char hex[ghostlock::support::kSha256HexBytes];
        ghostlock::support::sha256_hex(digest, hex);
        return std::string(hex);
    }

    /** A document declaring one combination, optionally a plan for a terminal, and
     *  optionally the retired step queue the device document still carries. */
    std::string build_document(const char *combination, const std::string &script,
                              const char *terminal, bool with_queue = false) {
        char *data = nullptr;
        std::size_t size = 0U;
        mpack_writer_t writer;
        mpack_writer_init_growable(&writer, &data, &size);
        mpack_start_map(&writer, 3U);
        mpack_write_cstr(&writer, "schema");
        mpack_write_uint(&writer, 3U);
        mpack_write_cstr(&writer, "combination");
        mpack_write_cstr(&writer, combination);
        mpack_write_cstr(&writer, "sections");
        const std::uint32_t sections =
            static_cast<std::uint32_t>((script.empty() ? 0U : 1U) + (with_queue ? 1U : 0U));
        mpack_start_map(&writer, sections);
        if (!script.empty()) {
            const std::string segment =
                std::string("backend.cve_2026_43499.payload.") + terminal + "_plan";
            mpack_write_cstr(&writer, segment.c_str());
            mpack_start_map(&writer, 4U);
            mpack_write_cstr(&writer, "bin");
            mpack_write_bin(&writer, script.data(), static_cast<std::uint32_t>(script.size()));
            mpack_write_cstr(&writer, "sha256");
            mpack_write_cstr(&writer, hex_of(script).c_str());
            mpack_write_cstr(&writer, "size");
            mpack_write_uint(&writer, static_cast<std::uint64_t>(script.size()));
            mpack_write_cstr(&writer, "kind");
            mpack_write_cstr(&writer, "plan");
            mpack_finish_map(&writer);
        }
        if (with_queue) {
            /* The retired queue lived under its backend's own segment, the same shape
             *  the payload family uses: sections[segment] = { queue = [ entries ] }. */
            mpack_write_cstr(&writer, "backend.cve_2026_43499");
            mpack_start_map(&writer, 1U);
            mpack_write_cstr(&writer, "queue");
            mpack_start_array(&writer, 1U);
            mpack_start_map(&writer, 1U);
            mpack_write_cstr(&writer, "step");
            mpack_write_cstr(&writer, "w1");
            mpack_finish_map(&writer);
            mpack_finish_array(&writer);
            mpack_finish_map(&writer);
        }
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        expect(mpack_writer_destroy(&writer) == mpack_ok, "the document was written");
        const std::string bytes(data, size);
        MPACK_FREE(data);
        return bytes;
    }

    RunPlanResult resolve(const std::string &bytes) {
        Document document;
        if (document.parse(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()) !=
            DocumentVerdict::Ok) {
            RunPlanResult failed;
            failed.verdict = RunPlanVerdict::DocumentInvalid;
            return failed;
        }
        return resolve_run_plan(document);
    }

    void the_device_document_is_refused_for_its_queue() {
        const std::string bytes = read_device_document();
        expect(!bytes.empty(), "the device document was read");
        const RunPlanResult resolved = resolve(bytes);
        expect(resolved.verdict == RunPlanVerdict::QueueRemoved,
               "a document that still carries a step queue is refused on sight");
        expect(std::string_view(run_plan_verdict_name(resolved.verdict)) ==
                   std::string_view("queue-removed"),
               "under the name the retirement gives it");
        expect(resolved.plan.chain == nullptr && resolved.plan.plan.script == nullptr,
               "and nothing at all is handed over");
    }

    void a_token_document_resolves() {
        const std::string script = "-- the plan\nreturn 1\n";
        const RunPlanResult resolved =
            resolve(build_document("mcast_rootchild", script, "root_child"));
        expect(resolved.verdict == RunPlanVerdict::Ok, "a token document resolves");
        expect(resolved.plan.chain != nullptr &&
                   std::strcmp(resolved.plan.chain->token, "mcast_rootchild") == 0,
               "and the chain is the one the token names");
        expect(resolved.plan.chain != nullptr &&
                   std::strcmp(resolved.plan.chain->backend, "cve_2026_43499") == 0 &&
                   std::strcmp(resolved.plan.chain->route, "multicast_waiter") == 0,
               "with its backend and route derived from that token, not declared again");
        expect(resolved.plan.plan.size == script.size() && resolved.plan.plan.script != nullptr,
               "and the plan script comes with it");
    }

    void the_selection_and_the_plan() {
        const std::string script = "-- the plan\n";
        expect(resolve(build_document("not_a_combination", script, "root_child")).verdict ==
                   RunPlanVerdict::ChainUnknown,
               "a token no chain names is refused");
        expect(std::string_view(run_plan_verdict_name(RunPlanVerdict::ChainUnknown)) ==
                   std::string_view("route-not-available"),
               "under the existing name, with no new code for it");
        expect(resolve(build_document("mcast_umh", script, "umh_forward")).verdict ==
                   RunPlanVerdict::ChainNotWired,
               "a chain this build does not wire is refused");
        expect(resolve(build_document("mcast_rootchild", "", "root_child")).verdict ==
                   RunPlanVerdict::PlanMissing,
               "a document that carries no plan is refused as plan-missing");
        const std::string over(64U * 1024U + 1U, 'x');
        expect(resolve(build_document("mcast_rootchild", over, "root_child")).verdict ==
                   RunPlanVerdict::PlanTooLarge,
               "and one whose plan is over the ceiling as budget-exceeded");
        expect(resolve(build_document("mcast_rootchild", script, "umh_forward")).verdict ==
                   RunPlanVerdict::PlanMissing,
               "a plan named for another terminal is not this chain's plan");
        expect(resolve(build_document("mcast_rootchild", script, "root_child", true)).verdict ==
                   RunPlanVerdict::QueueRemoved,
               "and a queue beside a plan is still refused, not preferred");
    }
}  // namespace

int main() {
    std::printf("run plan: the device document\n");
    the_device_document_is_refused_for_its_queue();
    std::printf("run plan: a token document\n");
    a_token_document_resolves();
    std::printf("run plan: what a document can get wrong\n");
    the_selection_and_the_plan();
    if (g_failures != 0U) {
        std::printf("run plan: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("run plan: PASS (token resolves + queue refused + plan delivered)\n");
    return 0;
}
