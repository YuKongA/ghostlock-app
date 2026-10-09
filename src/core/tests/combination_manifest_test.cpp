/** @file tests/combination_manifest_test.cpp
 *  Bare-run export + pin for the combination tokens.
 *
 *  Prints the manifest as TSV -- token, backend, route, path, steps, terminal, available, doc --
 *  taken from contract::kChainCatalog, which is where the tokens live now, and derives the doc
 *  column by joining the row with a separator as the artifact's own header describes.
 *
 *  Two columns are worth a note, because the artifact that exists on the app side spells them
 *  differently and the difference is not recoverable: it says rootchild where the catalogue says
 *  root_child, and it says w1_w3 where the step catalogue now says selinux_cred_seccomp. Those
 *  were the retired exporter's own spellings, they are not in the tree any more, and inventing
 *  them back would be guessing. This exporter prints the catalogue as it is; the app side aligns
 *  its reader with that in the same batch.
 *
 *  Pattern and targets are the same as the stepset manifest exporter:
 *    make -C src combination-manifest         verify (default path, runs this)
 *    make -C src combination-manifest-regen   print the diff, then write the new pin
 */
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "contract/chain_catalog.hpp"

namespace {
#ifndef COMBINATION_MANIFEST_PIN
#define COMBINATION_MANIFEST_PIN "core/tests/combination-manifest.pin.tsv"
#endif

    using ghostlock::contract::kChainCatalog;
    using ghostlock::contract::kChainCount;

    /** The row joined by the separator the artifact's header names. */
    int print_doc(const ghostlock::contract::ChainSpec &row) {
        const char *route = (row.route == nullptr || row.route[0] == 0) ? "none" : row.route;
        return std::printf("%s \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 %s", row.backend, route, row.step_set,
                           row.terminal);
    }

    int print_export() {
        std::printf("# GhostLock combination-token manifest (ADR-0006 T5 / S4 R6b F4).\n");
        std::printf("# Authoritative native export of contract::kChainCatalog, in catalogue order;\n");
        std::printf("# regenerate with: make -C src combination-manifest-regen\n");
        std::printf("# Columns: token<TAB>backend<TAB>route<TAB>path<TAB>steps<TAB>terminal<TAB>available<TAB>doc\n");
        std::printf("# available is 1 (wired/selectable) or 0 (planned: parses, selection gate rejects).\n");
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const auto &row = kChainCatalog[i];
            /* A backend with no route axis prints the sentinel the artifact header documents,
             *  not an empty field: the artifact said none, and a second spelling of the same fact
             *  is how two readers end up disagreeing about a row that is not in question. */
            const char *route = (row.route == nullptr || row.route[0] == 0) ? "none" : row.route;
            std::printf("%s\t%s\t%s\t%s\t%s\t%s\t%d\t", row.token, row.backend, route, row.path,
                        row.step_set, row.terminal, row.wired ? 1 : 0);
            (void) print_doc(row);
            std::printf("\n");
        }
        return 0;
    }

    int verify() {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const auto &row = kChainCatalog[i];
            if (row.token == nullptr || row.backend == nullptr || row.step_set == nullptr ||
                row.terminal == nullptr) {
                std::printf("combination-manifest: a row with no token, backend, steps or terminal\n");
                return 1;
            }
            for (std::size_t j = i + 1U; j < kChainCount; ++j) {
                if (std::strcmp(row.token, kChainCatalog[j].token) == 0) {
                    std::printf("combination-manifest: token %s appears twice\n", row.token);
                    return 1;
                }
            }
        }
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
    std::printf("combination-manifest: PASS (%zu tokens, exported)\n", kChainCount);
    return 0;
}
