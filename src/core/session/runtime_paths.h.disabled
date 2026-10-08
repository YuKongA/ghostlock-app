#ifndef GHOSTLOCK_RUNTIME_PATHS_H
#define GHOSTLOCK_RUNTIME_PATHS_H

/* Host-safe runtime path normalization shared by RuntimeConfig.
 *
 * Only <string>/<string_view> are used, so the exact legacy truncation and
 * trailing-slash rules can be locked by host fixed-vector tests
 * (tests/runtime_paths_test.cpp). */

#include <cstddef>

#include <string>
#include <string_view>

namespace ghostlock::config {
    /* Capacities match the historical fixed buffers (content bytes + NUL). */
    inline constexpr size_t kHomeDirCapacity = 256;
    inline constexpr size_t kRootScriptPathCapacity = 300;
    /* CVE-2026-43284 convention module path capacity (same class as the root
     * script path). */
    inline constexpr size_t kHelperModulePathCapacity = 300;

    /* Copy at most capacity-1 bytes (the previous snprintf truncation), then drop
 * trailing slashes but never the root slash itself. */
    [[nodiscard]] inline std::string normalize_home_dir(std::string_view home) {
        const size_t limit = kHomeDirCapacity - 1;
        std::string result(
            home.substr(0, home.size() < limit ? home.size() : limit));
        size_t length = result.size();
        while (length > 1 && result[length - 1] == '/') {
            --length;
        }
        result.resize(length);
        return result;
    }

    /* home_dir + "/.ghostlock_root.sh", truncated to the root-script capacity. */
    [[nodiscard]] inline std::string root_script_file(std::string_view home_dir) {
        std::string result(home_dir);
        result += "/.ghostlock_root.sh";
        if (result.size() > kRootScriptPathCapacity - 1) {
            result.resize(kRootScriptPathCapacity - 1);
        }
        return result;
    }

    /* home_dir + "/helper.ko", truncated to the helper-module capacity. This is
     * the convention source for the CVE-2026-43284 backend's kernel module; a
     * future backend.cve_2026_43284.module_path profile field can replace it
     * without touching the wire. */
    [[nodiscard]] inline std::string helper_module_file(std::string_view home_dir) {
        std::string result(home_dir);
        result += "/helper.ko";
        if (result.size() > kHelperModulePathCapacity - 1) {
            result.resize(kHelperModulePathCapacity - 1);
        }
        return result;
    }
} // namespace ghostlock::config

#endif
