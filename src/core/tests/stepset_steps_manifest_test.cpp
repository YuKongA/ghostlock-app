/* Host test / exporter for the stepset -> step sequence manifest (M3 blocker).
 *
 * The per-step-set step ORDER is the C++ compile-time definition, never a
 * hand-written table:
 *   - contract/step_catalog.hpp kStepSetAliases is the alias authority for every
 *     step set (each alias carries its ordered StepId span);
 *   - backend/cve_2026_43499/steps.hpp W1W3Steps::kSteps / W1W2Steps::kSteps are
 *     the executor orders, bound to that alias table by static_assert, so the
 *     executor and the export cannot drift.
 *
 * Export shape (index 0 is the first executed step):
 *   stepset<TAB>step_index<TAB>step_name
 *
 * "--write" regenerates the app test resource and the profile-core runtime
 * resource from one text (so the two cannot drift); the bare run verifies both
 * committed copies byte-for-byte and pins the exported rows verbatim, so a
 * reordered, renamed or dropped step fails the gate.
 * Regenerate with: make -C src stepset-steps-manifest */

#include "backend/cve_2026_43499/steps.hpp"
#include "contract/step_catalog.hpp"
#include "pipeline/component_catalog.hpp"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {
    const char *kAppResourceCandidates[] = {
        "app/src/test/resources/stepset-steps.tsv",
        "../app/src/test/resources/stepset-steps.tsv",
        "../../app/src/test/resources/stepset-steps.tsv",
    };
    const char *kRuntimeResourceCandidates[] = {
        "profile-core/src/main/resources/stepset-steps.tsv",
        "../profile-core/src/main/resources/stepset-steps.tsv",
        "../../profile-core/src/main/resources/stepset-steps.tsv",
    };

    /* Pinned expected rows: each step set in alias-table order, the steps in
     * execution order (43499 orders are also compile-time bound to the alias
     * table by the static_asserts in steps.hpp). */
    constexpr std::string_view kExpectedRows =
        "w1_w3\t0\tw1\n"
        "w1_w3\t1\tw2\n"
        "w1_w3\t2\tw3\n"
        "w1_w2\t0\tw1\n"
        "w1_w2\t1\tw2\n"
        "pagecache_write\t0\tpagecache_write\n";

    std::string manifest_header() {
        return "# GhostLock stepset -> step sequence manifest (M2/M3).\n"
               "# Authoritative native export of contract/step_catalog.hpp\n"
               "# (kStepSetAliases) plus the executor orders in\n"
               "# backend/cve_2026_43499/steps.hpp; regenerate with:\n"
               "#   make -C src stepset-steps-manifest\n"
               "# Columns: stepset<TAB>step_index<TAB>step_name\n"
               "# step_index 0 is the first executed step; step_name is the HOCON\n"
               "# step token M3 writes into the profile queue.\n";
    }

    /* Built from the compile-time authority; an unmapped name or a missing step
     * spec fails closed instead of exporting a fallback. */
    bool build_text(std::string &out) {
        out = manifest_header();
        for (const ghostlock::contract::StepSetAlias &alias :
             ghostlock::contract::kStepSetAliases) {
            const std::string_view name = ghostlock::pipeline::stepset_name(alias.kind);
            if (name == "unknown") {
                std::fprintf(stderr,
                             "stepset_steps_manifest_test: guard: unmapped stepset name\n");
                return false;
            }
            for (std::size_t i = 0; i < alias.steps.size(); ++i) {
                const ghostlock::contract::StepSpec *spec =
                        ghostlock::contract::step_spec(alias.steps[i]);
                if (spec == nullptr || spec->token == "unknown") {
                    std::fprintf(stderr,
                                 "stepset_steps_manifest_test: guard: unmapped step id in\n");
                    return false;
                }
                out += std::string(name) + "\t" + std::to_string(i) + "\t" +
                       std::string(spec->token) + "\n";
            }
        }
        return true;
    }

    bool read_file(const std::string &path, std::string &out) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        out = buffer.str();
        return true;
    }

    bool write_file(const std::string &path, const std::string &text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << text;
        return file.good();
    }

    int write_group(const char *const *candidates, std::size_t count,
                    const std::string &text) {
        int written = 0;
        for (std::size_t i = 0; i < count; i++) {
            const std::string path = candidates[i];
            const std::size_t slash = path.find_last_of('/');
            const std::string dir =
                    slash == std::string::npos ? "." : path.substr(0, slash);
            std::ifstream probe(dir);
            if (!probe.good()) continue;
            if (!write_file(path, text)) return -1;
            std::printf("stepset_steps_manifest_test: wrote %s\n", path.c_str());
            written++;
        }
        return written;
    }

    std::size_t first_differing_line(const std::string &a, const std::string &b) {
        std::size_t line = 1u;
        std::size_t i = 0u;
        std::size_t j = 0u;
        while (i < a.size() && j < b.size()) {
            if (a[i] != b[j]) return line;
            if (a[i] == '\n') line++;
            i++;
            j++;
        }
        return (i != a.size() || j != b.size()) ? line : 0u;
    }

    int check_group(const char *const *candidates, std::size_t count,
                    const char *label, const std::string &expected) {
        const char *found = nullptr;
        std::string text;
        int hits = 0;
        for (std::size_t i = 0; i < count; i++) {
            std::string candidate;
            if (!read_file(candidates[i], candidate)) continue;
            hits++;
            if (found == nullptr) {
                found = candidates[i];
                text = std::move(candidate);
            }
        }
        if (hits != 1) {
            std::fprintf(stderr,
                         "stepset_steps_manifest_test: expected exactly one %s manifest "
                         "(%d found)\n",
                         label, hits);
            return 0;
        }
        if (text != expected) {
            std::fprintf(stderr,
                         "stepset_steps_manifest_test: %s manifest drift (%s), first "
                         "differing line %zu\n",
                         label, found, first_differing_line(text, expected));
            return -1;
        }
        std::printf("stepset_steps_manifest_test: %s ok (%s)\n", label, found);
        return 1;
    }
} // namespace

int main(int argc, char **argv) {
    bool write = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--write") write = true;
    }

    std::string text;
    if (!build_text(text)) return 1;

    /* The pin: the exported text must equal the frozen rows verbatim, so a
     * reordered alias table or a renamed step fails here instead of silently
     * changing what M3 migrates into the 68 assets. */
    const std::string expected = manifest_header() + std::string(kExpectedRows);
    if (text != expected) {
        std::fprintf(stderr,
                     "stepset_steps_manifest_test: exported rows drifted from the pin, "
                     "first differing line %zu\n",
                     first_differing_line(text, expected));
        return 1;
    }

    if (write) {
        const int app = write_group(kAppResourceCandidates,
                                    std::size(kAppResourceCandidates), text);
        const int runtime = write_group(kRuntimeResourceCandidates,
                                        std::size(kRuntimeResourceCandidates), text);
        if (app != 1 || runtime != 1) {
            std::fprintf(stderr,
                         "stepset_steps_manifest_test: expected one writable app and "
                         "runtime resource (app=%d runtime=%d)\n",
                         app, runtime);
            return 1;
        }
        return 0;
    }

    const int app = check_group(kAppResourceCandidates,
                                std::size(kAppResourceCandidates), "app", text);
    if (app <= 0) return 1;
    const int runtime = check_group(kRuntimeResourceCandidates,
                                    std::size(kRuntimeResourceCandidates), "runtime", text);
    if (runtime <= 0) return 1;
    std::printf("stepset_steps_manifest_test: ok (%zu rows)\n",
                std::size(ghostlock::contract::kStepSetAliases));
    return 0;
}
