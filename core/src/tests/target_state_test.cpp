/** @file tests/target_state_test.cpp
 *  Readiness, in the order the facts arrive.
 *
 *  The point of the order is that the first missing fact is the answer: a caller that sees
 *  no-object knows the search is the problem, and a caller that sees no-page knows the search
 *  worked and the preparation did not. Reporting a later cause while an earlier one is missing
 *  sends the reader to the wrong place, so the test pins the order as well as the answers.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43499/session.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::TargetState;
    using ghostlock::backend::cve_2026_43499::write_readiness;
    using ghostlock::backend::cve_2026_43499::write_readiness_name;
    using ghostlock::backend::cve_2026_43499::WriteReadiness;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void an_empty_state_blames_the_search_first() {
        const TargetState nothing{};
        expect(write_readiness(nothing) == WriteReadiness::NoObject,
               "with nothing known, the answer is the object nobody found");
        expect(std::strcmp(write_readiness_name(WriteReadiness::NoObject), "plan-missing") == 0,
               "which is named for what the caller can do about it");
    }

    void each_missing_fact_is_named_in_order() {
        TargetState state{};
        state.mm_struct = 0xffff800000100000ULL;
        expect(write_readiness(state) == WriteReadiness::NoPage,
               "an object without a page is the preparation's failure, not the search's");
        state.page_base = 0xffff800000200000ULL;
        expect(write_readiness(state) == WriteReadiness::NoLanding,
               "a page without landing gear is the route's, and it is reported as such");
        state.fake_task = 0xffff800000300000ULL;
        expect(write_readiness(state) == WriteReadiness::NoLanding,
               "one half of the landing gear is not the gear");
        state.fake_lock = 0xffff800000400000ULL;
        expect(write_readiness(state) == WriteReadiness::Ready, "and with both halves it is ready");
        expect(std::strcmp(write_readiness_name(WriteReadiness::Ready), "ok") == 0,
               "the ready answer is the one the rest of the tree reports for a good state");
        expect(std::strcmp(write_readiness_name(WriteReadiness::NoLanding), "call-failed") == 0,
               "and the landing answer maps to the code callers already act on");
    }
}  // namespace

int main() {
    std::printf("target state: the order the facts arrive in\n");
    an_empty_state_blames_the_search_first();
    std::printf("target state: each missing fact\n");
    each_missing_fact_is_named_in_order();
    if (g_failures != 0U) {
        std::printf("target state: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("target state: PASS (the first missing fact is the answer)\n");
    return 0;
}
