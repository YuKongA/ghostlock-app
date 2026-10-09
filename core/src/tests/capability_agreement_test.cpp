/** @file tests/capability_agreement_test.cpp
 *  The declaration and the provision, against each other.
 *
 *  Three rules, from ADR-0012. Every required capability the catalogue declares must be
 *  provisioned, or the declaration is a promise nobody keeps. Every provisioned capability must
 *  be in the catalogue, or a second path exists that nobody registered -- and the exceptions, if
 *  there ever are any, are an empty ledger here so that adding one is a decision rather than an
 *  omission. And the required-or-optional flag is data in the catalogue, so an optional
 *  capability that is missing is allowed and refused by name when something selects it.
 *
 *  The falsification for this test is not inside it: removing a provisioned row from the
 *  backend must turn it red, and that was done by hand when the test was written.
 */
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/capabilities.hpp"
#include "contract/capability_catalog.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::kProvisioned;
    using ghostlock::backend::cve_2026_43499::kProvisionedCount;
    using ghostlock::contract::capability_named;
    using ghostlock::contract::kCapabilityCatalog;
    using ghostlock::contract::kCapabilityCount;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** Capabilities this backend has that the catalogue does not declare. Empty on purpose:
     *  an entry here would be an unregistered capability, which is the shape this arrangement
     *  exists to prevent, so adding one has to be a deliberate edit with a reason. */
    constexpr std::string_view kUnregisteredLedger[] = {};

    /** Required capabilities this backend does NOT have yet, written down rather than left to be
     *  noticed. This is the honest form of a broken promise: the catalogue says every backend
     *  carrying the interface must provide these, this one does not, and the gap is named here
     *  so that implementing one without removing it from this list turns the test red. When the
     *  list is empty the interface is satisfied; until then it says exactly what is missing. */
    constexpr std::string_view kNotYetImplemented[] = {"Selinux", "Seccomp", "arbitrary_write"};

    void every_required_capability_is_provisioned() {
        for (std::size_t i = 0U; i < kCapabilityCount; ++i) {
            const auto &wanted = kCapabilityCatalog[i];
            if (!wanted.required) {
                continue;
            }
            bool found = false;
            for (std::size_t j = 0U; j < kProvisionedCount; ++j) {
                if (kProvisioned[j].name == wanted.name) {
                    found = true;
                    break;
                }
            }
            expect(found, "a required capability is declared and nobody provides it");
        }
    }

    void nothing_is_provisioned_without_being_declared() {
        for (std::size_t i = 0U; i < kProvisionedCount; ++i) {
            const auto &provided = kProvisioned[i];
            const bool declared = capability_named(provided.name) != nullptr;
            bool registered = false;
            for (const auto &allowed : kUnregisteredLedger) {
                if (allowed == provided.name) {
                    registered = true;
                    break;
                }
            }
            expect(declared || registered,
                   "something is provided that the catalogue never declared, and it is not registered");
        }
        expect(sizeof(kUnregisteredLedger) / sizeof(kUnregisteredLedger[0]) == 0U,
               "the unregistered ledger is empty, as it is meant to be");
    }

    void the_flag_is_data_and_optional_gaps_are_named() {
        expect(kCapabilityCount > 0U && kProvisionedCount > 0U, "both sides have rows");
        std::size_t required = 0U;
        std::size_t optional = 0U;
        for (std::size_t i = 0U; i < kCapabilityCount; ++i) {
            if (kCapabilityCatalog[i].required) {
                ++required;
            } else {
                ++optional;
            }
        }
        expect(required > 0U && optional > 0U, "the catalogue carries both kinds, so the flag is data");
        /* An optional capability that this backend does not implement is allowed to be absent
         *  from the provision table, and a row that says implemented = false refuses by name. */
        std::size_t missing = 0U;
        for (std::size_t i = 0U; i < kProvisionedCount; ++i) {
            if (!kProvisioned[i].implemented) {
                ++missing;
            }
        }
        expect(missing > 0U, "this backend honestly says which capabilities it does not have");
    }

    void the_gaps_are_exactly_the_ones_written_down() {
        /* Every required capability that is not implemented must be in the ledger, and the
         *  ledger must not name anything that is implemented: either half being wrong is how a
         *  named gap turns back into a silent one. */
        for (std::size_t i = 0U; i < kProvisionedCount; ++i) {
            const auto &provided = kProvisioned[i];
            const auto *declared = capability_named(provided.name);
            if (declared == nullptr || !declared->required) {
                continue;
            }
            bool listed = false;
            for (const auto &name : kNotYetImplemented) {
                if (name == provided.name) {
                    listed = true;
                    break;
                }
            }
            expect(provided.implemented != listed,
                   "a required capability is either implemented or written down as missing, not both and not neither");
        }
    }
}  // namespace

int main() {
    std::printf("capability agreement: the declaration and the provision\n");
    every_required_capability_is_provisioned();
    std::printf("capability agreement: nothing provided in secret\n");
    nothing_is_provisioned_without_being_declared();
    std::printf("capability agreement: the flag is data\n");
    the_flag_is_data_and_optional_gaps_are_named();
    std::printf("capability agreement: the gaps are written down\n");
    the_gaps_are_exactly_the_ones_written_down();
    if (g_failures != 0U) {
        std::printf("capability agreement: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("capability agreement: PASS (declared is provided, provided is declared)\n");
    return 0;
}
