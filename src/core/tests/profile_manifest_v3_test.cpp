/* Host test / exporter for the GLKv3 owner-qualified field manifest (S4 R2).
 *
 * The native GLKv3 FieldSpec lists are the single source of truth for the
 * owner-qualified manifest universe:
 *
 *   owner<TAB>path<TAB>wire<TAB>required<TAB>default<TAB>source<TAB>doc
 *
 * where path is the owner-qualified "<section>.<key>" (R2). default/source/doc
 * are joined from the matching v2 owner Schema FieldSpec (same section/key) so
 * the manifest carries the R1 declaration once. "--" marks an empty value so
 * every line has exactly seven non-empty columns.
 *
 * Kotlin (ProfileManifestV3AgreementTest) asserts its generated
 * NativeProfileGlkv3Adapter path -> type table equals the manifest; the adapter
 * itself parses the same manifest resource, so a key or wire type added to only
 * one side fails that side's test instead of silently reaching production.
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
#include <fstream>
#include <iterator>
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

    std::string_view owner_for(std::string_view section) {
        if (section == "common") return "common";
        if (section.starts_with("platform.")) return "platform::abi";
        if (section.starts_with("countermeasure.")) return "countermeasure::vivo";
        if (section.starts_with("plugin")) return "plugin";
        if (section.starts_with("backend.cve_2026_43284")) return "cve_2026_43284";
        if (section.starts_with("backend.cve_2026_43499")) return "cve_2026_43499";
        return "unknown";
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
            def = "-";
            source = "-";
            doc = "-";
            return;
        }
        if (def.empty()) def = "-";
        if (source.empty()) source = "-";
        if (doc.empty()) doc = "-";
    }

    template<typename Fields>
    void append_owner(std::vector<std::string> &lines, const Fields &fields) {
        for (const auto &field : fields) {
            std::string def;
            std::string source;
            std::string doc;
            lookup_declaration(field.section, field.key, def, source, doc);
            std::ostringstream line;
            line << owner_for(field.section) << '\t' << field.section << '.'
                 << field.key << '\t'
                 << ghostlock::profile::glkv3::wire_type_name(field.type) << '\t'
                 << (field.required ? 1 : 0) << '\t' << def << '\t' << source
                 << '\t' << doc;
            lines.push_back(line.str());
        }
    }

    std::vector<std::string> schema_lines() {
        std::vector<std::string> lines;
        append_owner(lines, ghostlock::platform::abi::kPlatformAbiGlkv3Fields);
        append_owner(lines, ghostlock::backend::kCve2026_43499Glkv3Fields);
        append_owner(lines, ghostlock::backend::kCve2026_43284Glkv3Fields);
        append_owner(lines, ghostlock::plugin::kPluginGlkv3Fields);
        std::sort(lines.begin(), lines.end());
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
