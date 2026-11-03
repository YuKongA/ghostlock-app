/**
 * @file common/gl_log.c
 * @brief Implement the shared C logger for JNI libraries and standalone executables.
 * @note Each linked DSO owns independent fd, severity, and elapsed-time state.
 */
#include "common/gl_log.h"

#include <fcntl.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

/* Each DSO or standalone executable owns its logger state. */
static _Atomic int g_export_fd = -1;
static _Atomic int g_min_level = GL_LOG_INFO;
/* 启动（首次用到日志）之后的毫秒数：单调时钟，不受系统改时间影响。 */
static _Atomic long long g_start_ms = -1;

int gl_log_set_export_fd(const int fd) {
    return atomic_exchange(&g_export_fd, fd);
}

int gl_log_export_fd(void) {
    return atomic_load(&g_export_fd);
}

int gl_log_attach_fd(const int fd, const int timestamps, const gl_log_level min_level) {
    (void)timestamps;
    int owned = -1;
    if (fd >= 0) {
        owned = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (owned < 0) return -1;
    }
    const int previous = atomic_exchange(&g_export_fd, owned);
    if (previous >= 0) close(previous);
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
    (void)enabled;
}

int gl_log_timestamps(void) {
    return 1;
}

static long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long long) ts.tv_sec * 1000LL + (long long) (ts.tv_nsec / 1000000L);
}

/**
 * @brief Return monotonic milliseconds since the first logger call.
 * @return Nonnegative elapsed milliseconds; zero when the clock is unavailable.
 */
static long long elapsed_ms(void) {
    const long long now = now_ms();
    long long base = atomic_load(&g_start_ms);
    if (base < 0) {
        long long expected = -1;
        if (atomic_compare_exchange_strong(&g_start_ms, &expected, now)) {
            base = now;
        } else {
            base = expected;
        }
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

/**
 * @brief Return the canonical one-letter marker for a log level.
 * @param level Project severity.
 * @return V, D, I, W, or E; invalid values map to I.
 */
static char level_mark(const gl_log_level level) {
    switch (level) {
        case GL_LOG_VERBOSE: return 'V';
        case GL_LOG_DEBUG: return 'D';
        case GL_LOG_INFO: return 'I';
        case GL_LOG_WARN: return 'W';
        case GL_LOG_ERROR: return 'E';
    }
    return 'I';
}

/**
 * @brief Map a project severity to Android's matching log priority.
 * @param level Project severity.
 * @return Matching Android priority, with invalid values mapped to INFO.
 */
#if defined(__ANDROID__)
static int android_priority(const gl_log_level level) {
    switch (level) {
        case GL_LOG_VERBOSE: return ANDROID_LOG_VERBOSE;
        case GL_LOG_DEBUG: return ANDROID_LOG_DEBUG;
        case GL_LOG_INFO: return ANDROID_LOG_INFO;
        case GL_LOG_WARN: return ANDROID_LOG_WARN;
        case GL_LOG_ERROR: return ANDROID_LOG_ERROR;
    }
    return ANDROID_LOG_INFO;
}
#endif

/**
 * @brief Normalize controls to spaces and replace non-ASCII bytes with '?'.
 * @param text NUL-terminated message buffer.
 * @return Sanitized byte count, excluding the terminator.
 */
static size_t sanitize_ascii(char *text) {
    size_t output = 0;
    int pending_space = 0;
    for (size_t input = 0; text[input] != '\0'; ++input) {
        unsigned char value = (unsigned char)text[input];
        if (value < 0x20U || value == 0x7fU) {
            pending_space = output != 0U;
            continue;
        }
        if (value > 0x7eU) {
            value = '?';
        }
        if (pending_space != 0) {
            text[output++] = ' ';
            pending_space = 0;
        }
        text[output++] = (char)value;
    }
    while (output > 0U && text[output - 1U] == ' ') {
        --output;
    }
    text[output] = '\0';
    return output;
}

/**
 * @brief Return the source basename used in logcat records.
 * @param file Compiler-provided path, possibly NULL.
 * @return Nonempty basename or '?'.
 */
static const char *source_basename(const char *file) {
    if (file == NULL) {
        return "?";
    }
    const char *basename = file;
    for (const char *cursor = file; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            basename = cursor + 1;
        }
    }
    return *basename == '\0' ? "?" : basename;
}

void gl_log_write(const gl_log_level level, const char *file, const int line, const char *fmt, ...) {
    if ((int) level < atomic_load(&g_min_level)) {
        return; /* 低于阈值：不格式化、不写任何去处。 */
    }
    char body[512] = {0};
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    (void)sanitize_ascii(body);
#if defined(__ANDROID__)
    char logcat_line[640] = {0};
    snprintf(logcat_line, sizeof(logcat_line), "%s:%d %s", source_basename(file), line, body);
    (void)sanitize_ascii(logcat_line);
    (void)__android_log_write(android_priority(level), GL_LOG_TAG, logcat_line);
#endif
    const int fd = atomic_load(&g_export_fd);
    if (fd >= 0) {
        char exported[768] = {0};
        snprintf(exported, sizeof(exported), "[%c/%lldms] %s\n",
                 level_mark(level), elapsed_ms(), body);
        (void)write(fd, exported, strlen(exported));
        return;
    }
#if !defined(__ANDROID__)
    char host_line[768] = {0};
    snprintf(host_line, sizeof(host_line), "[%c/%lldms] %s:%d %s\n",
             level_mark(level), elapsed_ms(), source_basename(file), line, body);
    (void)sanitize_ascii(host_line);
    (void)write(STDERR_FILENO, host_line, strlen(host_line));
#endif
}
