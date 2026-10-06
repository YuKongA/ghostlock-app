#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* Neutral process, thread, syscall and byte-writing primitives shared by every
 * layer. The 43499 heap prepare / leak / spray block moved verbatim to
 * backend/cve_2026_43499/spray.cpp (F15 ownership move), so support no longer
 * depends on the backend layer and the R1 include firewall runs with an empty
 * whitelist ledger. Only definitions with no backend, route or kernel-state
 * dependency remain here. */
#include "memory/constants.hpp"
#include "session/runtime_config.h"
#include "support/decls.hpp"
#include "support/log.hpp"
#include "support/native_resource.hpp"

#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <signal.h>

namespace ghostlock::support {
    /* Persist stage-boundary diagnostics before a kernel panic or filesystem
     * rollback can discard buffered lines. Unsupported fsync targets are ignored. */
    void log_sync(void) {
        fflush(stdout);
        (void) fsync(STDOUT_FILENO);
    }

    [[noreturn]] void fail_stop_dirty_race(const char *reason,
                                           int32_t error_number) noexcept {
        pr_error("terminal dirty race: %s errno=%d; stopping native process\n",
                 reason ? reason : "unknown", error_number);
        log_sync();
        syscall(SYS_exit_group, 70);
        __builtin_unreachable();
    }

    void read_first_line(const char *path, char *buf, size_t len) {
        if (!len) {
            return;
        }
        snprintf(buf, len, "unreadable");
        support::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) {
            return;
        }
        const ssize_t n = read(fd.get(), buf, len - 1);
        const int32_t saved_errno = errno;
        fd.reset();
        if (n <= 0) {
            errno = saved_errno;
            snprintf(buf, len, "unreadable");
            return;
        }
        buf[n] = 0;
        buf[strcspn(buf, "\r\n")] = 0;
    }

    void disable_rseq_for_thread(void) {
        return;
    }

    long futex_op(uint32_t *uaddr, int32_t op, uint32_t val,
                  const void *timeout_or_value, uint32_t *uaddr2,
                  uint32_t val3) {
        return syscall(SYS_futex, uaddr, op, val, timeout_or_value, uaddr2, val3);
    }

    long sched_setattr_tid(int32_t tid, int32_t nice_value) {
        memory::local_sched_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.sched_policy = 3; /* SCHED_BATCH — nice change triggers PI walk (pi=true) */
        attr.sched_nice = nice_value;
        errno = 0;
        long ret = syscall(274, tid, &attr, 0);
        if (ret != 0) {
            pr_error("sched_setattr(%d,BATCH,nice=%d) ret=%ld errno=%d\n", tid, nice_value, ret, errno);
        }
        return ret;
    }

    void put64(unsigned char *p, size_t off, uint64_t value) {
        memcpy(p + off, &value, sizeof(value));
    }

    void put32(unsigned char *p, size_t off, uint32_t value) {
        memcpy(p + off, &value, sizeof(value));
    }

    pid_t clone_child(void) {
        pid_t child = static_cast<pid_t>(SYSCHK(syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0)));
        if (child == 0) {
            SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
            if (getppid() == 1) {
                _exit(0);
            }
            memory::pin_to_core(static_cast<size_t>(config::runtime_config_snapshot().main_cpu));
            for (;;) {
                pause();
            }
        }
        return child;
    }

    int32_t open_memfd(pid_t child) {
        std::array < char, 64 > path{};
        snprintf(path.data(), path.size(), "/proc/%d/mem", child);
        return SYSCHK(open(path.data(), O_RDONLY));
    }

    void kill_child(pid_t child) {
        if (child <= 0) {
            return;
        }
        SYSCHK(kill(child, SIGKILL));
        SYSCHK(waitpid(child, nullptr, 0));
    }

    int32_t clone_memfd(void) {
        pid_t child = clone_child();
        int32_t fd = open_memfd(child);
        kill_child(child);
        return fd;
    }
} // namespace ghostlock::support
