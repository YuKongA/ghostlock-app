/* Host test / exporter for the GLKv3 owner-qualified field manifest (S4 R2).
 *
 * The native GLKv3 FieldSpec lists are the single source of truth for the
 * owner-qualified manifest universe:
 *
 *   owner<TAB>path<TAB>wire<TAB>required<TAB>default<TAB>source<TAB>doc
 *
 * where path is the owner-qualified "<section>.<key>" (R2). A root scalar (the
 * HOCON-refactor kernel_major / kernel_minor / safe_mode, declared with the
 * empty section) has no section prefix: its path is the bare key and its owner
 * is "root". default/source/doc are joined from the matching v2 owner Schema
 * FieldSpec (same section/key) so the manifest carries the R1 declaration once.
 * A wire_only declaration is omitted: the key stays accepted on the wire but is
 * not a profile key (kmi / lkm_path / carrier_path). "--" marks an empty value
 * so every line has exactly seven non-empty columns.
 *
 * Kotlin (ProfileManifestV3AgreementTest) asserts its generated
 * NativeProfileGlkv3Adapter path -> type table equals the manifest; the adapter
 * itself parses the same manifest resource, so a key or wire type added to only
 * one side fails that side's test instead of silently reaching production.
 *
 * Guards (each one fails the run instead of exporting a weakened contract; the
 * two copies are compared byte for byte, so without them a silently weakened
 * export would still keep both copies identical and the gate green):
 *   - owner_for(): an unmapped section prefix is a hard failure, never the
 *     literal owner "unknown";
 *   - lookup_declaration(): an exported (section, key) with no v2 owner
 *     declaration is a hard failure, never "-" columns;
 *   - check_export_shape(): the row count must equal the appended tables minus
 *     their wire_only rows, and the owner set must equal BOTH the expected
 *     literal set and the owner set of the registered v2 owner Schemas;
 *   - check_wire_only_keys(): kmi / lkm_path / carrier_path stay declared in the
 *     C++ table with wire_only=true and never appear in the export.
 *
 * Run bare to verify every committed copy: the app test resource and the
 * profile-core runtime resource must each resolve to exactly one path AND match
 * the exported text byte-for-byte (header included). Comparing only the first
 * copy that opens would leave the runtime consumer unchecked; run with --write
 * to regenerate both (make -C src profile-manifest-v3). */

#include "backend/cve_2026_43284/glkv3_schema.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/glkv3_schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "platform/abi.hpp"
#include "plugin/schema.hpp"
#include "profile/glkv3.hpp"
#include "profile/schema.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
    /* Two destinations: the app test resource (native + app agreement tests)
     * and the profile-core runtime resource the Kotlin adapter parses. Only the
     * relative form that resolves from the current working directory is written;
     * the other is skipped. */
    const char *kAppResourceCandidates[] = {
        "app/src/test/resources/profile-manifest-v3.tsv",
        "../app/src/test/resources/profile-manifest-v3.tsv",
        "../../app/src/test/resources/profile-manifest-v3.tsv",
    };
    const char *kRuntimeResourceCandidates[] = {
        "profile-core/src/main/resources/profile-manifest-v3.tsv",
        "../profile-core/src/main/resources/profile-manifest-v3.tsv",
        "../../profile-core/src/main/resources/profile-manifest-v3.tsv",
    };

    std::string manifest_header() {
        return "# GhostLock GLKv3 owner-qualified manifest (GLKv3 schema 3 / generated form R2).\n"
               "# Authoritative native export; regenerate with:\n"
               "#   make -C src profile-manifest-v3\n"
               "# Columns: owner<TAB>path<TAB>wire<TAB>required<TAB>default<TAB>source<TAB>doc\n"
               "# The wire column may be a \"|\"-separated UNION of wire kinds (the S4 P1\n"
               "# dynamic plugin paths plugin.<id>.params.* / plugin.<id>.extract.*); every\n"
               "# member must come from the wire-kind vocabulary and an unknown member is\n"
               "# rejected fail-closed (the Kotlin adapter parses the same union).\n"
               "# Dynamic paths resolve their concrete type from the plugin descriptor\n"
               "# (probe TSV param/extract rows), whose type literals are these same kinds.\n";
    }

    /* A guard failure is a hard failure. The manifest is a cross-language
     * contract whose two copies are compared byte for byte, so a silently
     * weakened export (unknown owner, missing declaration, dropped table) would
     * still keep both copies identical and the gate green. */
    [[noreturn]] void guard_fail(const std::string &message) {
        std::fprintf(stderr, "profile_manifest_v3_test: guard: %s\n",
                     message.c_str());
        std::exit(1);
    }

    std::string_view owner_for(std::string_view section) {
        /* HOCON refactor: the empty section is the document root (see
         * profile/document.hpp kRootSection). The "common" and
         * "countermeasure.*" owners are deleted, so they have no branch. */
        if (section.empty()) return "root";
        if (section.starts_with("plugin")) return "plugin";
        // USER DIRECTIVE 2026-10-05: payload paused
        // if (section.starts_with("payload")) return "payload";
        if (section.starts_with("backend.cve_2026_43284")) return "cve_2026_43284";
        if (section.starts_with("backend.cve_2026_43499")) return "cve_2026_43499";
        /* Guard: an unmapped owner prefix must never be exported as a literal
         * "unknown" owner -- that would weaken the contract silently while both
         * copies stay identical. Adding an owner means adding its branch here. */
        guard_fail("owner_for(): no owner mapping for section '" +
                   std::string(section) + "'");
    }

    std::string format_default(const ghostlock::profile::DefaultValue &value) {
        using Kind = ghostlock::profile::DefaultValue::Kind;
        switch (value.kind) {
            case Kind::None:
                return {};
            case Kind::Literal:
                return "literal:" + std::to_string(value.literal_value);
            case Kind::Derived:
                return "derived";
            case Kind::Convention:
                return "convention:" + std::string(value.convention_path);
        }
        return {};
    }

    std::string_view source_name(ghostlock::profile::FieldSource source) {
        switch (source) {
            case ghostlock::profile::FieldSource::Profile: return "profile";
            case ghostlock::profile::FieldSource::Derived: return "derived";
            case ghostlock::profile::FieldSource::Convention: return "convention";
            case ghostlock::profile::FieldSource::Platform: return "platform";
        }
        return "profile";
    }

    template<typename Schema>
    bool lookup_v2(std::string_view section, std::string_view key,
                   std::string &def, std::string &source, std::string &doc) {
        for (const auto &field : Schema::kFields) {
            if (field.section != section || field.key != key) continue;
            def = format_default(field.default_value);
            source = std::string(source_name(field.source));
            doc = std::string(field.doc);
            return true;
        }
        return false;
    }

    void lookup_declaration(std::string_view section, std::string_view key,
                            std::string &def, std::string &source,
                            std::string &doc) {
        def.clear();
        source.clear();
        doc.clear();
        const bool found =
                lookup_v2<ghostlock::platform::abi::Schema>(section, key, def, source, doc) ||
                lookup_v2<ghostlock::backend::Cve2026_43499Schema>(section, key, def,
                                                                   source, doc) ||
                lookup_v2<ghostlock::backend::Cve2026_43284Schema>(section, key, def,
                                                                   source, doc) ||
                lookup_v2<ghostlock::plugin::Schema>(section, key, def, source, doc);
        if (!found) {
            /* Guard: a row with no v2 owner declaration would export
             * default/source/doc as "-" -- a silent weakening of the contract.
             * Every exported (section, key) must be declared by exactly one of
             * the registered owner Schemas. */
            guard_fail("lookup_declaration(): no v2 owner declaration for '" +
                       std::string(section) + "." + std::string(key) + "'");
        }
        if (def.empty()) def = "-";
        if (source.empty()) source = "-";
        if (doc.empty()) doc = "-";
    }

    template<typename Fields>
    void append_owner(std::vector<std::string> &lines, const Fields &fields) {
        for (const auto &field : fields) {
            /* HOCON refactor: a wire-only key has no profile declaration -- the
             * wire keeps it (native resolves it at the point of use) but the
             * manifest, which is the Kotlin profile-declaration surface, must
             * not offer it. kmi / lkm_path / carrier_path today. */
            if (field.wire_only) continue;
            std::string def;
            std::string source;
            std::string doc;
            lookup_declaration(field.section, field.key, def, source, doc);
            /* A root key has no section prefix: path == key. */
            std::string path(field.section);
            if (!path.empty()) path.push_back('.');
            path.append(field.key);
            std::ostringstream line;
            line << owner_for(field.section) << '\t' << path << '\t'
                 << ghostlock::profile::glkv3::wire_type_name(field.type) << '\t'
                 << (field.required ? 1 : 0) << '\t' << def << '\t' << source
                 << '\t' << doc;
            lines.push_back(line.str());
        }
    }

    /* Rows a table contributes to the manifest: every declaration except the
     * wire-only keys, which the profile must not offer. */
    template<typename Fields>
    std::size_t exported_row_count(const Fields &fields) {
        std::size_t rows = 0;
        for (const auto &field : fields) {
            if (!field.wire_only) ++rows;
        }
        return rows;
    }

    std::set<std::string> exported_owner_set(const std::vector<std::string> &lines) {
        std::set<std::string> owners;
        for (const std::string &line : lines) {
            const std::size_t tab = line.find('\t');
            owners.insert(line.substr(0, tab));
        }
        return owners;
    }

    /* The owner labels the registered v2 owner Schemas actually declare: the
     * manifest side of the registry's owner set. */
    std::set<std::string> registered_owner_set() {
        std::set<std::string> owners;
        for (const auto &field : ghostlock::platform::abi::Schema::kFields) {
            owners.insert(std::string(owner_for(field.section)));
        }
        for (const auto &field : ghostlock::backend::Cve2026_43499Schema::kFields) {
            owners.insert(std::string(owner_for(field.section)));
        }
        for (const auto &field : ghostlock::backend::Cve2026_43284Schema::kFields) {
            owners.insert(std::string(owner_for(field.section)));
        }
        for (const auto &field : ghostlock::plugin::Schema::kFields) {
            owners.insert(std::string(owner_for(field.section)));
        }
        return owners;
    }

    std::string join_set(const std::set<std::string> &values) {
        std::string out;
        for (const std::string &value : values) {
            if (!out.empty()) out += ", ";
            out += value;
        }
        return out;
    }

    /* Guard: the three runtime-injected convention keys must stay declared in
     * the C++ table (the wire accepts them) with wire_only = true (the profile
     * must not offer them), and must not appear in the export. This
     * machine-checks the "wire keeps it, profile rejects it" ruling. */
    void check_wire_only_keys(const std::vector<std::string> &lines) {
        constexpr std::string_view kConventionKeys[] = {"kmi", "lkm_path",
                                                        "carrier_path"};
        for (const std::string_view key : kConventionKeys) {
            const ghostlock::profile::glkv3::FieldSpec *found = nullptr;
            for (const auto &field : ghostlock::backend::kCve2026_43284Glkv3Fields) {
                if (field.section == "backend.cve_2026_43284" && field.key == key) {
                    found = &field;
                }
            }
            if (found == nullptr || !found->wire_only) {
                guard_fail("wire_only: convention key '" + std::string(key) +
                           "' must stay declared in the GLKv3 table with "
                           "wire_only=true");
            }
            const std::string path = "backend.cve_2026_43284." + std::string(key);
            for (const std::string &line : lines) {
                if (line.find(path) != std::string::npos) {
                    guard_fail("wire_only: '" + path +
                               "' must not appear in the profile manifest");
                }
            }
        }
    }

    /* Guards on the export shape. The two committed copies are compared byte for
     * byte, so a table that stops being appended, a new owner that is only
     * half-registered or a wire-only key that leaks would keep that comparison
     * green while the contract silently shrinks. */
    void check_export_shape(const std::vector<std::string> &lines) {
        const std::size_t expected_rows =
                exported_row_count(ghostlock::platform::abi::kPlatformAbiGlkv3Fields) +
                exported_row_count(ghostlock::backend::kCve2026_43499Glkv3Fields) +
                exported_row_count(ghostlock::backend::kCve2026_43284Glkv3Fields) +
                exported_row_count(ghostlock::plugin::kPluginGlkv3Fields);
        // USER DIRECTIVE 2026-10-05: payload paused -> kPayloadGlkv3Fields is
        // not appended; restoring payload means adding it to this sum too.
        if (lines.size() != expected_rows) {
            guard_fail("row count: exported " + std::to_string(lines.size()) +
                       " rows, declaration tables say " +
                       std::to_string(expected_rows));
        }
        const std::set<std::string> exported = exported_owner_set(lines);
        const std::set<std::string> expected_owners = {"root", "cve_2026_43499",
                                                       "cve_2026_43284", "plugin"};
        if (exported != expected_owners) {
            guard_fail("owner set: exported {" + join_set(exported) +
                       "} != expected {" + join_set(expected_owners) + "}");
        }
        const std::set<std::string> registered = registered_owner_set();
        if (exported != registered) {
            guard_fail("owner set: exported {" + join_set(exported) +
                       "} != registered v2 owner Schemas {" +
                       join_set(registered) + "}");
        }
        check_wire_only_keys(lines);
    }

    std::vector<std::string> schema_lines() {
        std::vector<std::string> lines;
        append_owner(lines, ghostlock::platform::abi::kPlatformAbiGlkv3Fields);
        append_owner(lines, ghostlock::backend::kCve2026_43499Glkv3Fields);
        append_owner(lines, ghostlock::backend::kCve2026_43284Glkv3Fields);
        append_owner(lines, ghostlock::plugin::kPluginGlkv3Fields);
        // USER DIRECTIVE 2026-10-05: payload paused
        // append_owner(lines, ghostlock::profile::kPayloadGlkv3Fields);
        std::sort(lines.begin(), lines.end());
        check_export_shape(lines);
        return lines;
    }

    std::vector<std::string> field_lines(const std::string &text) {
        std::vector<std::string> lines;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') continue;
            lines.push_back(line);
        }
        std::sort(lines.begin(), lines.end());
        return lines;
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

    /* Writes every candidate whose parent directory exists, requiring the app
     * resource and the runtime resource were each written exactly once. */
    int write_group(const char *const *candidates, size_t count,
                    const std::string &text) {
        int written = 0;
        for (size_t i = 0; i < count; i++) {
            const std::string path = candidates[i];
            const size_t slash = path.find_last_of('/');
            const std::string dir =
                    slash == std::string::npos ? "." : path.substr(0, slash);
            std::ifstream probe(dir);
            if (!probe.good()) continue;
            if (!write_file(path, text)) return -1;
            std::printf("profile_manifest_v3_test: wrote %s\n", path.c_str());
            written++;
        }
        return written;
    }

    void print_diff(const std::vector<std::string> &expected,
                    const std::vector<std::string> &actual) {
        std::vector<std::string> missing, extra;
        std::set_difference(expected.begin(), expected.end(), actual.begin(),
                            actual.end(), std::back_inserter(missing));
        std::set_difference(actual.begin(), actual.end(), expected.begin(),
                            expected.end(), std::back_inserter(extra));
        for (const std::string &line : missing) {
            std::fprintf(stderr, "manifest-v3 missing: %s\n", line.c_str());
        }
        for (const std::string &line : extra) {
            std::fprintf(stderr, "manifest-v3 extra:   %s\n", line.c_str());
        }
    }

    /* 1-based number of the first line where the two texts differ, or 0 when
     * they are identical. Used to point at the diverging copy instead of only
     * reporting that the row SET changed. */
    std::size_t first_differing_line(const std::string &a, const std::string &b) {
        std::size_t line = 1;
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i] != b[j]) return line;
            if (a[i] == '\n') line++;
            i++;
            j++;
        }
        return (i != a.size() || j != b.size()) ? line : 0;
    }
} // namespace

int main(int argc, char **argv) {
    bool write = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--write") write = true;
    }

    const std::vector<std::string> expected = schema_lines();

    if (write) {
        std::string text = manifest_header();
        for (const std::string &line : expected) text += line + "\n";
        const int app = write_group(kAppResourceCandidates,
                                    std::size(kAppResourceCandidates), text);
        const int runtime = write_group(kRuntimeResourceCandidates,
                                        std::size(kRuntimeResourceCandidates), text);
        if (app != 1 || runtime != 1) {
            std::fprintf(stderr,
                         "profile_manifest_v3_test: expected one writable app and "
                         "runtime resource (app=%d runtime=%d)\n",
                         app, runtime);
            return 1;
        }
        return 0;
    }

    /* Verify EVERY committed copy, not just the first one that opens. The app
     * test resource and the profile-core runtime resource are consumed by
     * different legs (native/Kotlin agreement vs the production adapter), so a
     * stale copy on either side must fail here: comparing only the first hit
     * would leave the runtime consumer unchecked. Each group must resolve to
     * exactly one path from the current working directory. */
    std::string expected_text = manifest_header();
    for (const std::string &line : expected) expected_text += line + "\n";

    const struct {
        const char *const *candidates;
        std::size_t count;
        const char *label;
    } groups[] = {
        {kAppResourceCandidates, std::size(kAppResourceCandidates), "app"},
        {kRuntimeResourceCandidates, std::size(kRuntimeResourceCandidates), "runtime"},
    };
    for (const auto &group : groups) {
        const char *found = nullptr;
        std::string text;
        int hits = 0;
        for (std::size_t i = 0; i < group.count; i++) {
            std::string candidate;
            if (!read_file(group.candidates[i], candidate)) continue;
            hits++;
            if (found == nullptr) {
                found = group.candidates[i];
                text = std::move(candidate);
            }
        }
        if (hits != 1) {
            std::fprintf(stderr,
                         "profile_manifest_v3_test: expected exactly one %s manifest "
                         "(%d found)\n",
                         group.label, hits);
            return 1;
        }
        if (text != expected_text) {
            std::fprintf(stderr,
                         "profile_manifest_v3_test: %s manifest drift (%s), first "
                         "differing line %zu\n",
                         group.label, found, first_differing_line(text, expected_text));
            print_diff(expected, field_lines(text));
            return 1;
        }
        std::printf("profile_manifest_v3_test: %s ok (%s)\n", group.label, found);
    }

    std::printf("profile_manifest_v3_test: ok (%zu fields, both copies)\n",
                expected.size());
    return 0;
}
