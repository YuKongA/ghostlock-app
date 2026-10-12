/** @file tests/cve43284_access_params_test.cpp
 *  The second backend's parameter set, which is credentials rather than a route.
 *
 *  What is pinned: the set refuses to be used for anything until credentials are present, because
 *  an all-zero material is not a credential and writing under a key of zeros is the failure this
 *  guards; a position of zero is legitimate so it is not a sentinel and a separate flag says
 *  whether one was given; and the name is this backend's, so the access refuses a set belonging to
 *  the other one instead of misreading it.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/access_params.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::AccessParams;
    using ghostlock::backend::cve_2026_43284::SaMaterial;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void an_empty_set_is_not_usable() {
        const AccessParams params;
        expect(params.name() == std::string_view("cve_2026_43284"),
               "the set names this backend, so the other access refuses it by name");
        expect(!params.usable_for_initialize(), "nothing supplied is not credentials");
        expect(!params.usable_for_access(), "and it is not enough to write with either");
    }

    void credentials_alone_open_the_route_but_do_not_write() {
        AccessParams params;
        params.sa.spi = 0x1234U;
        params.sa.aes_key[0] = 0xAAU;
        params.sa.hmac_key[0] = 0xBBU;
        params.sa.set = true;
        expect(params.usable_for_initialize(), "credentials are enough to bring the route up");
        expect(!params.usable_for_access(), "but a write also needs a position");
        params.offset = 0U;
        params.offset_set = true;
        expect(params.usable_for_access(), "zero is a legitimate position, said so explicitly");
        /* The material is held by value, so clearing the caller's copy does not reach the set:
         *  the access owns what it was given, which is what lets it zero its own copy on the way
         *  out without touching the caller's. */
        const SaMaterial held = params.sa;
        expect(held.set && held.aes_key[0] == 0xAAU && held.hmac_key[0] == 0xBBU,
               "and the material travels by value rather than by reference");
    }
    /** Every value the device rounds proved has to come from outside is a field of this set, and a
     *  borrowed string is a whole pair: a pointer with no length is not an empty path, it is a caller's
     *  mistake, and the set is refused rather than read. */
    void the_found_parameters_travel_in_the_set() {
        AccessParams params;
        params.sa.set = true;
        expect(params.usable_for_initialize(), "credentials alone are still enough");
        expect(!params.has_sequence(), "and no sequence was supplied, so a window starts its own");
        params.sequence = 91U;
        params.sequence_set = true;
        expect(params.has_sequence(), "a supplied sequence is reported, and bring-up adopts it");
        params.carrier = {"carrier", 7U};
        expect(params.usable_for_initialize() && params.consistent(),
               "a path given as a whole pair keeps the set usable");
        params.carrier = {"carrier", 0U};
        expect(!params.consistent() && !params.usable_for_initialize(),
               "a pointer with no length is refused by name");
        params.carrier = {nullptr, 7U};
        expect(!params.consistent(), "and so is a length with no pointer");
        params.carrier = {nullptr, 0U};
        expect(params.consistent(), "an unsupplied path is a whole nothing, which is allowed");
        params.hook_plan_set = true;
        params.hook_plan = ghostlock::backend::cve_2026_43284::hook::device_sentry_plan();
        expect(params.hook_plan.hook_vaddr != 0U, "the hook plan travels in the same set");
        params.kernel_modversions = true;
        params.kernel_module_force_unload = false;
        params.kernel_preempt = true;
        expect(params.kernel_modversions && params.kernel_preempt,
               "as do the module-build facts the precheck cannot probe");
        params.wait_ms = 5000U;
        params.trigger_delay_ms = 1000U;
        params.success_marker = {"/dev/dfm0", 8U};
        params.failure_marker = {"/dev/dfm1", 8U};
        params.module_name = {"ghostlock", 9U};
        expect(params.wait_ms == 5000U && params.success_marker.length == 8U,
               "and the finish's budget and markers");
    }
}  // namespace

int main() {
    std::printf("cve-2026-43284 params: what is not credentials\n");
    an_empty_set_is_not_usable();
    std::printf("cve-2026-43284 params: the parameters the device rounds found\n");
    the_found_parameters_travel_in_the_set();
    std::printf("cve-2026-43284 params: what a write needs\n");
    credentials_alone_open_the_route_but_do_not_write();
    if (g_failures != 0U) {
        std::printf("cve-2026-43284 params: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("cve-2026-43284 params: PASS (credentials, position, whole pairs, sequence, plan, "
                "finish)\n");
    return 0;
}
