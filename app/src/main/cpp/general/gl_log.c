/** @file general/gl_log.c
 *  纯 C 实现（2026-10-18 用户裁定）。状态只有两个：导出 fd 与阈值，见头文件。
 */
#include "general/gl_log.h"

#include <fcntl.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

/* 每个 DSO 一份：本文件编进静态库后分别链进三个 JNI 库，各自拿到自己的副本。 */
static _Atomic int g_export_fd = -1;
static _Atomic int g_min_level = GL_LOG_INFO;
static _Atomic int g_timestamps = 1;
/* 启动（首次用到日志）之后的毫秒数：单调时钟，不受系统改时间影响。 */
static _Atomic long long g_start_ms = -1;

int gl_log_set_export_fd(const int fd) {
    return atomic_exchange(&g_export_fd, fd);
}

int gl_log_export_fd(void) {
    return atomic_load(&g_export_fd);
}

int gl_log_attach_fd(const int fd, const int timestamps, const gl_log_level min_level) {
    int owned = -1;
    if (fd >= 0) {
        owned = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (owned < 0) return -1;
    }
    const int previous = atomic_exchange(&g_export_fd, owned);
    if (previous >= 0) close(previous);
    atomic_store(&g_timestamps, timestamps != 0 ? 1 : 0);
    atomic_store(&g_min_level, (int) min_level);
    return owned;
}

void gl_log_set_min_level(const gl_log_level level) {
    atomic_store(&g_min_level, (int) level);
}

gl_log_level gl_log_min_level(void) {
    return (gl_log_level) atomic_load(&g_min_level);
}

void gl_log_set_timestamps(const int enabled) {
    atomic_store(&g_timestamps, enabled != 0 ? 1 : 0);
}

int gl_log_timestamps(void) {
    return atomic_load(&g_timestamps);
}

static long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long long) ts.tv_sec * 1000LL + (long long) (ts.tv_nsec / 1000000L);
}

/** 首次调用时记下起点；之后返回“启动后毫秒数”。 */
static long long elapsed_ms(void) {
    const long long now = now_ms();
    long long base = atomic_load(&g_start_ms);
    if (base < 0) {
        atomic_store(&g_start_ms, now);
        base = now;
    }
    return now - base;
}

gl_log_level gl_log_level_from_name(const char *name) {
    if (name == NULL) {
        return GL_LOG_INFO;
    }
    if (strcmp(name, "trace") == 0) { return GL_LOG_VERBOSE; }
    if (strcmp(name, "debug") == 0) { return GL_LOG_DEBUG; }
    if (strcmp(name, "info") == 0) { return GL_LOG_INFO; }
    if (strcmp(name, "warn") == 0 || strcmp(name, "warning") == 0) { return GL_LOG_WARN; }
    if (strcmp(name, "error") == 0 || strcmp(name, "critical") == 0) { return GL_LOG_ERROR; }
    return GL_LOG_INFO;
}

/* V / ~ / * / - / !（与 Kotlin 侧同一张表）。 */
static char level_mark(const gl_log_level level) {
    switch (level) {
        case GL_LOG_VERBOSE: return 'V';
        case GL_LOG_DEBUG: return '~';
        case GL_LOG_WARN: return '-';
        case GL_LOG_ERROR: return '!';
        case GL_LOG_INFO: break;
    }
    return '*';
}

/* 行首方括号里的内容：有开关时 `* 1234ms`，没有时只有级别符号。 */
static void head_into(char *out, const size_t capacity, const gl_log_level level) {
    if (atomic_load(&g_timestamps) != 0) {
        snprintf(out, capacity, "%c %lldms", level_mark(level), elapsed_ms());
    } else {
        snprintf(out, capacity, "%c", level_mark(level));
    }
}

void gl_log_write(const gl_log_level level, const char *file, const int line, const char *fmt, ...) {
    if ((int) level < atomic_load(&g_min_level)) {
        return; /* 低于阈值：不格式化、不写任何去处。 */
    }
    const char *slash = file;
    const char *cursor = file;
    for (; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            slash = cursor + 1;
        }
    }
    char body[512] = {0};
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    char full[640] = {0};
    snprintf(full, sizeof(full), "%s:%d %s", slash, line, body);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, GL_LOG_TAG, "%s", full);
    const int fd = atomic_load(&g_export_fd);
    if (fd >= 0) {
        char head[32] = {0};
        head_into(head, sizeof(head), level);
        char exported[768] = {0};
        /* 与 Kotlin 侧同一形状、窄屏友好：[<级别符号> <启动毫秒>] 正文（方括号 = native）。 */
        snprintf(exported, sizeof(exported), "[%s] %s\n", head, body);
        const size_t want = strlen(exported);
        size_t done = 0;
        while (done < want) {
            const ssize_t wrote = write(fd, exported + done, want - done);
            if (wrote <= 0) {
                break; /* 日志写失败绝不影响主流程。 */
            }
            done += (size_t) wrote;
        }
    }
#else
    {
        char head[32] = {0};
        head_into(head, sizeof(head), level);
        fprintf(stderr, "[%s] %s\n", head, full);
    }
#endif
}
