/* Host test / exporter for the GLKv3 path -> type manifest (GLKv3-3).
 *
 * The native GLKv3 FieldSpec lists are the single source of truth for the v3
 * (path, wire, required) universe. This test exports that universe to a
 * committed, machine-readable manifest shared by the native and Kotlin ends:
 *
 *   owner<TAB>path<TAB>wire<TAB>required
 *
 * where path is "<section>.<key>". Kotlin (ProfileManifestV3AgreementTest)
 * asserts its NativeProfileGlkv3Adapter path -> type table equals the manifest,
 * so a key or wire type added to only one side fails that side's test instead
 * of silently reaching the GLKv3-4 production migration.
 *
 * Run bare to verify the committed manifest; run with --write to regenerate it
 * from the GLKv3 FieldSpec lists (make -C src profile-manifest-v3). */

#include "backend/cve_2026_43284/glkv3_schema.hpp"
#include "backend/cve_2026_43499/glkv3_schema.hpp"
#include "profile/glkv3.hpp"

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
    const char *kCandidates[] = {
        "app/src/test/resources/profile-manifest-v3.tsv",
        "../app/src/test/resources/profile-manifest-v3.tsv",
        "../../app/src/test/resources/profile-manifest-v3.tsv",
    };

    std::string manifest_header() {
        return "# GhostLock GLKv3 path -> wire type manifest (GLKv3-3).\n"
               "# Authoritative export of the native GLKv3 FieldSpec lists;\n"
               "# regenerate with: make -C src profile-manifest-v3\n"
               "# Columns: owner<TAB>path<TAB>wire<TAB>required\n";
    }

    template<typename Fields>
    void append_owner(std::vector<std::string> &lines, std::string_view owner,
                      const Fields &fields) {
        for (const auto &field : fields) {
            std::ostringstream line;
            line << owner << '\t' << field.section << '.' << field.key << '\t'
                 << ghostlock::profile::glkv3::wire_type_name(field.type) << '\t'
                 << (field.required ? 1 : 0);
            lines.push_back(line.str());
        }
    }

    std::vector<std::string> schema_lines() {
        std::vector<std::string> lines;
        append_owner(lines, "cve_2026_43499",
                     ghostlock::backend::kCve2026_43499Glkv3Fields);
        append_owner(lines, "cve_2026_43284",
                     ghostlock::backend::kCve2026_43284Glkv3Fields);
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
        for (const char *path : kCandidates) {
            if (write_file(path, text)) {
                std::printf("profile_manifest_v3_test: wrote %s\n", path);
                return 0;
            }
        }
        std::fprintf(stderr, "profile_manifest_v3_test: no writable manifest path\n");
        return 1;
    }

    std::string text;
    const char *found = nullptr;
    for (const char *path : kCandidates) {
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
