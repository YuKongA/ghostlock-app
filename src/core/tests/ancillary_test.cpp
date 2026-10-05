/* Host test for the neutral ancillary mechanism: the stage vocabulary, the
 * policy contract, the caller-injected registry, gate and context. The vendor
 * vr.ko behaviors now live in platform::vivo and are checked in
 * platform_vivo_test.cpp. No session is constructed and no behavior body runs. */

#include "ancillary/controller.hpp"

#include <cassert>
#include <concepts>
#include <cstdio>
#include <tuple>

using namespace ghostlock;

namespace {
    struct EmptyMiddleware final {};
    struct EmptyContext final {};

    int g_applied = 0;

    /* A behavior that opts in: it proves the injected gate, not the mechanism,
     * decides whether a policy runs. */
    struct CountingPolicy : ancillary::AncillaryPolicyDefaults {
        template <class Middleware>
        static Status apply(ancillary::AncillaryStage, session::CoreSession &,
                            ancillary::AncillaryOps &,
                            const EmptyContext &) noexcept {
            ++g_applied;
            return true;
        }
    };

    struct OtherCountingPolicy : ancillary::AncillaryPolicyDefaults {
        template <class Middleware>
        static Status apply(ancillary::AncillaryStage, session::CoreSession &,
                            ancillary::AncillaryOps &,
                            const EmptyContext &) noexcept {
            return true;
        }
    };
} // namespace

int main() {
    using namespace ancillary;

    /* The behavior contract is one apply entry; it says nothing about kind or
     * about how a behavior is gated. */
    static_assert(AncillaryPolicyFor<CountingPolicy, EmptyMiddleware, EmptyContext>);

    /* Traversal over a caller-supplied registry. */
    using TestPolicies = std::tuple<CountingPolicy, OtherCountingPolicy>;
    int visited = 0;
    for_each_ancillary_policy<TestPolicies>([&]<class P>() {
        ++visited;
        (void)sizeof(P);
    });
    assert(visited == 2);

    /* The controller consults the injected gate and dispatches only the policies
     * it accepts. The header only needs a session reference to reach behaviors
     * that ignore it, so bind a never-dereferenced one. */
    alignas(void *) unsigned char session_storage[sizeof(void *)]{};
    auto &fake_session = *reinterpret_cast<session::CoreSession *>(session_storage);
    using CountingPolicies = std::tuple<CountingPolicy>;
    AncillaryOps ops{};
    const EmptyContext context{};
    const auto never = []<class P>() { return false; };
    const auto always = []<class P>() { return true; };

    g_applied = 0;
    assert(AncillaryController<CountingPolicies>::apply<EmptyMiddleware>(
            AncillaryStage::PreSpawn, fake_session, ops, never, context));
    assert(g_applied == 0);
    assert(AncillaryController<CountingPolicies>::apply<EmptyMiddleware>(
            AncillaryStage::PreSpawn, fake_session, ops, always, context));
    assert(g_applied == 1);

    puts("ancillary_test: ok");
    return 0;
}
