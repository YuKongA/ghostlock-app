#pragma once

/* v12.6: panic-proof native-side log tee — RAW FD EDITION.
 *
 * History: the FILE*-based tee (fdopen + _IONBF + vfprintf) silently lost
 * every log line on this unit — the 64-byte probe written with raw
 * write(2) always landed (filesDir AND FUSE), yet not a single vfprintf
 * line ever appeared in the rescued archives (20261004-125637/141822:
 * 64/65 bytes after minutes-long runs). The stdio layer is the only
 * difference between the working probe and the failing writes, so it is
 * gone: the tee is now a raw fd and every line is one ::write(2). */

#include <android/log.h>
#include <sys/stat.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace ghostlock::support {

inline int g_osync_fd = -1;

inline int osync_try_open(const char *dir) {
    ::mkdir(dir, 0755);
    std::string path = std::string(dir) + "/native-osync.log";
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    char probe[64];
    memset(probe, '\n', sizeof(probe));
    if (::write(fd, probe, sizeof(probe)) != sizeof(probe)) {
        ::close(fd);
        return -1;
    }
    struct stat st = {};
    if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(probe))) {
        ::close(fd);
        return -1;
    }
    return fd;
}

inline void osync_log_init(const char *preferred_dir = nullptr) {
    if (g_osync_fd >= 0) return;
    /* filesDir first (ext4, app-owned, survives reboots; OsyncRescue moves
     * it into the export area at every app start), then shell-readable tmp,
     * then the FUSE export dir as a same-shot convenience. */
    static const char *const kCandidates[] = {
            "/data/data/com.ghostlock.app/files",
            "/data/local/tmp",
            "/storage/emulated/0/Android/data/com.ghostlock.app/files",
    };
    for (const char *dir : kCandidates) {
        g_osync_fd = osync_try_open(dir);
        if (g_osync_fd >= 0) return;
    }
    if (preferred_dir && preferred_dir[0]) {
        g_osync_fd = osync_try_open(preferred_dir);
    }
}

/* v12.6: one ::write(2) per line onto the raw fd — no stdio, no buffering,
 * no silent drop layer. */
inline void osync_write_line(const char *buf, size_t len) {
    if (g_osync_fd < 0 || !buf || len == 0) return;
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(g_osync_fd, buf + off, len - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break; /* the tee is best-effort; never block logging */
        }
        off += static_cast<size_t>(n);
    }
}

inline void pr_emit(const char *fmt, ...) {
    char stackbuf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = static_cast<size_t>(n) < sizeof(stackbuf)
                         ? static_cast<size_t>(n)
                         : sizeof(stackbuf) - 1;
        osync_write_line(stackbuf, len);
    }
    {
        va_list ap2;
        va_start(ap2, fmt);
        std::vfprintf(stdout, fmt, ap2);
        std::fflush(stdout);
        va_end(ap2);
    }
}

inline void pr_emit_logcat(int prio, const char *fmt, ...) {
    char stackbuf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = static_cast<size_t>(n) < sizeof(stackbuf)
                         ? static_cast<size_t>(n)
                         : sizeof(stackbuf) - 1;
        osync_write_line(stackbuf, len);
    }
    {
        va_list ap2;
        va_start(ap2, fmt);
        __android_log_vprint(prio, "google_poc_app", fmt, ap2);
        va_end(ap2);
    }
}

} // namespace ghostlock::support
