#define _POSIX_C_SOURCE 200809L
#include "common/gl_log.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_write_calls;
static int g_write_behavior;

enum gl_log_test_write_behavior {
    GL_LOG_TEST_WRITE_THROUGH = 0,
    GL_LOG_TEST_WRITE_EINTR = 1,
    GL_LOG_TEST_WRITE_SHORT = 2,
};

/**
 * @brief Count each logger write while preserving the host write behavior.
 * @param fd Destination descriptor.
 * @param buffer Bytes to write.
 * @param size Requested byte count.
 * @return Result from the system write call.
 */
ssize_t gl_log_test_write(const int fd, const void *buffer, const size_t size) {
    ++g_write_calls;
    if (g_write_behavior == GL_LOG_TEST_WRITE_EINTR) {
        errno = EINTR;
        return -1;
    }
    if (g_write_behavior == GL_LOG_TEST_WRITE_SHORT) {
        return write(fd, buffer, size / 2U);
    }
    return write(fd, buffer, size);
}

/**
 * @brief Verify the shared logger's formatting, filtering, and best-effort sink.
 * @return Zero after all assertions pass.
 */
int main(void) {
    char path[] = "/tmp/gl_log_test_XXXXXX";
    const int source_fd = mkstemp(path);
    assert(source_fd >= 0);
    assert(unlink(path) == 0);

    gl_log_set_min_level(GL_LOG_VERBOSE);
    const int owned_fd = gl_log_attach_fd(source_fd, 0, GL_LOG_VERBOSE);
    assert(owned_fd >= 0);
    assert(owned_fd != source_fd);
    assert(gl_log_export_fd() == owned_fd);
    assert(fcntl(owned_fd, F_GETFD) >= 0);
    assert(close(source_fd) == 0);
    assert(gl_log_timestamps() == 1);

    static const gl_log_level levels[] = {
        GL_LOG_VERBOSE, GL_LOG_DEBUG, GL_LOG_INFO, GL_LOG_WARN, GL_LOG_ERROR,
    };
    static const char markers[] = {'V', 'D', 'I', 'W', 'E'};
    for (size_t index = 0; index < sizeof(levels) / sizeof(levels[0]); ++index) {
        g_write_calls = 0;
        gl_log_write(levels[index], "nested/path.c", 20 + (int)index, "hello %d", 7);
        assert(g_write_calls == 1);
    }

    const char unsafe_text[] = "first\nsecond\r\n\xc3\xa9";
    g_write_calls = 0;
    gl_log_write(GL_LOG_WARN, "source.c", 31, "%s", unsafe_text);
    assert(g_write_calls == 1);

    gl_log_set_min_level(GL_LOG_ERROR);
    g_write_calls = 0;
    gl_log_write(GL_LOG_DEBUG, "source.c", 32, "filtered");
    assert(g_write_calls == 0);

    char output[2048] = {0};
    assert(lseek(owned_fd, 0, SEEK_SET) == 0);
    const ssize_t bytes = read(owned_fd, output, sizeof(output) - 1U);
    assert(bytes > 0);
    assert(close(owned_fd) == 0);
    for (size_t index = 0; index < sizeof(markers); ++index) {
        char expected[8];
        (void)snprintf(expected, sizeof(expected), "[%c/", markers[index]);
        assert(strstr(output, expected) != NULL);
    }
    assert(strstr(output, "] hello 7\n") != NULL);
    assert(strstr(output, "first second ??\n") != NULL);
    size_t newline_count = 0;
    for (const char *cursor = output; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            ++newline_count;
        }
        assert((unsigned char)*cursor < 0x80U);
    }
    assert(newline_count == 6U);

    gl_log_set_export_fd(owned_fd);
    g_write_calls = 0;
    gl_log_write(GL_LOG_ERROR, "source.c", 40, "sink failure is ignored");
    assert(g_write_calls == 1);
    gl_log_set_export_fd(-1);

    assert(gl_log_level_from_name("warn") == GL_LOG_WARN);
    assert(gl_log_level_from_name("unknown") == GL_LOG_INFO);

    char failure_path[] = "/tmp/gl_log_failure_test_XXXXXX";
    const int failure_source_fd = mkstemp(failure_path);
    assert(failure_source_fd >= 0);
    assert(unlink(failure_path) == 0);
    const int failure_fd = gl_log_attach_fd(failure_source_fd, 0, GL_LOG_ERROR);
    assert(failure_fd >= 0);
    assert(close(failure_source_fd) == 0);

    struct stat status;
    g_write_behavior = GL_LOG_TEST_WRITE_EINTR;
    g_write_calls = 0;
    errno = 0;
    gl_log_write(GL_LOG_ERROR, "source.c", 50, "injected interrupted sink");
    assert(g_write_calls == 1);
    assert(errno == EINTR);
    assert(fstat(failure_fd, &status) == 0);
    assert(status.st_size == 0);

    g_write_behavior = GL_LOG_TEST_WRITE_SHORT;
    g_write_calls = 0;
    gl_log_write(GL_LOG_ERROR, "source.c", 51, "injected partial sink");
    assert(g_write_calls == 1);
    assert(fstat(failure_fd, &status) == 0);
    assert(status.st_size > 0);

    g_write_behavior = GL_LOG_TEST_WRITE_THROUGH;
    g_write_calls = 0;
    gl_log_write(GL_LOG_ERROR, "source.c", 52, "logger continues after sink failure");
    assert(g_write_calls == 1);
    assert(lseek(failure_fd, 0, SEEK_SET) == 0);
    memset(output, 0, sizeof(output));
    const ssize_t failure_bytes = read(failure_fd, output, sizeof(output) - 1U);
    assert(failure_bytes > 0);
    assert(strstr(output, "logger continues after sink failure\n") != NULL);
    assert(gl_log_attach_fd(-1, 0, GL_LOG_ERROR) == -1);

    gl_log_set_export_fd(999999);
    g_write_calls = 0;
    errno = 0;
    gl_log_write(GL_LOG_ERROR, "source.c", 53, "invalid fd is ignored");
    assert(g_write_calls == 1);
    assert(errno == EBADF);
    gl_log_set_export_fd(-1);
    return 0;
}
