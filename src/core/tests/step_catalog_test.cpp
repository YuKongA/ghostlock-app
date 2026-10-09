/** @file tests/step_catalog_test.cpp
 *  The step vocabulary and the declared orders.
 *
 *  Sections: 1. the tokens and effects are the declared ones and are unique;
 *  2. each step set's order is the design's order, with index zero first;
 *  3. every step set the component catalogue offers is covered by an order, and
 *  the two tables agree on which sets exist.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "contract/kind.hpp"
#include "contract/step_catalog.hpp"

namespace {
    using ghostlock::contract::ComponentKind;
    using ghostlock::contract::kComponentCount;
    using ghostlock::contract::kComponents;
    using ghostlock::contract::kStepCatalog;
    using ghostlock::contract::kStepCount;
    using ghostlock::contract::kStepIdCount;
    using ghostlock::contract::kStepSetDetermined;
    using ghostlock::contract::kStepSetOrders;
    using ghostlock::contract::StepEffect;
    using ghostlock::contract::StepId;
    using ghostlock::contract::StepSetOrder;
    using ghostlock::contract::step_effect_of;
    using ghostlock::contract::step_set_order;
    using ghostlock::contract::step_token;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /* ---- 1: the vocabulary ---- */
    void the_vocabulary_holds() {
        expect(kStepIdCount == 5U, "five step ids are declared");
        expect(kStepCount == kStepIdCount, "every id has a catalogue row");
        expect(std::strcmp(step_token(StepId::DisenforceSelinux), "disenforce_selinux") == 0 &&
                   std::strcmp(step_token(StepId::CredentialForgery), "credential_forgery") == 0 &&
                   std::strcmp(step_token(StepId::SeccompBypass), "seccomp_bypass") == 0,
               "the three window steps keep their names");
        expect(std::strcmp(step_token(StepId::PageCacheWrite), "pagecache_write") == 0,
               "the page cache step keeps its name");
        expect(step_effect_of(StepId::DisenforceSelinux) == StepEffect::Selinux &&
                   step_effect_of(StepId::CredentialForgery) == StepEffect::Credentials &&
                   step_effect_of(StepId::SeccompBypass) == StepEffect::Seccomp,
               "each window step keeps its side-effect domain");
        for (std::size_t i = 0U; i < kStepCount; ++i) {
            for (std::size_t j = i + 1U; j < kStepCount; ++j) {
                expect(std::strcmp(kStepCatalog[i].token, kStepCatalog[j].token) != 0,
                       "no two steps share a token");
            }
        }
    }

    /* ---- 2: the orders ---- */
    void the_orders_hold() {
        const StepSetOrder *full_chain = step_set_order(std::string_view("selinux_cred_seccomp"));
        expect(full_chain != nullptr && full_chain->count == 3U, "the full chain is three steps");
        expect(full_chain != nullptr && full_chain->steps[0U] == StepId::DisenforceSelinux &&
                   full_chain->steps[1U] == StepId::CredentialForgery && full_chain->steps[2U] == StepId::SeccompBypass,
               "and runs them in that order");
        const StepSetOrder *cred_chain = step_set_order(std::string_view("selinux_cred"));
        expect(cred_chain != nullptr && cred_chain->count == 2U, "the credential chain is two steps");
        expect(cred_chain != nullptr && cred_chain->steps[0U] == StepId::DisenforceSelinux &&
                   cred_chain->steps[1U] == StepId::CredentialForgery,
               "and stops before seccomp");
        const StepSetOrder *pagecache = step_set_order(std::string_view("pagecache"));
        expect(pagecache != nullptr && pagecache->count == 1U &&
                   pagecache->steps[0U] == StepId::PageCacheWrite,
               "a page cache write is one step");
        expect(step_set_order(std::string_view("w1w4")) == nullptr,
               "an undeclared step set has no order");
        expect(step_set_order(std::string_view("")) == nullptr, "an empty name has no order");
    }

    /* ---- 3: the two tables cover the same sets ---- */
    void the_tables_agree() {
        std::size_t offered = 0U;
        for (std::size_t i = 0U; i < kComponentCount; ++i) {
            if (kComponents[i].kind != ComponentKind::Stepset) {
                continue;
            }
            ++offered;
            const StepSetOrder *order = step_set_order(kComponents[i].token);
            expect(order != nullptr, "every offered step set has an order");
            expect(order != nullptr && order->count != 0U, "and that order is not empty");
            for (std::size_t j = 0U; order != nullptr && j < order->count; ++j) {
                expect(order->steps[j] != StepId::Unknown, "and names real steps");
                expect(std::strcmp(step_token(order->steps[j]), "unknown") != 0,
                       "a step in an order has a declared token");
            }
        }
        expect(offered > 0U, "the catalogue offers step sets at all");
        expect(offered == kStepSetDetermined(), "the offered count is the declared one");
    }
}  // namespace

int main() {
    std::printf("steps: vocabulary\n");
    the_vocabulary_holds();
    std::printf("steps: orders\n");
    the_orders_hold();
    std::printf("steps: coverage\n");
    the_tables_agree();
    if (g_failures != 0U) {
        std::printf("steps: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("steps: PASS (tokens unique + orders declared + offered sets covered)\n");
    return 0;
}
