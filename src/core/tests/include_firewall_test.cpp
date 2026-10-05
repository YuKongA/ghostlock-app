/* Host include firewall for ADR-0004 R1 (A2-5-5).
 *
 * The allowed-edge graph is the only authority for layer dependencies (ADR-0004
 * R1). This test walks the production tree under src/core, reads every quoted
 * #include and rejects an edge whose target layer is forbidden for the source
 * layer. It runs for real in the native-host-tests target and prints one
 * diagnostic line per checked violation, so a regression names file, line,
 * include target and the rule that was broken.
 *
 * Known, pre-existing violations are listed in kWhitelist with the owning batch
 * and the reason they are still there (pending cleanup). A non-whitelisted
 * violation fails the test; a stale whitelist entry (the include is gone) also
 * fails, so the ledger cannot silently rot.
 *
 * R1 table enforced here:
 *   contract/memory/session/profile/support/plugin -> must not include
 *        backend/, pipeline/, platform/, terminal/
 *   backend -> must not include pipeline/
 *   platform -> must not include backend/, pipeline/, terminal/
 * plugin -> contract/memory/support stays allowed; platform -> plugin
 * and backend -> platform/terminal/profile are allowed by R1.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#ifndef GHOSTLOCK_FIREWALL_ROOT
#define GHOSTLOCK_FIREWALL_ROOT "."
#endif

namespace {

    constexpr std::size_t kTrackedLayerCount = 8;

    struct Rule final {
        const char *source_layer;
        const char *forbidden_layers; /* comma-separated target layers */
        const char *note;
    };

    /* One rule per restricted source layer. The forbidden list is deliberately
     * the exact A2-5-5 brief, a subset of the full R1 allowed-edge graph. */
    constexpr Rule kRules[] = {
        {"contract", "backend,pipeline,platform,terminal",
         "contract -> support/memory only"},
        {"memory", "backend,pipeline,platform,terminal",
         "memory -> support only"},
        {"session", "backend,pipeline,platform,terminal",
         "session -> contract/memory/support"},
        {"profile", "backend,pipeline,platform,terminal",
         "neutral container: no reverse dependency on any layer"},
        {"support", "backend,pipeline,platform,terminal",
         "neutral leaf: support is depended on, never depends upward"},
        {"plugin", "backend,pipeline,platform,terminal",
         "plugin facility: plugin -> contract/memory/support"},
        {"backend", "pipeline",
         "backend implements the contract; identity is declared in backend and "
         "pipeline consumes it one way"},
        {"platform", "backend,pipeline,terminal",
         "platform -> contract/memory/profile/plugin/support"},
    };
    static_assert(sizeof(kRules) / sizeof(kRules[0]) == kTrackedLayerCount,
                  "A2-5-5: the firewall rule table must cover all 8 restricted source layers");

    struct Exception final {
        const char *file;   /* path relative to src/core */
        const char *target; /* exact #include target text */
        const char *todo;   /* owning batch / pending cleanup */
    };

    /* Explicit whitelist ledger. Every entry is a real current violation with a
     * named follow-up batch; nothing is passed silently. */
    constexpr Exception kWhitelist[] = {
        {"support/util.cpp", "backend/cve_2026_43499_state.hpp",
         "A2-5-4: per-file include decoupling interim; util still reaches 43499 state"},
        {"support/util.cpp", "backend/cve_2026_43499/route/route_policy.hpp",
         "A2-5-2/A2-5-4: spray helpers still name the 43499 route policy"},
        {"support/util.cpp", "backend/cve_2026_43499/backend_profile/accessors.hpp",
         "A2-4-4: 43499 slide accessors moved out of profile; util still reads them "
         "until the A2-5-4 include decoupling lands"},
        {"support/util.cpp", "backend/cve_2026_43499/leak/address_discovery.h",
         "A3-2: the one TU that owns the frozen kernelsnitch provider includes its "
         "leak-module adapter; the spray/leak ownership move (F15) removes it"},
    };
    static_assert(sizeof(kWhitelist) / sizeof(kWhitelist[0]) == 4,
                  "A2-5-5: whitelist ledger changed; update the list and its count together");

    bool has_source_extension(const std::string &path) {
        const char *exts[] = {".h", ".hpp", ".cpp", ".c", ".cc"};
        for (const char *ext : exts) {
            const std::size_t n = std::strlen(ext);
            if (path.size() >= n && path.compare(path.size() - n, n, ext) == 0) {
                return true;
            }
        }
        return false;
    }

    std::string top_layer(const std::string &rel) {
        const std::size_t slash = rel.find('/');
        return slash == std::string::npos ? rel : rel.substr(0, slash);
    }

    std::string normalize_path(const std::string &in) {
        std::vector<std::string> parts;
        std::size_t i = 0;
        while (i < in.size()) {
            const std::size_t j = in.find('/', i);
            const std::string part =
                    in.substr(i, j == std::string::npos ? std::string::npos : j - i);
            i = (j == std::string::npos) ? in.size() : j + 1;
            if (part.empty() || part == ".") continue;
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
                continue;
            }
            parts.push_back(part);
        }
        std::string out;
        for (const std::string &part : parts) {
            if (!out.empty()) out += '/';
            out += part;
        }
        return out;
    }

    /* Quoted includes resolve against the include root (src/core), except the
     * file-relative "../" form used by a few test files. Both are normalised to
     * a path relative to src/core. */
    std::string resolve_target(const std::string &file_rel, const std::string &target) {
        if (target.rfind("../", 0) == 0) {
            const std::size_t slash = file_rel.find_last_of('/');
            const std::string dir = slash == std::string::npos ? "" : file_rel.substr(0, slash);
            return normalize_path(dir + "/" + target);
        }
        return normalize_path(target);
    }

    const Rule *rule_for(const std::string &layer) {
        for (const Rule &rule : kRules) {
            if (layer == rule.source_layer) return &rule;
        }
        return nullptr;
    }

    bool layer_forbidden(const Rule &rule, const std::string &target_layer) {
        const std::string list = rule.forbidden_layers;
        std::size_t i = 0;
        while (i <= list.size()) {
            const std::size_t j = list.find(',', i);
            const std::string item =
                    list.substr(i, j == std::string::npos ? std::string::npos : j - i);
            if (item == target_layer) return true;
            if (j == std::string::npos) break;
            i = j + 1;
        }
        return false;
    }

    void collect_sources(const std::string &abs_dir, const std::string &rel_dir,
                         std::vector<std::string> &out) {
        DIR *dir = ::opendir(abs_dir.c_str());
        if (dir == nullptr) return;
        while (dirent *entry = ::readdir(dir)) {
            if (std::strcmp(entry->d_name, ".") == 0 ||
                std::strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            const std::string rel =
                    rel_dir.empty() ? entry->d_name : rel_dir + "/" + entry->d_name;
            const std::string abs = abs_dir + "/" + entry->d_name;
            struct stat info {};
            if (::stat(abs.c_str(), &info) != 0) continue;
            if (S_ISDIR(info.st_mode)) {
                collect_sources(abs, rel, out);
            } else if (S_ISREG(info.st_mode) && has_source_extension(rel)) {
                out.push_back(rel);
            }
        }
        ::closedir(dir);
    }

} // namespace

int32_t main(void) {
    std::string root = std::string(GHOSTLOCK_FIREWALL_ROOT) + "/core";
    {
        struct stat info {};
        if (!(::stat(root.c_str(), &info) == 0 && S_ISDIR(info.st_mode))) {
            root = "core";
        }
        if (!(::stat(root.c_str(), &info) == 0 && S_ISDIR(info.st_mode))) {
            root = "../core";
        }
        if (!(::stat(root.c_str(), &info) == 0 && S_ISDIR(info.st_mode))) {
            root = "../../src/core";
        }
    }

    std::printf("include_firewall_test: source root %s\n", root.c_str());
    std::puts("R1 include firewall rules:");
    for (const Rule &rule : kRules) {
        std::printf("  %-9s must not include { %s }  [%s]\n", rule.source_layer,
                    rule.forbidden_layers, rule.note);
    }

    std::vector<std::string> files;
    collect_sources(root, "", files);

    const std::size_t kExceptionCount = sizeof(kWhitelist) / sizeof(kWhitelist[0]);
    std::size_t files_scanned = 0;
    std::size_t layer_edges = 0;
    std::size_t whitelisted_seen = 0;
    std::size_t unexpected = 0;
    std::vector<bool> used(kExceptionCount, false);

    for (const std::string &file_rel : files) {
        const std::string layer = top_layer(file_rel);
        const Rule *rule = rule_for(layer);
        if (rule == nullptr) continue;
        ++files_scanned;

        std::ifstream in(root + "/" + file_rel);
        if (!in) continue;
        std::string line;
        int line_number = 0;
        while (std::getline(in, line)) {
            ++line_number;
            const std::size_t hash = line.find('#');
            if (hash == std::string::npos) continue;
            const std::size_t inc = line.find("include", hash);
            if (inc == std::string::npos) continue;
            const std::size_t quote = line.find('"', inc);
            if (quote == std::string::npos) continue;
            const std::size_t end = line.find('"', quote + 1);
            if (end == std::string::npos) continue;
            const std::string target = line.substr(quote + 1, end - quote - 1);
            const std::string resolved = resolve_target(file_rel, target);
            const std::string target_layer = top_layer(resolved);
            if (!layer_forbidden(*rule, target_layer)) continue;
            ++layer_edges;

            std::size_t match_index = kExceptionCount;
            for (std::size_t i = 0; i < kExceptionCount; ++i) {
                if (file_rel == kWhitelist[i].file && target == kWhitelist[i].target) {
                    match_index = i;
                    break;
                }
            }
            if (match_index != kExceptionCount) {
                used[match_index] = true;
                ++whitelisted_seen;
                std::printf("  WHITELIST %s:%d  #include \"%s\"  (source %s -> target %s)\n",
                            file_rel.c_str(), line_number, target.c_str(),
                            rule->source_layer, target_layer.c_str());
                std::printf("            pending: %s\n", kWhitelist[match_index].todo);
            } else {
                ++unexpected;
                std::printf(
                        "  VIOLATION %s:%d  #include \"%s\"  (%s -> %s) breaks rule: %s\n",
                        file_rel.c_str(), line_number, target.c_str(), rule->source_layer,
                        target_layer.c_str(), rule->note);
            }
        }
    }

    std::size_t stale = 0;
    for (std::size_t i = 0; i < kExceptionCount; ++i) {
        if (used[i]) continue;
        ++stale;
        std::printf("  STALE WHITELIST %s -> %s  (%s): include no longer present; remove it\n",
                    kWhitelist[i].file, kWhitelist[i].target, kWhitelist[i].todo);
    }

    std::printf(
            "include_firewall_test: %zu files, %zu forbidden-layer edges, %zu whitelisted, "
            "%zu unexpected, %zu stale\n",
            files_scanned, layer_edges, whitelisted_seen, unexpected, stale);

    if (unexpected != 0 || stale != 0) {
        std::puts("include_firewall_test: FAIL");
        return 1;
    }
    std::puts("include_firewall_test: ok");
    return 0;
}
