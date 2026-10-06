/* S4 P1 step 3a structural guard: the plugin host's production call sites.
 *
 * R1 (design section 4) says no plugin mapping may exist while the PI waiter is
 * alive. The runtime guard (open(WaiterAlive) refuses without a dlopen) is the
 * backstop; this test is the static half: the ONLY production call sites of the
 * host's construction, its window-gated open and its stage dispatch are the
 * ones named here. Moving a call - in particular opening the host before the
 * backend's race window - changes this test's list and fails it, so the order
 * cannot drift silently. The technique mirrors include_firewall_test.cpp.
 *
 * The API owner (plugin/host.{hpp,cpp}) and the test tree are out of scope by
 * construction: host.hpp declares WindowState/HostStage/from_document, so only
 * *other* files count as call sites.
 *
 * 3b (43499 pre_terminal) extends kWindowStateSites / kHostStageSites with
 * backend/cve_2026_43499/steps.cpp. Every entry here must be FOUND (a stale
 * allowlist entry fails just like an unexpected call site). */

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#ifndef GHOSTLOCK_WIRING_ROOT
#define GHOSTLOCK_WIRING_ROOT "."
#endif

namespace {

    struct Site final {
        const char *file;  /* path relative to src/core */
        const char *what;  /* the token that makes it a call site */
    };

    /* The frozen list of production call sites. */
    constexpr Site kCallSites[] = {
        /* Construction: composition root only (after the P1 wire gate). */
        {"main.cpp", "PluginHost::from_document"},
        /* open(WindowState): 43284 opens before its chain (no PI waiter exists
         * on that backend); 43499 opens at its post-race anchor in step 3b. */
        {"main.cpp", "WindowState::"},
        /* dispatch(HostStage): the composition seam binds the neutral thunk the
         * LKM residency window invokes; 43499's pre_terminal site lands in
         * step 3b. */
        {"backend/cve_2026_43284/execution_binding.cpp", "HostStage::"},
    };
    constexpr std::size_t kCallSiteCount = sizeof(kCallSites) / sizeof(kCallSites[0]);

    /* Files that own the API, never call sites. */
    bool is_api_owner(const std::string &rel) {
        return rel == "plugin/host.hpp" || rel == "plugin/host.cpp";
    }

    bool has_source_extension(const std::string &path) {
        const char *exts[] = {".hpp", ".cpp", ".h", ".c"};
        for (const char *ext : exts) {
            const std::size_t n = std::strlen(ext);
            if (path.size() >= n && path.compare(path.size() - n, n, ext) == 0) {
                return true;
            }
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
                if (rel == "tests") continue; /* host tests are not production */
                collect_sources(abs, rel, out);
            } else if (S_ISREG(info.st_mode) && has_source_extension(rel)) {
                out.push_back(rel);
            }
        }
        ::closedir(dir);
    }

    bool is_allowed(const std::string &file, const char *what) {
        for (std::size_t i = 0; i < kCallSiteCount; ++i) {
            if (file == kCallSites[i].file && std::strcmp(what, kCallSites[i].what) == 0) {
                return true;
            }
        }
        return false;
    }

} // namespace

int main() {
    std::string root = std::string(GHOSTLOCK_WIRING_ROOT) + "/core";
    {
        struct stat info {};
        if (!(::stat(root.c_str(), &info) == 0 && S_ISDIR(info.st_mode))) root = "core";
        if (!(::stat(root.c_str(), &info) == 0 && S_ISDIR(info.st_mode))) root = "../core";
        if (!(::stat(root.c_str(), &info) == 0 && S_ISDIR(info.st_mode))) {
            root = "../../src/core";
        }
    }
    std::printf("plugin_open_site_test: source root %s\n", root.c_str());

    std::vector<std::string> files;
    collect_sources(root, "", files);

    std::vector<bool> seen(kCallSiteCount, false);
    std::size_t hits = 0;
    std::size_t unexpected = 0;

    for (const std::string &file : files) {
        if (is_api_owner(file)) continue;
        std::ifstream in(root + "/" + file);
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            for (std::size_t i = 0; i < kCallSiteCount; ++i) {
                if (line.find(kCallSites[i].what) == std::string::npos) continue;
                ++hits;
                if (is_allowed(file, kCallSites[i].what)) {
                    seen[i] = true;
                    std::printf("  call site %-45s %s\n", file.c_str(),
                                kCallSites[i].what);
                } else {
                    ++unexpected;
                    std::printf("  UNEXPECTED %-45s %s\n", file.c_str(),
                                kCallSites[i].what);
                }
            }
        }
    }

    std::size_t stale = 0;
    for (std::size_t i = 0; i < kCallSiteCount; ++i) {
        if (!seen[i]) {
            ++stale;
            std::printf("  STALE allowlist entry: %s %s\n", kCallSites[i].file,
                        kCallSites[i].what);
        }
    }

    std::printf("plugin_open_site_test: %zu files, %zu call-site hits, %zu unexpected, "
                "%zu stale\n",
                files.size(), hits, unexpected, stale);
    if (unexpected != 0 || stale != 0) {
        std::printf("plugin_open_site_test: FAIL\n");
        return 1;
    }
    std::puts("plugin_open_site_test: ok");
    return 0;
}
