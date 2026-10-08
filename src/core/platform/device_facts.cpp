/* Platform device/firmware fact collection (B5-7).
 *
 * collect_device_facts() and the f4c50a4 heuristic are portable and host
 * testable. real_device_probe() is the only device-touching code in this unit
 * and is compiled for __linux__ only; everywhere else it returns an all-null
 * surface so nothing links a Linux syscall. */

#include "platform/device_facts.hpp"

#include <cstring>
#include <string_view>

namespace ghostlock::platform {

    bool proc_version_indicates_fixed(std::string_view proc_version) noexcept {
        /* The mainline fix commit is f4c50a4; a build that carries the short
         * hash in /proc/version is treated as already patched. This is a
         * best-effort marker, not a per-KMI detector. */
        return proc_version.find("f4c50a4") != std::string_view::npos;
    }

    DeviceFactError collect_device_facts(const DeviceProbeOps &ops,
                                         DeviceFacts &out) noexcept {
        out = DeviceFacts{};
        if (!ops.available()) {
            return DeviceFactError::Unavailable;
        }

        const long release_len = ops.read_release(ops.ctx, out.release.value.data(),
                                                  out.release.value.size());
        if (release_len <= 0) {
            return DeviceFactError::ReleaseMissing;
        }
        out.release_present = true;

        /* /proc/version is denied to untrusted_app on many devices
         * (tcontext=proc_version). Record the unknown instead of failing: the
         * fix marker (has_f4c50a4) stays false so an unread marker can never
         * fire PatchedKernel, and preempt stays unknown (preempt_known()==false)
         * for the vermagic precheck. */
        const long version_len = ops.read_proc_version(ops.ctx, out.proc_version.value.data(),
                                                       out.proc_version.value.size());
        if (version_len > 0) {
            out.proc_version_present = true;
            out.has_f4c50a4 = proc_version_indicates_fixed(out.proc_version.view());
        } else {
            out.degraded = out.degraded | DeviceFactDegraded::ProcVersion;
        }

        /* /sys/fs/selinux/enforce is denied to untrusted_app too. Unknown is not
         * "permissive": selinux_enforce stays 0 and readable stays false. */
        const int enforce = ops.read_selinux_enforce(ops.ctx);
        if (enforce >= 0) {
            out.selinux_enforce_readable = true;
            out.selinux_enforce = enforce;
        } else {
            out.degraded = out.degraded | DeviceFactDegraded::Selinux;
        }

        /* crash_dump64 is patch #1's target, so its EXISTENCE stays mandatory;
         * only the ls -lZ label degrades to unknown. */
        if (!ops.file_fact(ops.ctx, kCrashDump64Path, out.crash_dump)) {
            return DeviceFactError::CrashDumpMissing;
        }
        if (!out.crash_dump.exists) {
            return DeviceFactError::CrashDumpMissing;
        }
        if (!out.crash_dump.label_known) {
            out.degraded = out.degraded | DeviceFactDegraded::CrashDumpLabel;
        }

        /* An empty list is allowed: the production carrier policy has a default
         * fallback and the chain fails closed if the chosen carrier is
         * unusable. */
        const std::size_t candidates =
                ops.list_vendor_candidates(ops.ctx, out.vendor_candidates.data(),
                                           out.vendor_candidates.size());
        out.vendor_candidate_count = candidates < out.vendor_candidates.size()
                                             ? candidates
                                             : out.vendor_candidates.size();
        if (out.vendor_candidate_count == 0U) {
            out.degraded = out.degraded | DeviceFactDegraded::VendorCandidates;
        }

        out.symbols.selinux_state = ops.symbol_present(ops.ctx, "selinux_state");
        /* Symbol absence is a recorded fact, never fatal (same policy as the
         * Defex symbols below). An unprivileged /proc/kallsyms hides every
         * symbol, so a missing selinux_state usually means a restricted table
         * rather than a kernel property; and SELinux can be disabled via LKM,
         * so native preconditions do not require the symbol. */
        out.symbols.kallsyms_restricted = !out.symbols.selinux_state;
        return DeviceFactError::None;
    }

    std::size_t format_device_fact_degraded(DeviceFactDegraded bits, char *out,
                                            std::size_t capacity) noexcept {
        if (out == nullptr || capacity == 0U) {
            return 0U;
        }
        struct Entry final {
            DeviceFactDegraded bit;
            std::string_view name;
        };
        constexpr Entry kEntries[] = {
            {DeviceFactDegraded::ProcVersion, "proc_version"},
            {DeviceFactDegraded::Selinux, "selinux"},
            {DeviceFactDegraded::VendorCandidates, "vendor"},
            {DeviceFactDegraded::CrashDumpLabel, "crash_dump_label"},
        };
        std::size_t written = 0U;
        bool first = true;
        for (const Entry &entry : kEntries) {
            if (static_cast<std::uint8_t>(bits & entry.bit) == 0U) {
                continue;
            }
            if (!first) {
                if (written + 1U >= capacity) {
                    break;
                }
                out[written] = ',';
                ++written;
            }
            first = false;
            for (const char c : entry.name) {
                if (written + 1U >= capacity) {
                    out[written] = '\0';
                    return written;
                }
                out[written] = c;
                ++written;
            }
        }
        out[written] = '\0';
        return written;
    }

#if defined(__linux__)

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/xattr.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<linux/fs.h>)
#include <linux/fs.h>
#include <sys/ioctl.h>
#define GHOSTLOCK_HAS_FS_IOCTL 1
#endif
#endif

    namespace {
        constexpr const char *kVendorLib64Path = "/vendor/lib64";
        constexpr std::string_view kVendorLib64 = kVendorLib64Path;

        bool read_fd_text(const char *path, char *out, std::size_t capacity,
                          long &written) noexcept {
            const int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                return false;
            }
            std::size_t total = 0U;
            bool ok = true;
            while (total + 1U < capacity) {
                const ssize_t got = read(fd, out + total, capacity - 1U - total);
                if (got < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    ok = false;
                    break;
                }
                if (got == 0) {
                    break;
                }
                total += static_cast<std::size_t>(got);
            }
            (void)close(fd);
            if (!ok) {
                return false;
            }
            while (total > 0U) {
                const char tail = out[total - 1U];
                if (tail != '\n' && tail != '\r' && tail != ' ' && tail != '\t') {
                    break;
                }
                --total;
            }
            out[total] = '\0';
            written = static_cast<long>(total);
            return total > 0U;
        }

        long linux_read_release(void *, char *out, std::size_t capacity) noexcept {
            struct utsname name {};
            if (uname(&name) != 0) {
                return -errno;
            }
            const std::size_t len = strnlen(name.release, sizeof(name.release));
            if (len + 1U > capacity) {
                return -ENAMETOOLONG;
            }
            std::memcpy(out, name.release, len);
            out[len] = '\0';
            return static_cast<long>(len);
        }

        long linux_read_proc_version(void *, char *out, std::size_t capacity) noexcept {
            long written = 0;
            if (!read_fd_text("/proc/version", out, capacity, written)) {
                return -EIO;
            }
            return written;
        }

        int linux_read_selinux_enforce(void *) noexcept {
            char buffer[8] = {};
            long written = 0;
            if (!read_fd_text("/sys/fs/selinux/enforce", buffer, sizeof(buffer), written)) {
                return -EIO;
            }
            return buffer[0] == '1' ? 1 : 0;
        }

        bool linux_file_fact(void *, const char *path, FileFact &out) noexcept {
            out = FileFact{};
            if (path == nullptr || path[0] == '\0') {
                return false;
            }
            out.path.set(path);
            struct stat info {};
            if (stat(path, &info) != 0) {
                /* Absent is a fact, not a probe failure. */
                return true;
            }
            out.exists = true;

            char label[kDeviceLabelMax] = {};
            const ssize_t label_len =
                    getxattr(path, "security.selinux", label, sizeof(label) - 1U);
            if (label_len >= 0) {
                label[static_cast<std::size_t>(label_len)] = '\0';
                out.label.set(label);
                out.label_known = true;
            }

#if defined(GHOSTLOCK_HAS_FS_IOCTL)
            const int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                long flags = 0;
                if (ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0) {
#if defined(FS_VERITY_FL)
                    out.verity = (flags & static_cast<long>(FS_VERITY_FL)) != 0;
#endif
                }
                (void)close(fd);
            }
#endif
            return true;
        }

        std::string_view join_vendor_path(char (&buffer)[kDevicePathMax],
                                          std::string_view name) noexcept {
            std::size_t pos = 0U;
            for (const char c : kVendorLib64) {
                if (pos + 1U >= kDevicePathMax) {
                    break;
                }
                buffer[pos] = c;
                ++pos;
            }
            if (pos + 1U < kDevicePathMax) {
                buffer[pos] = '/';
                ++pos;
            }
            for (const char c : name) {
                if (pos + 1U >= kDevicePathMax) {
                    break;
                }
                buffer[pos] = c;
                ++pos;
            }
            buffer[pos] = '\0';
            return std::string_view(buffer, pos);
        }

        std::size_t linux_list_vendor_candidates(void *ctx, VendorCandidate *out,
                                                 std::size_t capacity) noexcept {
            if (out == nullptr || capacity == 0U) {
                return 0U;
            }
            DIR *dir = opendir(kVendorLib64Path);
            if (dir == nullptr) {
                return 0U;
            }
            std::size_t count = 0U;
            while (count < capacity) {
                const struct dirent *entry = readdir(dir);
                if (entry == nullptr) {
                    break;
                }
                const std::string_view name(entry->d_name);
                if (name.empty() || name == "." || name == "..") {
                    continue;
                }
                if (name.size() < 3U || name.substr(name.size() - 3U) != ".so") {
                    continue;
                }
                char path_buffer[kDevicePathMax] = {};
                const std::string_view path = join_vendor_path(path_buffer, name);
                FileFact fact{};
                if (!linux_file_fact(ctx, path_buffer, fact) || !fact.exists) {
                    continue;
                }
                out[count].path.set(path);
                out[count].label = fact.label;
                out[count].exists = true;
                out[count].vendor_file_label =
                        fact.label.view().find("vendor_file") != std::string_view::npos;
                ++count;
            }
            (void)closedir(dir);
            return count;
        }

        bool linux_symbol_present(void *, const char *symbol) noexcept {
            if (symbol == nullptr || symbol[0] == '\0') {
                return false;
            }
            std::FILE *file = std::fopen("/proc/kallsyms", "re");
            if (file == nullptr) {
                return false;
            }
            const std::size_t symbol_len = std::strlen(symbol);
            char line[256];
            bool found = false;
            while (!found && std::fgets(line, sizeof(line), file) != nullptr) {
                std::size_t i = 0U;
                while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') {
                    ++i;
                }
                while (line[i] == ' ' || line[i] == '\t') {
                    ++i;
                }
                if (line[i] == '\0') {
                    continue;
                }
                ++i; /* symbol type character */
                while (line[i] == ' ' || line[i] == '\t') {
                    ++i;
                }
                if (std::strncmp(line + i, symbol, symbol_len) == 0) {
                    const char after = line[i + symbol_len];
                    if (after == '\0' || after == ' ' || after == '\t' ||
                        after == '\n' || after == '\r') {
                        found = true;
                    }
                }
            }
            (void)std::fclose(file);
            return found;
        }
    } // namespace

    DeviceProbeOps real_device_probe() noexcept {
        DeviceProbeOps ops{};
        ops.read_release = linux_read_release;
        ops.read_proc_version = linux_read_proc_version;
        ops.read_selinux_enforce = linux_read_selinux_enforce;
        ops.file_fact = linux_file_fact;
        ops.list_vendor_candidates = linux_list_vendor_candidates;
        ops.symbol_present = linux_symbol_present;
        return ops;
    }

#else

    DeviceProbeOps real_device_probe() noexcept {
        return DeviceProbeOps{};
    }

#endif

} // namespace ghostlock::platform
