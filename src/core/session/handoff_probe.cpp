#include "session/handoff_probe.hpp"

#include "support/native_resource.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <cstdio>
#include <string>
#include <string_view>
#include <unistd.h>

namespace ghostlock::session {
    namespace {
        constexpr size_t kKsuLogPathMax = 320;

        /* Read a line-delimited file, invoking `fn` for each line (newline stripped).
 * Returns false when the file cannot be opened. */
        template<typename Fn>
        bool for_each_line(const char *path, Fn &&fn) noexcept {
            support::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
            if (!fd.valid()) return false;

            std::string data;
            std::array < char, 4096 > chunk{};
            for (;;) {
                const ssize_t n = read(fd.get(), chunk.data(), chunk.size());
                if (n > 0) {
                    data.append(chunk.data(), static_cast<size_t>(n));
                    continue;
                }
                if (n < 0 && errno == EINTR) continue;
                break;
            }

            size_t start = 0;
            while (start < data.size()) {
                const size_t newline = data.find('\n', start);
                const size_t end = newline == std::string::npos ? data.size() : newline;
                fn(std::string_view(data).substr(start, end - start));
                if (newline == std::string::npos) break;
                start = newline + 1;
            }
            return true;
        }

        /* Module init re-enforces at the very end of kernelsu_init; a denied read or
 * the value '1' both mean enforcing here. */
        bool read_enforce_enforcing() noexcept {
            support::UniqueFd fd(open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC));
            if (!fd.valid()) return true;
            std::array < char, 4 > buffer{};
            const ssize_t n = read(fd.get(), buffer.data(), buffer.size());
            return n > 0 && buffer[0] == '1';
        }
    } // namespace

    bool kernelsu_module_visible() noexcept {
        bool loaded = false;
        for_each_line("/proc/modules", [&loaded](std::string_view line) {
            const size_t end = line.find_first_of(" \t");
            if (line.substr(0, end) == "kernelsu") loaded = true;
        });
        return loaded;
    }

    bool scan_ksu_log(std::string_view path, bool &loaded, bool &failed) noexcept {
        std::array < char, kKsuLogPathMax > resolved{};
        snprintf(resolved.data(), resolved.size(), "%.*s", static_cast<int32_t>(path.size()),
                 path.data());
        const bool opened = for_each_line(resolved.data(), [&](std::string_view line) {
            if (line.contains("[+] KernelSU module loaded") ||
                line.contains("[+] KernelSU already loaded")) {
                loaded = true;
            }
            if (line.contains("[!] KernelSU module not loaded")) {
                failed = true;
            }
        });
        return opened ? (loaded || failed) : false;
    }

    HandoffProbeResult handoff_probe_run(const HandoffPollPolicy &policy,
                                         std::string_view ksu_log_path) noexcept {
        HandoffProbeResult result;

        for (uint32_t i = 0;
             i < policy.module_poll_attempts &&
             !(result.module_visible = kernelsu_module_visible()); i++) {
            usleep(policy.module_poll_interval_ms * 1000U);
        }

        /* untrusted_app loses /proc/modules once enforcing is restored, so poll
     * the app-readable log for the loaded-module line. The path is per-run
     * (U01-S14): the caller passes the resolved file so a previous run's
     * markers can never satisfy this poll. */
        for (uint32_t i = 0;
             i < policy.log_poll_attempts &&
             !(result.ksu_log_loaded || result.ksu_log_failed); i++) {
            scan_ksu_log(ksu_log_path, result.ksu_log_loaded, result.ksu_log_failed);
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
} // namespace ghostlock::session
