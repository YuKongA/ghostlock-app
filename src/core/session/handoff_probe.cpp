#include "session/handoff_probe.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace ghostlock {
namespace {

constexpr char kKsuLogSuffix[] = ".ghostlock_ksu.log";
constexpr size_t kKsuLogPathMax = 320;

/* Module init re-enforces at the very end of kernelsu_init; a denied read or
 * the value '1' both mean enforcing here. */
bool read_enforce_enforcing() noexcept {
    const int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return true;
    char buffer[4] = {0};
    const ssize_t n = read(fd, buffer, sizeof(buffer));
    close(fd);
    return n > 0 && buffer[0] == '1';
}

}  // namespace

bool kernelsu_module_visible() noexcept {
    FILE *modules = fopen("/proc/modules", "r");
    if (!modules) return false;

    char line[256];
    bool loaded = false;
    while (fgets(line, sizeof(line), modules)) {
        char name[64];
        if (sscanf(line, "%63s", name) == 1 && strcmp(name, "kernelsu") == 0) {
            loaded = true;
            break;
        }
    }
    fclose(modules);
    return loaded;
}

bool scan_ksu_log(std::string_view path, bool &loaded, bool &failed) noexcept {
    char resolved[kKsuLogPathMax];
    snprintf(resolved, sizeof(resolved), "%.*s", (int)path.size(), path.data());
    FILE *log = fopen(resolved, "r");
    if (!log) return false;

    char line[256];
    while (fgets(line, sizeof(line), log)) {
        if (strstr(line, "[+] KernelSU module loaded") ||
                strstr(line, "[+] KernelSU already loaded")) {
            loaded = true;
        }
        if (strstr(line, "[!] KernelSU module not loaded")) failed = true;
    }
    fclose(log);
    return loaded || failed;
}

HandoffProbeResult handoff_probe_run(const HandoffPollPolicy &policy,
                                     std::string_view home_dir) noexcept {
    HandoffProbeResult result;

    for (uint32_t i = 0;
         i < policy.module_poll_attempts &&
                 !(result.module_visible = kernelsu_module_visible()); i++) {
        usleep(policy.module_poll_interval_ms * 1000U);
    }

    char log_path[kKsuLogPathMax];
    snprintf(log_path, sizeof(log_path), "%.*s/%s", (int)home_dir.size(),
             home_dir.data(), kKsuLogSuffix);

    /* untrusted_app loses /proc/modules once enforcing is restored, so poll
     * the app-readable log for the loaded-module line. */
    for (uint32_t i = 0;
         i < policy.log_poll_attempts &&
                 !(result.ksu_log_loaded || result.ksu_log_failed); i++) {
        scan_ksu_log(log_path, result.ksu_log_loaded, result.ksu_log_failed);
        if (!(result.ksu_log_loaded || result.ksu_log_failed)) {
            usleep(policy.log_poll_interval_ms * 1000U);
        }
    }

    for (uint32_t i = 0;
         result.ksu_log_loaded && !result.enforce_ok &&
                 i < policy.enforce_poll_attempts; i++) {
        result.enforce_ok = read_enforce_enforcing();
        if (!result.enforce_ok) {
            usleep(policy.enforce_poll_interval_ms * 1000U);
        }
    }

    return result;
}

}  // namespace ghostlock
