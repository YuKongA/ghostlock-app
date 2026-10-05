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
 * Run bare to verify the committed manifest; run with --write to regenerate the
 * app test resource and the profile-core runtime resource (make -C src
 * profile-manifest-v3). */

#include "backend/cve_2026_43284/glkv3_schema.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/glkv3_schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "platform/abi.hpp"
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
               "# Columns: owner<TAB>path<TAB>wire<TAB>required<TAB>default<TAB>source<TAB>doc\n";
    }

    std::string_view owner_for(std::string_view section) {
        if (section == "common") return "common";
        if (section.starts_with("platform.")) return "platform::abi";
        if (section.starts_with("countermeasure.")) return "countermeasure::vivo";
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
                                                                   source, doc);
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

    const char *found = nullptr;
    std::string text;
    const char *all[] = {
        "app/src/test/resources/profile-manifest-v3.tsv",
        "../app/src/test/resources/profile-manifest-v3.tsv",
        "../../app/src/test/resources/profile-manifest-v3.tsv",
        "profile-core/src/main/resources/profile-manifest-v3.tsv",
        "../profile-core/src/main/resources/profile-manifest-v3.tsv",
    };
    for (const char *path : all) {
        if (read_file(path, text)) {
            found = path;
            break;
        }
    }
    if (found == nullptr) {
        std::fprintf(stderr,
                     "profile_manifest_v3_test: manifest not found in any candidate\n");
        return 1;
    }

    const std::vector<std::string> actual = field_lines(text);
    if (expected != actual) {
        std::fprintf(stderr,
                     "profile_manifest_v3_test: manifest drift (%zu schema vs %zu file)\n",
                     expected.size(), actual.size());
        print_diff(expected, actual);
        return 1;
    }

    std::printf("profile_manifest_v3_test: ok (%zu fields, %s)\n", expected.size(),
                found);
    return 0;
}
