/* Host test / exporter for the build-time LKM KMI manifest (contract-design 3.17).
 *
 * lkm::kSupportedKmis is the single authority for the eight KMI delivery rows
 * //TODO(mechanical-comment-path-fix): 机械改动：注释路径修正（计划已归档，路径改写；无语义变更）。
 * (docs/archive/20261007-2237-minimal-lkm-plan.md section 2): Gradle builds one
 * "ghostlock-<label>.ko" image per row and Kotlin validates the same list, so
 * no consumer hand-copies the labels. This unit exports the table to
 *
 *   label<TAB>android_release<TAB>kmi<TAB>ko_filename
 *
 * and writes TWO byte-identical copies -- the app test resource used by the
 * native/Kotlin agreement tests and the profile-core runtime resource the
 * Kotlin consumer parses -- exactly like profile-manifest-v3.tsv,
 * combination-manifest.tsv and vocabulary-manifest.tsv.
 *
 * android12-5.10 and android13-5.10 share kmi 5010: the LABEL is the delivery
 * identity. (android_release, kmi) is therefore the unique lookup key while the
 * artifact name follows the label, never the KMI.
 *
 * Run bare to verify every committed copy (each group must resolve to exactly
 * one path and match the export byte for byte) and the table invariants below,
 * so the export is self-proving against kSupportedKmis. Run with --write to
 * regenerate both copies (make -C src lkm-kmi-manifest). */

#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"

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
#include <utility>
#include <vector>

namespace {
    using ghostlock::backend::cve_2026_43284::lkm::kSupportedKmiCount;
    using ghostlock::backend::cve_2026_43284::lkm::kSupportedKmis;
    using ghostlock::backend::cve_2026_43284::lkm::SupportedKmi;

    /* Two destinations; only the relative form that resolves from the current
     * working directory is written (src/ under make). */
    const char *kAppResourceCandidates[] = {
        "app/src/test/resources/lkm-kmi-manifest.tsv",
        "../app/src/test/resources/lkm-kmi-manifest.tsv",
        "../../app/src/test/resources/lkm-kmi-manifest.tsv",
    };
    const char *kRuntimeResourceCandidates[] = {
        "profile-core/src/main/resources/lkm-kmi-manifest.tsv",
        "../profile-core/src/main/resources/lkm-kmi-manifest.tsv",
        "../../profile-core/src/main/resources/lkm-kmi-manifest.tsv",
    };

    //TODO(mechanical-artifact-string-fix): 机械改动：注释/表头字符串中的计划名改为归档路径。
    // 无法用「注释套住旧码」处理（这是被 Gradle/Kotlin 消费的产物表头文本，不是可注释的语句），
    // 故按要求以独立 //TODO 标注；改后必须重导两份 lkm-kmi-manifest.tsv。
    std::string manifest_header() {
        return "# GhostLock LKM KMI manifest (contract-design 3.17 / docs/archive/20261007-2237-minimal-lkm-plan.md section 2).\n"
               "# Authoritative native export of lkm::kSupportedKmis; regenerate with:\n"
               "#   make -C src lkm-kmi-manifest\n"
               "# Columns: label<TAB>android_release<TAB>kmi<TAB>ko_filename\n"
               "# android12-5.10 and android13-5.10 share kmi 5010: the label is the\n"
               "# delivery identity and the file name follows the label, never the KMI.\n";
    }

    std::string expected_label(const SupportedKmi &entry) {
        std::string out = "android";
        out += std::to_string(static_cast<unsigned>(entry.android_release));
        out += '-';
        out += std::to_string(static_cast<unsigned>(entry.kernel_major));
        out += '.';
        out += std::to_string(static_cast<unsigned>(entry.kernel_minor));
        return out;
    }

    /* The exact export text, derived row for row from kSupportedKmis. */
    std::string export_text() {
        std::string text = manifest_header();
        for (const SupportedKmi &entry : kSupportedKmis) {
            text += entry.label;
            text += '\t';
            text += std::to_string(static_cast<unsigned>(entry.android_release));
            text += '\t';
            text += std::to_string(static_cast<unsigned>(entry.kmi));
            text += '\t';
            text += entry.ko_filename;
            text += '\n';
        }
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

    /* Writes every candidate whose parent directory exists, requiring each
     * group to be written exactly once. */
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
            std::printf("lkm_kmi_manifest_test: wrote %s\n", path.c_str());
            written++;
        }
        return written;
    }

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

    /* Table invariants: one authority row per delivery target, with the label
     * and the artifact name derived from the release and the geometry. */
    void check_table() {
        if (kSupportedKmiCount != 8U || std::size(kSupportedKmis) != kSupportedKmiCount) {
            std::fprintf(stderr, "lkm_kmi_manifest_test: expected 8 rows\n");
            std::exit(1);
        }
        std::set<std::string> labels;
        std::set<std::string> keys;
        std::size_t shared_5010 = 0;
        for (const SupportedKmi &entry : kSupportedKmis) {
            if (entry.android_release == 0U) {
                std::fprintf(stderr, "lkm_kmi_manifest_test: row without android release\n");
                std::exit(1);
            }
            const std::uint32_t kmi =
                    static_cast<std::uint32_t>(entry.kernel_major) * 1000U +
                    static_cast<std::uint32_t>(entry.kernel_minor);
            if (kmi != entry.kmi) {
                std::fprintf(stderr, "lkm_kmi_manifest_test: kmi mismatch for %.*s\n",
                             static_cast<int>(entry.label.size()), entry.label.data());
                std::exit(1);
            }
            if (entry.label != expected_label(entry)) {
                std::fprintf(stderr, "lkm_kmi_manifest_test: label mismatch for %.*s\n",
                             static_cast<int>(entry.label.size()), entry.label.data());
                std::exit(1);
            }
            const std::string ko = "ghostlock-" + std::string(entry.label) + ".ko";
            if (entry.ko_filename != ko) {
                std::fprintf(stderr,
                             "lkm_kmi_manifest_test: ko_filename mismatch for %.*s\n",
                             static_cast<int>(entry.label.size()), entry.label.data());
                std::exit(1);
            }
            if (!labels.insert(std::string(entry.label)).second) {
                std::fprintf(stderr, "lkm_kmi_manifest_test: duplicate label %.*s\n",
                             static_cast<int>(entry.label.size()), entry.label.data());
                std::exit(1);
            }
            std::string key = std::to_string(static_cast<unsigned>(entry.android_release));
            key += ':';
            key += std::to_string(static_cast<unsigned>(entry.kmi));
            if (!keys.insert(key).second) {
                std::fprintf(stderr, "lkm_kmi_manifest_test: duplicate key %s\n",
                             key.c_str());
                std::exit(1);
            }
            if (entry.kmi == 5010U) shared_5010++;
        }
        /* The two 5.10 rows share the KMI but stay distinct delivery targets. */
        if (shared_5010 != 2U) {
            std::fprintf(stderr,
                         "lkm_kmi_manifest_test: expected two rows sharing kmi 5010\n");
            std::exit(1);
        }
    }
} // namespace

int main(int argc, char **argv) {
    bool write = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--write") write = true;
    }

    check_table();
    const std::string expected = export_text();

    if (write) {
        const int app = write_group(kAppResourceCandidates,
                                    std::size(kAppResourceCandidates), expected);
        const int runtime = write_group(kRuntimeResourceCandidates,
                                        std::size(kRuntimeResourceCandidates), expected);
        if (app != 1 || runtime != 1) {
            std::fprintf(stderr,
                         "lkm_kmi_manifest_test: expected one writable app and "
                         "runtime resource (app=%d runtime=%d)\n",
                         app, runtime);
            return 1;
        }
        return 0;
    }

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
                         "lkm_kmi_manifest_test: expected exactly one %s manifest "
                         "(%d found)\n",
                         group.label, hits);
            return 1;
        }
        if (text != expected) {
            std::fprintf(stderr,
                         "lkm_kmi_manifest_test: %s manifest drift (%s), first "
                         "differing line %zu\n",
                         group.label, found, first_differing_line(text, expected));
            return 1;
        }
        std::printf("lkm_kmi_manifest_test: %s ok (%s)\n", group.label, found);
    }

    std::printf("lkm_kmi_manifest_test: ok (%zu rows, both copies)\n",
                kSupportedKmiCount);
    return 0;
}
