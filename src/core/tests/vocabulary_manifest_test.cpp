/* Host test / exporter for the component vocabulary manifest (S4 task-6).
 *
 * contract/identity.hpp is the single authority for the three selection
 * vocabularies; this unit exports token <-> wire <-> availability so the Kotlin
 * side stops carrying its own expectation tables:
 *
 *   kind<TAB>token<TAB>wire<TAB>available<TAB>doc
 *
 * kind is one of backend | frontend | stepset | route and the rows are emitted in
 * catalogue order (the UI order and the Kotlin enum order both derive from it).
 * wire is the numeric id native stores in the document slots; available is 1/0
 * and comes from the same per-axis predicate the selection gate uses.
 *
 * "--write" regenerates the app test resource and the profile-core runtime
 * resource from one text (so they cannot drift); the bare run verifies EVERY
 * committed copy byte-for-byte and names the first differing line. Kotlin reads
 * the resource and asserts its enums against it (VocabularyAgreementTest). */

#include "contract/identity.hpp"
#include "pipeline/component_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    const char *kAppResourceCandidates[] = {
        "app/src/test/resources/vocabulary-manifest.tsv",
        "../app/src/test/resources/vocabulary-manifest.tsv",
        "../../app/src/test/resources/vocabulary-manifest.tsv",
    };
    const char *kRuntimeResourceCandidates[] = {
        "profile-core/src/main/resources/vocabulary-manifest.tsv",
        "../profile-core/src/main/resources/vocabulary-manifest.tsv",
        "../../profile-core/src/main/resources/vocabulary-manifest.tsv",
    };

    struct Row final {
        std::string_view kind;
        std::string_view token;
        std::uint32_t wire;
        bool available;
        std::string_view doc;
    };

    std::string manifest_header() {
        return "# GhostLock component vocabulary manifest (S4 task-6).\n"
               "# Authoritative native export of contract/identity.hpp, catalogue order;\n"
               "# regenerate with: make -C src vocabulary-manifest\n"
               "# Columns: kind<TAB>token<TAB>wire<TAB>available<TAB>doc\n"
               "# kind is backend | frontend | stepset | route; wire is the numeric id native stores\n"
               "# in the document slots; route wire values come from kRouteCatalog; available is\n"
               "# 1 (usable) or 0 (declared, rejected).\n";
    }

    std::vector<Row> expected_rows() {
        using ghostlock::contract::BackendKind;
        using ghostlock::contract::StepSetKind;
        using ghostlock::contract::TerminalKind;
        std::vector<Row> rows;
        for (const BackendKind kind :
             {BackendKind::Cve2026_43499, BackendKind::Cve2026_64560,
              BackendKind::Cve2026_31431, BackendKind::Cve2026_43503,
              BackendKind::Cve2026_23274, BackendKind::Cve2026_43284}) {
            rows.push_back({"backend", ghostlock::contract::backend_token_name(kind),
                            static_cast<std::uint32_t>(kind),
                            ghostlock::contract::backend_available(kind),
                            "backend identity; unavailable ids are declared placeholders"});
        }
        for (const TerminalKind kind :
             {TerminalKind::RootChild, TerminalKind::UmhForward}) {
            rows.push_back({"frontend", ghostlock::contract::terminal_token_name(kind),
                            static_cast<std::uint32_t>(kind),
                            ghostlock::contract::terminal_available(kind),
                            "handoff frontend; the terminal that takes over"});
        }
        /* Route policies: backend-internal selection, but the token <-> wire map is
         * a cross-language fact (Kotlin RouteKind mirrors kRouteCatalog). */
        for (const ghostlock::profile::RouteCatalogEntry &entry :
             ghostlock::profile::kRouteCatalog) {
            rows.push_back({"route", entry.token, entry.wire, true,
                            "route policy; backend-internal, selected by the document"});
        }
        /* StepSetKind::Unknown = 0 is a native sentinel (absent/unknown wire id),
         * not a selectable vocabulary entry, so it is deliberately not exported. */
        for (const StepSetKind kind : {StepSetKind::W1W2, StepSetKind::W1W3,
                                       StepSetKind::PageCacheWrite}) {
            rows.push_back({"stepset", ghostlock::pipeline::stepset_name(kind),
                            static_cast<std::uint32_t>(kind),
                            ghostlock::contract::stepset_available(kind),
                            "backend step set; Unknown=0 is a native sentinel"});
        }
        return rows;
    }

    std::string row_text(const Row &row) {
        std::ostringstream line;
        line << row.kind << '\t' << row.token << '\t' << row.wire << '\t'
             << (row.available ? 1 : 0) << '\t' << row.doc;
        return line.str();
    }

    std::string manifest_text(const std::vector<Row> &rows) {
        std::string text = manifest_header();
        for (const Row &row : rows) text += row_text(row) + "\n";
        return text;
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
            std::printf("vocabulary_manifest_test: wrote %s\n", path.c_str());
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
                         "vocabulary_manifest_test: expected exactly one %s manifest "
                         "(%d found)\n",
                         label, hits);
            return 0;
        }
        if (text != expected) {
            std::fprintf(stderr,
                         "vocabulary_manifest_test: %s manifest drift (%s), first "
                         "differing line %zu\n",
                         label, found, first_differing_line(text, expected));
            return -1;
        }
        std::printf("vocabulary_manifest_test: %s ok (%s)\n", label, found);
        return 1;
    }

    bool check_rows(const std::vector<Row> &rows) {
        if (rows.size() != 14u) {
            std::fprintf(stderr, "vocabulary_manifest_test: unexpected row count %zu\n",
                         rows.size());
            return false;
        }
        for (std::size_t i = 0u; i < rows.size(); ++i) {
            if (rows[i].token.empty() || rows[i].doc.empty()) {
                std::fprintf(stderr, "vocabulary_manifest_test: empty token/doc\n");
                return false;
            }
            for (std::size_t j = 0u; j < i; ++j) {
                if (rows[j].kind != rows[i].kind) continue;
                if (rows[j].wire == rows[i].wire || rows[j].token == rows[i].token) {
                    std::fprintf(stderr,
                                 "vocabulary_manifest_test: duplicate %.*s entry\n",
                                 static_cast<int>(rows[i].kind.size()),
                                 rows[i].kind.data());
                    return false;
                }
            }
        }
        /* Guard (step-queue design doc 10.1-S8/S9/S13): a name that fell back to
         * the "unknown"/"none" default must fail here instead of being exported as
         * a legitimate vocabulary token. Both committed copies are compared byte
         * for byte, so without this a missing *_name() branch would weaken the
         * contract while the gate stayed green. */
        for (const Row &row : rows) {
            if (row.token == "unknown" || row.token == "none") {
                std::fprintf(stderr,
                             "vocabulary_manifest_test: guard: unmapped %.*s name\n",
                             static_cast<int>(row.kind.size()), row.kind.data());
                return false;
            }
        }
        /* Guard: the per-kind composition is pinned (4 kinds). */
        const struct { const char *kind; std::size_t count; } kExpectedKinds[] = {
            {"backend", 6U}, {"frontend", 2U}, {"stepset", 3U}, {"route", 3U}};
        for (const auto &expected : kExpectedKinds) {
            std::size_t count = 0U;
            for (const Row &row : rows) {
                if (row.kind == expected.kind) ++count;
            }
            if (count != expected.count) {
                std::fprintf(stderr,
                             "vocabulary_manifest_test: guard: kind %s has %zu rows, "
                             "expected %zu\n",
                             expected.kind, count, expected.count);
                return false;
            }
        }
        /* Guard: the three step-set rows keep their wire ids 1/2/3 (a renumber
         * would break the wire contract). */
        const struct { const char *token; std::uint32_t wire; } kStepSets[] = {
            {"w1_w2", 1U}, {"w1_w3", 2U}, {"pagecache_write", 3U}};
        for (const auto &expected : kStepSets) {
            bool found = false;
            for (const Row &row : rows) {
                if (row.kind == "stepset" && row.token == expected.token &&
                    row.wire == expected.wire) {
                    found = true;
                }
            }
            if (!found) {
                std::fprintf(stderr,
                             "vocabulary_manifest_test: guard: stepset %s wire %u "
                             "missing\n",
                             expected.token, expected.wire);
                return false;
            }
        }
        return true;
    }
} // namespace

int main(int argc, char **argv) {
    bool write = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--write") write = true;
    }

    const std::vector<Row> rows = expected_rows();
    if (!check_rows(rows)) return 1;
    const std::string text = manifest_text(rows);

    if (write) {
        const int app = write_group(kAppResourceCandidates,
                                    std::size(kAppResourceCandidates), text);
        const int runtime = write_group(kRuntimeResourceCandidates,
                                        std::size(kRuntimeResourceCandidates), text);
        if (app != 1 || runtime != 1) {
            std::fprintf(stderr,
                         "vocabulary_manifest_test: expected one writable app and "
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
                                    std::size(kRuntimeResourceCandidates), "runtime",
                                    text);
    if (runtime <= 0) return 1;
    std::printf("vocabulary_manifest_test: ok (%zu rows)\n", rows.size());
    return 0;
}
