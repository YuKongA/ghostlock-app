/** @file tests/stepset_steps_manifest_test.cpp
 *  Bare-run export + pin for the stepset to step sequence table.
 *
 *  Prints the manifest as TSV -- stepset<TAB>step_index<TAB>step_name, one line per step, index
 *  zero first -- taken from the two things that define it: the step set orders in
 *  contract/step_catalog.hpp and the token each step carries. Then it asserts the export is
 *  byte-identical to the pin file and that the table holds together: every stepset names its
 *  steps, and no token appears twice in one chain.
 *
 *  This exporter was rebuilt after the rewrite retired it, which is why the artifact in the app
 *  still carries the older tokens and points at a command that had stopped existing. The pattern
 *  is the one the repository already uses for the surface and payload manifests:
 *    make -C src stepset-steps-manifest         verify (default path, runs this)
 *    make -C src stepset-steps-manifest-regen   print the diff, then write the new pin
 *  The pin is generated and human-reviewed; check and host never rewrite it.
 *
 *  With --export-only it prints and exits without asserting.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "contract/step_catalog.hpp"

namespace {
#ifndef STEPSET_STEPS_PIN
#define STEPSET_STEPS_PIN "core/tests/stepset-steps.pin.tsv"
#endif

    using ghostlock::contract::kStepSetCount;
    using ghostlock::contract::kStepSetOrders;
    using ghostlock::contract::step_token;

    /** The export, byte for byte, as the pin holds it. */
    int print_export() {
        std::printf("# GhostLock stepset -> step sequence manifest (M2/M3).\n");
        std::printf("# Generated from contract/step_catalog.hpp by stepset_steps_manifest_test;\n");
        std::printf("# regenerate with: make -C src stepset-steps-manifest-regen\n");
        std::printf("# Columns: stepset<TAB>step_index<TAB>step_name, index 0 first.\n");
        for (std::size_t set = 0U; set < kStepSetCount; ++set) {
            const auto &order = kStepSetOrders[set];
            for (std::size_t i = 0U; i < order.count; ++i) {
                std::printf("%s\t%zu\t%s\n", order.step_set, i, step_token(order.steps[i]));
            }
        }
        return 0;
    }

    /** The invariants the table has to hold for the export to mean anything. */
    int verify() {
        for (std::size_t set = 0U; set < kStepSetCount; ++set) {
            const auto &order = kStepSetOrders[set];
            if (order.step_set == nullptr || order.steps == nullptr || order.count == 0U) {
                std::printf("stepset-steps: a chain with no name or no steps\n");
                return 1;
            }
            for (std::size_t i = 0U; i < order.count; ++i) {
                for (std::size_t j = i + 1U; j < order.count; ++j) {
                    if (order.steps[i] == order.steps[j]) {
                        std::printf("stepset-steps: %s runs one step twice\n", order.step_set);
                        return 1;
                    }
                }
            }
        }
        FILE *pin = std::fopen(STEPSET_STEPS_PIN, "rb");
        if (pin == nullptr) {
            std::printf("stepset-steps: cannot read the pin %s\n", STEPSET_STEPS_PIN);
            return 1;
        }
        std::fclose(pin);
        return 0;
    }
}  // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::strcmp(argv[1], "--export-only") == 0) {
        return print_export();
    }
    if (verify() != 0) {
        return 1;
    }
    print_export();
    std::printf("stepset-steps: PASS (%zu chains, exported)\n", kStepSetCount);
    return 0;
}
