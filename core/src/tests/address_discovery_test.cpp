/** @file tests/address_discovery_test.cpp
 *  The one thing two different discoveries have to agree on: what failure looks like.
 *
 *  Success is easy to agree on and hard to get wrong. Failure is where the contract
 *  earns its place: a discovery that returns false must leave nothing behind, and a
 *  result that claims success while a field is still zero must not pass for complete.
 *  Both cases are checked here because the alternative -- an attack built on an
 *  address that was never found -- fails somewhere else entirely.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "contract/address_discovery.hpp"

namespace {
    using ghostlock::contract::AddressDiscoveryOps;
    using ghostlock::contract::AddressDiscoveryResult;
    using ghostlock::contract::discovery_failed;
    using ghostlock::contract::discovery_is_complete;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    AddressDiscoveryResult complete() {
        AddressDiscoveryResult found;
        found.ok = true;
        found.kaslr_base = 0xffff800000000000U;
        found.init_task = 0xffff800010000000U;
        found.target_task = 0xffff800010000100U;
        found.mm_struct = 0xffff800010000200U;
        return found;
    }

    /** A mechanism that answers the shape, for the concept check below. */
    struct FakeDiscovery final {
        static bool discover(AddressDiscoveryResult &out) noexcept {
            out = complete();
            return true;
        }
    };
    static_assert(AddressDiscoveryOps<FakeDiscovery>,
                  "a mechanism that fills the result answers the shape");

    void failure_is_empty() {
        const AddressDiscoveryResult nothing = discovery_failed();
        expect(!nothing.ok, "failure is not success");
        expect(nothing.kaslr_base == 0U && nothing.init_task == 0U &&
                   nothing.target_task == 0U && nothing.mm_struct == 0U,
               "and carries no address at all, partial or otherwise");
        expect(!discovery_is_complete(nothing), "nor does it pass for complete");
    }

    void success_has_to_be_whole() {
        expect(discovery_is_complete(complete()), "a filled result is complete");
        /* The case the rule exists for: a mechanism that says it succeeded while a
         *  field is still zero. Four fields, four ways to be incomplete. */
        AddressDiscoveryResult no_kaslr = complete();
        no_kaslr.kaslr_base = 0U;
        expect(!discovery_is_complete(no_kaslr), "a success with no kernel base is not one");
        AddressDiscoveryResult no_init = complete();
        no_init.init_task = 0U;
        expect(!discovery_is_complete(no_init), "nor one with no init task");
        AddressDiscoveryResult no_target = complete();
        no_target.target_task = 0U;
        expect(!discovery_is_complete(no_target), "nor one with no target task");
        AddressDiscoveryResult no_mm = complete();
        no_mm.mm_struct = 0U;
        expect(!discovery_is_complete(no_mm), "nor one with no mm_struct");
        AddressDiscoveryResult said_no = complete();
        said_no.ok = false;
        expect(!discovery_is_complete(said_no),
               "and a mechanism that reports failure is believed, addresses or not");
    }
}  // namespace

int main() {
    std::printf("address discovery: failure is empty\n");
    failure_is_empty();
    std::printf("address discovery: success has to be whole\n");
    success_has_to_be_whole();
    if (g_failures != 0U) {
        std::printf("address discovery: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("address discovery: PASS (failure carries nothing, success has to be whole)\n");
    return 0;
}
