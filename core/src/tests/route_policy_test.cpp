/** @file tests/route_policy_test.cpp
 *  Two policies and the defaults they did and did not redeclare.
 *
 *  The point of the defaults base is that adding a capability is one constant that every
 *  existing policy inherits, and that a policy needing it redeclares without touching the
 *  others. The test pins that with two policies that differ in exactly one capability, and it
 *  pins the concept by asking it about both.
 */
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/route/route_policy.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::route::RoutePolicy;
    using ghostlock::backend::cve_2026_43499::route::RoutePolicyDefaults;
    using ghostlock::backend::cve_2026_43499::route::RouteReport;
    using ghostlock::backend::cve_2026_43499::route::RouteResultCode;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A device profile, as far as a policy can see one. */
    struct FakeProfile final {
        bool two_words_supported = false;
    };

    /** A policy that takes the defaults as they are. */
    struct PlainPolicy final : RoutePolicyDefaults {
        static constexpr std::string_view kind = "select_stack";
        [[nodiscard]] static bool supported(const FakeProfile &) noexcept { return true; }
        [[nodiscard]] static RouteReport run() noexcept { return RouteReport{}; }
    };

    /** The same, with one capability redeclared and a condition on the device. */
    struct TwoWordPolicy final : RoutePolicyDefaults {
        static constexpr bool needs_two_words = true;
        static constexpr std::string_view kind = "multicast_waiter";
        [[nodiscard]] static bool supported(const FakeProfile &profile) noexcept {
            return profile.two_words_supported;
        }
        [[nodiscard]] static RouteReport run() noexcept { return RouteReport{}; }
    };

    void what_the_defaults_are_for() {
        expect(RoutePolicyDefaults::multicast == false, "the base has every capability off");
        expect(PlainPolicy::needs_two_words == false, "so a policy that says nothing inherits off");
        expect(TwoWordPolicy::needs_two_words, "and one that needs it redeclares it");
        expect(TwoWordPolicy::w3_exact_target == false,
               "while the capabilities it did not touch stay inherited");
        expect(PlainPolicy::kind == std::string_view("select_stack"), "a policy names its kind");
        expect(TwoWordPolicy::kind == std::string_view("multicast_waiter"), "and so does the other");
    }

    void the_concept_asks_the_right_questions() {
        expect(RoutePolicy<PlainPolicy, FakeProfile>, "a policy with a kind and a decision satisfies it");
        expect(RoutePolicy<TwoWordPolicy, FakeProfile>, "including one whose decision depends on the device");
        FakeProfile weak;
        expect(!TwoWordPolicy::supported(weak), "which says no when the device is not ready");
        FakeProfile strong;
        strong.two_words_supported = true;
        expect(TwoWordPolicy::supported(strong), "and yes when it is");
        expect(PlainPolicy::supported(weak), "while a policy without conditions always agrees");
        RouteReport report{};
        expect(report.code == RouteResultCode::Retryable, "and a default report is a retryable one");
    }
}  // namespace

int main() {
    std::printf("route policy: the defaults\n");
    what_the_defaults_are_for();
    std::printf("route policy: the concept\n");
    the_concept_asks_the_right_questions();
    if (g_failures != 0U) {
        std::printf("route policy: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("route policy: PASS (capabilities are compile-time and inherited)\n");
    return 0;
}
