/* Host test / exporter for the profile field manifest (ADR-0003 / A2-3c-3).
 *
 * The native owner Schema is the single source of truth for the wire
 * (section, key, width, required) universe. This test exports that universe to
 * a committed, machine-readable manifest shared by the three ends:
 *
 *   owner<TAB>section<TAB>key<TAB>width<TAB>required
 *
 * Kotlin (ProfileManifestAgreementTest) asserts its wire key set equals the
 * manifest; the extractor (report.rs) asserts its declared native keys are a
 * subset. A drift on any side fails that side's test instead of silently
 * reaching the startup parse.
 *
 * Run bare to verify the committed manifest; run with --write to regenerate
 * it from the Schema (make -C src profile-manifest).
 */

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "platform/abi.hpp"
#include "profile/schema.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

    /* The manifest lives in app test resources so the Kotlin unit test can read
     * it off its classpath; native/Rust read it by path. Several candidates
     * cover the working directory used by each runner. */
    const char *kCandidates[] = {
        "app/src/test/resources/profile-manifest.tsv",
        "../app/src/test/resources/profile-manifest.tsv",
        "../../app/src/test/resources/profile-manifest.tsv",
    };

    std::string manifest_header() {
        return "# GhostLock profile field manifest (A2-3c-3).\n"
               "# Authoritative export of the native owner Schemas; regenerate\n"
               "# with: make -C src profile-manifest\n"
               "# Columns: owner<TAB>section<TAB>key<TAB>width<TAB>required\n";
    }

    /* Append one manifest body line per field owned by Schema. */
    template<typename Schema>
    void append_owner(std::vector<std::string> &lines, std::string_view owner) {
        for (const auto &field : Schema::kFields) {
            std::ostringstream line;
            line << owner << '\t' << field.section << '\t' << field.key << '\t'
                 << static_cast<unsigned>(field.width) << '\t'
                 << (field.required ? 1 : 0);
            lines.push_back(line.str());
        }
    }

    /* One manifest body line per owned field, stable-sorted. Native is the
     * authority for every owner: the platform ABI keys, the 43499 backend keys
     * and the 43284 private section (S3 B4). The owner column moved for the
     * A2-4-3 platform split while the (section, key) set stayed put. */
    std::vector<std::string> schema_lines() {
        std::vector<std::string> lines;
        append_owner<ghostlock::platform::abi::Schema>(lines, "platform::abi");
        append_owner<ghostlock::backend::Cve2026_43499Schema>(
                lines, "cve_2026_43499");
        append_owner<ghostlock::backend::Cve2026_43284Schema>(
                lines, "cve_2026_43284");
        std::sort(lines.begin(), lines.end());
        return lines;
    }

    /* Field lines only, ignoring comments and blanks, stable-sorted. */
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
            std::fprintf(stderr, "manifest missing: %s\n", line.c_str());
        }
        for (const std::string &line : extra) {
            std::fprintf(stderr, "manifest extra:   %s\n", line.c_str());
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
                std::printf("profile_manifest_test: wrote %s\n", path);
                return 0;
            }
        }
        std::fprintf(stderr, "profile_manifest_test: no writable manifest path\n");
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
                     "profile_manifest_test: manifest not found in any candidate\n");
        return 1;
    }

    const std::vector<std::string> actual = field_lines(text);
    if (expected != actual) {
        std::fprintf(stderr,
                     "profile_manifest_test: manifest drift (%zu schema vs %zu file)\n",
                     expected.size(), actual.size());
        print_diff(expected, actual);
        return 1;
    }

    std::printf("profile_manifest_test: ok (%zu fields, %s)\n", expected.size(),
                found);
    return 0;
}
