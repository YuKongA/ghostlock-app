/** @file tests/policy_dispatch_test.cpp
 *  The dispatch chain: first match wins, and nothing else is asked.
 *
 *  The policies here are made up on purpose. The point of the chain is not what a policy decides
 *  but that exactly one of them acts, in the order written, and that the answer comes back
 *  unchanged. A policy that says no must not run; a policy after the first yes must not run; and
 *  when every policy says no the caller must get an unsupported report rather than the last answer
 *  or a default. Those are the three ways a chain like this goes wrong, so all three are asserted.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/route/policy_dispatch.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::route::dispatch_policy;
    using ghostlock::backend::cve_2026_43499::route::RouteResultCode;
    using ghostlock::backend::cve_2026_43499::route::RouteStatus;

    std::size_t g_failures = 0U;
    int g_first_calls = 0;
    int g_second_calls = 0;
    int g_never_calls = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void reset_calls() {
        g_first_calls = 0;
        g_second_calls = 0;
        g_never_calls = 0;
    }

    struct Values final {
        bool first_applies = false;
        bool second_applies = false;
    };
    struct Request final {
        int marker = 0;
    };

    struct First final {
        static bool supported(const Values &v) { return v.first_applies; }
        static RouteStatus run(const Request *) {
            ++g_first_calls;
            return RouteStatus{.code = RouteResultCode::Ok};
        }
    };
    struct Second final {
        static bool supported(const Values &v) { return v.second_applies; }
        static RouteStatus run(const Request *) {
            ++g_second_calls;
            return RouteStatus{.code = RouteResultCode::Unsupported};
        }
    };
    struct Never final {
        static bool supported(const Values &) { return false; }
        static RouteStatus run(const Request *) {
            ++g_never_calls;
            return RouteStatus{.code = RouteResultCode::Unsupported};
        }
    };

    void an_applying_policy_runs_exactly_once_and_its_report_comes_back() {
        reset_calls();
        Values values;
        values.first_applies = true;
        Request request;
        const RouteStatus status = dispatch_policy<First, Never>(values, &request);
        expect(g_first_calls == 1, "the applying policy runs exactly once");
        expect(g_never_calls == 0, "and the one that says no is not asked");
        expect(status.code == RouteResultCode::Ok, "and its report is what the caller gets");
    }

    void a_policy_that_says_no_does_not_run() {
        reset_calls();
        Values values;
        values.second_applies = true;
        Request request;
        static_cast<void>(dispatch_policy<First, Second>(values, &request));
        expect(g_first_calls == 0, "the policy that does not apply never runs");
        expect(g_second_calls == 1, "and the first one that does is the one that runs");
    }

    /* The property this whole file exists for: two policies claiming the same device must not both
     *  act on one write. If the chain kept going after a match, this case would show two runs. */
    void the_first_match_stops_the_chain() {
        reset_calls();
        Values values;
        values.first_applies = true;
        values.second_applies = true;
        Request request;
        const RouteStatus status = dispatch_policy<First, Second, Never>(values, &request);
        expect(g_first_calls == 1, "the first match runs");
        expect(g_second_calls == 0, "and the second is not asked even though it would apply");
        expect(status.code == RouteResultCode::Ok, "and the answer is still the first one's");
    }

    void nothing_applying_is_reported_as_unsupported() {
        reset_calls();
        Values values;
        Request request;
        const RouteStatus status = dispatch_policy<First, Second, Never>(values, &request);
        expect(status.code == RouteResultCode::Unsupported, "no match is reported, not assumed");
        expect(g_first_calls == 0 && g_second_calls == 0 && g_never_calls == 0,
               "and nothing ran to produce that answer");
    }
}  // namespace

int main() {
    std::printf("policy dispatch: one match\n");
    an_applying_policy_runs_exactly_once_and_its_report_comes_back();
    std::printf("policy dispatch: a refusal\n");
    a_policy_that_says_no_does_not_run();
    std::printf("policy dispatch: first match stops the chain\n");
    the_first_match_stops_the_chain();
    std::printf("policy dispatch: no match\n");
    nothing_applying_is_reported_as_unsupported();
    if (g_failures != 0U) {
        std::printf("policy dispatch: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("policy dispatch: PASS (one match, in order, unsupported when none)\n");
    return 0;
}
