/* Host test / exporter for the combination-token artifacts (S4 R6b / F1 / F4).
 *
 * contract::kCombinationCatalog is the single authority for the user-visible
 * selection tokens. This unit exports three artifacts from it:
 *
 *   1. combination-manifest.tsv  (8 columns, TWO copies: the app test resource
 *      used by the native/Kotlin agreement tests and the profile-core runtime
 *      resource the Kotlin catalogue parses at runtime);
 *   2. the same bytes are written to both copies from one --write run, so the
 *      runtime consumer can never drift from the agreement copy;
 *   3. combination-resolve-vectors.tsv (test-only resolve vectors, ONE copy).
 *
 * Manifest columns, in catalogue order (the UI dropdown order and the App
 * default depend on it, so the file is deliberately NOT sorted):
 *
 *   token<TAB>backend<TAB>route<TAB>path<TAB>steps<TAB>terminal<TAB>available<TAB>doc
 *
 * where route is a kRouteCatalog token or "none" for a backend without a route
 * axis (RouteKind::None), path is the row's PathKind (F1 decomposition), steps
 * and terminal are the derived execution facts, available is 1 (wired) or 0
 * (planned: parses, selection gate rejects) and doc is the dropdown summary
 * carried as catalogue data.
 *
 * Resolve-vector columns:
 *
 *   backend<TAB>input<TAB>expected_token|--
 *
 * The expected column is computed by contract::combination_resolve, which is
 * EXACT (case-sensitive, no trimming); "--" marks a pair that does not resolve.
 * The inputs cover the canonical rows, cross-backend pairs, case and whitespace
 * variants, the empty string, unknown tokens, planned items and a backend with
 * no catalogue rows. Inputs are escaped (\s = space, \t = tab, \\ = backslash)
 * so trailing blanks are visible and diff-stable.
 *
 * "--write" regenerates the artifacts; the bare run asserts every committed
 * copy equals the catalogue export AND the source invariants below. Kotlin's
 * CombinationTokenAgreementTest / CombinationResolveVectorsTest assert the same
 * resources from the other side. */

#include "contract/identity.hpp"
#include "contract/model.hpp"
#include "pipeline/component_catalog.hpp"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    /* Destination groups. Each group must resolve to exactly one path from the
     * current working directory (src/ under make): the manifest is generated
     * into two runtime-consumer locations and the vectors into one test-only
     * location. */
    const char *kAppResourceCandidates[] = {
        "app/src/test/resources/combination-manifest.tsv",
        "../app/src/test/resources/combination-manifest.tsv",
        "../../app/src/test/resources/combination-manifest.tsv",
    };
    const char *kRuntimeResourceCandidates[] = {
        "profile-core/src/main/resources/combination-manifest.tsv",
        "../profile-core/src/main/resources/combination-manifest.tsv",
        "../../profile-core/src/main/resources/combination-manifest.tsv",
    };
    const char *kVectorsResourceCandidates[] = {
        "app/src/test/resources/combination-resolve-vectors.tsv",
        "../app/src/test/resources/combination-resolve-vectors.tsv",
        "../../app/src/test/resources/combination-resolve-vectors.tsv",
    };

    std::string manifest_header() {
        return "# GhostLock combination-token manifest (ADR-0006 T5 / S4 R6b F4).\n"
               "# Authoritative native export of contract::kCombinationCatalog, in catalogue order;\n"
               "# regenerate with: make -C src combination-manifest\n"
               "# Columns: token<TAB>backend<TAB>route<TAB>path<TAB>steps<TAB>terminal<TAB>available<TAB>doc\n"
               "# route is a kRouteCatalog token or none (backend without a route axis);\n"
               "# available is 1 (wired/selectable) or 0 (planned: parses, selection gate rejects);\n"
               "# doc is the dropdown summary, derived from this row (columns joined by ' \u00b7 ').\n";
    }

    std::string vectors_header() {
        return "# GhostLock combination-token resolve vectors (native SSOT, ADR-0006 T5 / S4 R6b F4).\n"
               "# Authoritative native export; regenerate with: make -C src combination-manifest\n"
               "# Columns: backend<TAB>input<TAB>expected_token|--\n"
               "# input escapes: \\s = space, \\t = tab, \\\\ = backslash; the input column never\n"
               "# contains a literal TAB.\n"
               "# expected_token is computed by contract::combination_resolve, which is EXACT\n"
               "# (case-sensitive, no trimming); \"--\" means the pair does not resolve.\n"
               "# The column is the token, NOT its availability: planned rows resolve here and are\n"
               "# reported with available=0 in combination-manifest.tsv.\n"
               "# Single copy on purpose: this is a test-only agreement vector with no runtime\n"
               "# consumer, so unlike combination-manifest.tsv it is NOT duplicated into\n"
               "# profile-core (do not add a copy).\n";
    }

    std::string row_for(const ghostlock::contract::CombinationSpec &spec) {
        std::ostringstream line;
        line << spec.token << '\t'
             << ghostlock::contract::backend_token_name(spec.backend) << '\t'
             << ghostlock::contract::route_name(spec.route) << '\t'
             << ghostlock::contract::path_name(spec.path) << '\t'
             << ghostlock::pipeline::stepset_name(spec.steps) << '\t'
             << ghostlock::contract::terminal_token_name(spec.terminal) << '\t'
             << (spec.available ? 1 : 0) << '\t' << spec.doc;
        return line.str();
    }

    std::vector<std::string> expected_rows() {
        std::vector<std::string> rows;
        for (const ghostlock::contract::CombinationSpec &spec :
             ghostlock::contract::kCombinationCatalog) {
            rows.push_back(row_for(spec));
        }
        return rows;
    }

    std::string manifest_text(const std::vector<std::string> &rows) {
        std::string text = manifest_header();
        for (const std::string &row : rows) text += row + "\n";
        return text;
    }

    /* ---- resolve vectors ---- */

    /* Row classes; the exporter asserts every class is represented so the
     * interesting decision boundaries cannot silently disappear. */
    enum : unsigned {
        kCanonical = 1U << 0U,
        kCrossBackend = 1U << 1U,
        kCase = 1U << 2U,
        kWhitespace = 1U << 3U,
        kEmpty = 1U << 4U,
        kUnknown = 1U << 5U,
        kPlanned = 1U << 6U,
        kNoCatalogBackend = 1U << 7U,
    };

    struct VectorRow final {
        ghostlock::contract::BackendKind backend;
        std::string input;
        unsigned classes;
    };

    std::vector<VectorRow> vector_rows() {
        using ghostlock::contract::BackendKind;
        std::vector<VectorRow> rows;
        /* 1. canonical: each catalogue row with its owning backend. Planned
         * rows are additionally marked kPlanned (they resolve AND are
         * unavailable; the two facts are separate by design). */
        for (const ghostlock::contract::CombinationSpec &spec :
             ghostlock::contract::kCombinationCatalog) {
            rows.push_back({spec.backend, std::string(spec.token),
                            spec.available ? kCanonical : (kCanonical | kPlanned)});
        }
        /* 2. cross-backend: every token fed to the other catalogued backend. */
        for (const ghostlock::contract::CombinationSpec &spec :
             ghostlock::contract::kCombinationCatalog) {
            const BackendKind other =
                    spec.backend == BackendKind::Cve2026_43499
                            ? BackendKind::Cve2026_43284
                            : BackendKind::Cve2026_43499;
            rows.push_back({other, std::string(spec.token), kCrossBackend});
        }
        /* 3. case variants. */
        rows.push_back({BackendKind::Cve2026_43499, "MCAST_ROOTCHILD", kCase});
        rows.push_back({BackendKind::Cve2026_43284, "UMH", kCase});
        rows.push_back({BackendKind::Cve2026_43284, "Rootchild", kCase});
        /* 4. whitespace variants (leading, trailing, tab, both). */
        rows.push_back({BackendKind::Cve2026_43499, " mcast_rootchild", kWhitespace});
        rows.push_back({BackendKind::Cve2026_43499, "mcast_rootchild ", kWhitespace});
        rows.push_back({BackendKind::Cve2026_43284, "\tumh", kWhitespace});
        rows.push_back({BackendKind::Cve2026_43284, " umh ", kWhitespace});
        /* 5. empty string. */
        rows.push_back({BackendKind::Cve2026_43499, "", kEmpty});
        rows.push_back({BackendKind::Cve2026_43284, "", kEmpty});
        /* 6. unknown tokens. */
        rows.push_back({BackendKind::Cve2026_43499, "bogus", kUnknown});
        rows.push_back({BackendKind::Cve2026_43499, "mcast_", kUnknown});
        rows.push_back({BackendKind::Cve2026_43499, "_rootchild", kUnknown});
        rows.push_back({BackendKind::Cve2026_43499, "mcast_rootchild_umh", kUnknown});
        rows.push_back({BackendKind::Cve2026_43284, "UMH_", kUnknown});
        /* 7. a known backend id with no catalogue rows. */
        rows.push_back({BackendKind::Cve2026_64560, "umh", kNoCatalogBackend});
        rows.push_back({BackendKind::Cve2026_64560, "", kNoCatalogBackend});
        return rows;
    }

    /* Escapes the input column so blank characters are visible and diff-stable:
     * \s space, \t tab, \\ backslash. Nothing else is escaped. */
    std::string escape_input(std::string_view text) {
        std::string out;
        for (const char ch : text) {
            if (ch == '\\') {
                out += "\\\\";
            } else if (ch == '\t') {
                out += "\\t";
            } else if (ch == ' ') {
                out += "\\s";
            } else {
                out += ch;
            }
        }
        return out;
    }

    std::string expected_token(ghostlock::contract::BackendKind backend,
                               std::string_view input) {
        ghostlock::contract::CombinationKind kind =
                ghostlock::contract::CombinationKind::Unknown;
        if (!ghostlock::contract::combination_resolve(backend, input, kind)) {
            return "--";
        }
        const std::string_view token = ghostlock::contract::combination_name(kind);
        return token.empty() ? std::string("--") : std::string(token);
    }

    std::string vectors_text(const std::vector<VectorRow> &rows) {
        std::string text = vectors_header();
        for (const VectorRow &row : rows) {
            text += ghostlock::contract::backend_token_name(row.backend);
            text += '\t';
            text += escape_input(row.input);
            text += '\t';
            text += expected_token(row.backend, row.input);
            text += '\n';
        }
        return text;
    }

    /* ---- source invariants (hold in both modes) ---- */

    bool fail(const char *what) {
        std::fprintf(stderr, "combination_manifest_test: %s\n", what);
        return false;
    }

    bool check_columns_have_no_tab(std::string_view text) {
        return text.find('\t') == std::string_view::npos;
    }

    bool check_catalog_invariants() {
        using ghostlock::contract::CombinationId;
        using ghostlock::contract::CombinationKind;
        using ghostlock::contract::CombinationSpec;
        const CombinationSpec *all = ghostlock::contract::kCombinationCatalog;
        const std::size_t count = std::size(ghostlock::contract::kCombinationCatalog);
        for (std::size_t i = 0; i < count; i++) {
            const CombinationSpec &spec = all[i];
            if (spec.token.empty()) return fail("catalogue row with an empty token");
            if (spec.doc.empty()) return fail("catalogue row with an empty doc");
            if (!check_columns_have_no_tab(spec.token) ||
                !check_columns_have_no_tab(spec.doc)) {
                return fail("catalogue token/doc contains a TAB");
            }
            CombinationId id{};
            if (!ghostlock::contract::combination_id(spec.kind, id)) {
                return fail("combination_id() rejected a catalogue kind");
            }
            if (id.backend != spec.backend || id.route != spec.route ||
                id.path != spec.path) {
                return fail("combination_id() disagrees with its catalogue row");
            }
            if (ghostlock::contract::combination_from_id(id) != spec.kind) {
                return fail("combination_from_id() is not the inverse of combination_id()");
            }
            /* (backend, route, path) is unique, so the decomposition never
             * aliases two tokens. */
            for (std::size_t j = 0; j < i; j++) {
                if (all[j].backend == spec.backend && all[j].route == spec.route &&
                    all[j].path == spec.path) {
                    return fail("duplicate (backend, route, path) in the catalogue");
                }
            }
            /* availability and wiring are two facts, but they must agree: a
             * planned row has no dispatch target, a wired row has one. */
            const bool wired = ghostlock::pipeline::dispatch_target_of(
                                       spec.backend, spec.kind) !=
                               ghostlock::pipeline::DispatchTarget::None;
            if (wired != spec.available) {
                return fail("available flag disagrees with the dispatch target");
            }
        }
        return true;
    }

    bool check_vector_invariants(const std::vector<VectorRow> &rows) {
        using ghostlock::contract::CombinationKind;
        using ghostlock::contract::CombinationSpec;
        if (rows.size() < 40U) return fail("fewer than 40 resolve vectors");
        /* The ESCAPED column must never contain a literal TAB (the raw input
         * may: that is exactly what the whitespace class encodes). */
        if (escape_input("\t") != "\\t" || escape_input(" a ") != "\\sa\\s" ||
            escape_input("\\") != "\\\\") {
            return fail("input escaping is not the documented \\s/\\t/\\\\ form");
        }
        unsigned seen = 0U;
        for (const VectorRow &row : rows) {
            seen |= row.classes;
            if (!check_columns_have_no_tab(escape_input(row.input))) {
                return fail("escaped vector input contains a literal TAB");
            }
            if (!check_columns_have_no_tab(expected_token(row.backend, row.input))) {
                return fail("vector expectation contains a TAB");
            }
        }
        const unsigned required = kCanonical | kCrossBackend | kCase | kWhitespace |
                                  kEmpty | kUnknown | kPlanned | kNoCatalogBackend;
        if ((seen & required) != required) {
            return fail("a resolve-vector class has no row");
        }
        /* Every catalogue row has its canonical vector, and a planned row
         * resolves while staying unavailable (resolution != availability). */
        for (const CombinationSpec &spec : ghostlock::contract::kCombinationCatalog) {
            bool found = false;
            for (const VectorRow &row : rows) {
                if (row.backend == spec.backend && row.input == spec.token) {
                    if (expected_token(row.backend, row.input) != spec.token) {
                        return fail("canonical vector does not resolve to its token");
                    }
                    found = true;
                }
            }
            if (!found) return fail("catalogue row without a canonical vector");
            if (!spec.available) {
                CombinationKind kind = CombinationKind::Unknown;
                if (!ghostlock::contract::combination_resolve(spec.backend, spec.token,
                                                              kind)) {
                    return fail("planned row does not resolve");
                }
                if (ghostlock::contract::combination_available(kind)) {
                    return fail("planned row reports available");
                }
            }
        }
        return true;
    }

    /* ---- file helpers ---- */

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
            std::printf("combination_manifest_test: wrote %s\n", path.c_str());
            written++;
        }
        return written;
    }

    /* Reads the single existing path of one group and compares it with the
     * export. Returns 1 on success, 0 when the group is missing or ambiguous,
     * -1 on content drift. */
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
                         "combination_manifest_test: expected exactly one %s "
                         "artifact (%d found)\n",
                         label, hits);
            return 0;
        }
        if (text != expected) {
            std::fprintf(stderr,
                         "combination_manifest_test: %s artifact drift (%s)\n",
                         label, found);
            return -1;
        }
        std::printf("combination_manifest_test: %s ok (%s)\n", label, found);
        return 1;
    }
} // namespace

int main(int argc, char **argv) {
    bool write = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--write") write = true;
    }

    if (!check_catalog_invariants()) return 1;
    const std::vector<VectorRow> vectors = vector_rows();
    if (!check_vector_invariants(vectors)) return 1;

    const std::vector<std::string> rows = expected_rows();
    const std::string manifest = manifest_text(rows);
    const std::string vector_text = vectors_text(vectors);

    if (write) {
        const int app = write_group(kAppResourceCandidates,
                                    std::size(kAppResourceCandidates), manifest);
        const int runtime = write_group(kRuntimeResourceCandidates,
                                        std::size(kRuntimeResourceCandidates), manifest);
        const int vec = write_group(kVectorsResourceCandidates,
                                    std::size(kVectorsResourceCandidates), vector_text);
        if (app != 1 || runtime != 1 || vec != 1) {
            std::fprintf(stderr,
                         "combination_manifest_test: expected one writable app, "
                         "runtime and vector resource (app=%d runtime=%d vectors=%d)\n",
                         app, runtime, vec);
            return 1;
        }
        return 0;
    }

    const int app = check_group(kAppResourceCandidates,
                                std::size(kAppResourceCandidates), "app", manifest);
    if (app <= 0) return 1;
    const int runtime = check_group(kRuntimeResourceCandidates,
                                    std::size(kRuntimeResourceCandidates),
                                    "runtime", manifest);
    if (runtime <= 0) return 1;
    const int vec = check_group(kVectorsResourceCandidates,
                                std::size(kVectorsResourceCandidates), "vectors",
                                vector_text);
    if (vec <= 0) return 1;

    std::printf("combination_manifest_test: ok (%zu rows, %zu vectors)\n",
                rows.size(), vectors.size());
    return 0;
}
