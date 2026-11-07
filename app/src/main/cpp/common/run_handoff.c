/**
 * @file common/run_handoff.c
 * @brief Implement the fixed-path, same-opened-fd shell handoff.
 */

#include "common/run_handoff.h"

#include "common/gl_log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define GL_HANDOFF_SCRIPT_FD 198

static const char GL_HANDOFF_HEADER[] = "#!/system/bin/sh\n# GHOSTLOCK_HANDOFF_V1\n";
static const char GL_HANDOFF_DIRECTORY[] = "root-script";
static const char GL_HANDOFF_SCRIPT[] = "root-script.sh";

#if defined(__ANDROID__)
static const char GL_HANDOFF_SHELL[] = "/system/bin/sh";
static const char GL_HANDOFF_FD_PATH[] = "/proc/self/fd/198";
#else
static const char GL_HANDOFF_SHELL[] = "/bin/sh";
static const char GL_HANDOFF_FD_PATH[] = "/dev/fd/198";
#endif

/**
 * @brief Close an owned descriptor and replace it with -1.
 * @param fd Address of one helper-owned descriptor.
 */
static void close_owned(int *const fd) {
    if (fd != NULL && *fd >= 0) {
        (void)close(*fd);
        *fd = -1;
    }
}

/**
 * @brief Read and compare the exact handoff header without advancing the shared file offset.
 * @param script_fd Open ordinary script descriptor.
 * @return Nonzero only when every expected byte matches.
 */
static int has_exact_header(const int script_fd) {
    char bytes[sizeof(GL_HANDOFF_HEADER) - 1U];
    size_t consumed = 0U;
    while (consumed < sizeof(bytes)) {
        const ssize_t count = pread(
            script_fd,
            bytes + consumed,
            sizeof(bytes) - consumed,
            (off_t)consumed
        );
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (count == 0) {
            return 0;
        }
        consumed += (size_t)count;
    }
    return memcmp(bytes, GL_HANDOFF_HEADER, sizeof(bytes)) == 0;
}

int32_t run_handoff(void) {
    int home_fd = -1;
    int directory_fd = -1;
    int script_fd = -1;
    int actions_ready = 0;
    posix_spawn_file_actions_t actions;
    pid_t child = (pid_t)-1;
    int32_t result = GL_HANDOFF_INTERNAL;

    const char *const home = getenv("GHOSTLOCK_HOME");
    if (home == NULL) {
        return GL_HANDOFF_HOME_MISSING;
    }
    const size_t home_length = strlen(home);
    if (home_length == 0U || home[0] != '/' || home_length >= (size_t)PATH_MAX) {
        return GL_HANDOFF_HOME_INVALID;
    }

    home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (home_fd < 0) {
        return GL_HANDOFF_OPEN_HOME;
    }
    directory_fd = openat(
        home_fd,
        GL_HANDOFF_DIRECTORY,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
    );
    if (directory_fd < 0) {
        result = GL_HANDOFF_OPEN_DIRECTORY;
        goto finish;
    }
    script_fd = openat(directory_fd, GL_HANDOFF_SCRIPT, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (script_fd < 0) {
        result = GL_HANDOFF_OPEN_SCRIPT;
        goto finish;
    }

    struct stat script_stat;
    if (fstat(script_fd, &script_stat) != 0 || !S_ISREG(script_stat.st_mode)) {
        result = GL_HANDOFF_SCRIPT_TYPE;
        goto finish;
    }
    if (!has_exact_header(script_fd)) {
        result = GL_HANDOFF_SCRIPT_MAGIC;
        goto finish;
    }

    if (posix_spawn_file_actions_init(&actions) != 0) {
        result = GL_HANDOFF_SPAWN_CALL;
        goto finish;
    }
    actions_ready = 1;
    if (posix_spawn_file_actions_adddup2(&actions, script_fd, GL_HANDOFF_SCRIPT_FD) != 0) {
        result = GL_HANDOFF_SPAWN_CALL;
        goto finish;
    }
    const int log_fd = gl_log_export_fd();
    if (log_fd >= 0) {
        if (posix_spawn_file_actions_adddup2(&actions, log_fd, STDOUT_FILENO) != 0 ||
            posix_spawn_file_actions_adddup2(&actions, log_fd, STDERR_FILENO) != 0) {
            result = GL_HANDOFF_SPAWN_CALL;
            goto finish;
        }
    }

    char *const arguments[] = {
        (char *)GL_HANDOFF_SHELL,
        (char *)GL_HANDOFF_FD_PATH,
        NULL,
    };
    const int spawn_status = posix_spawn(
        &child,
        GL_HANDOFF_SHELL,
        &actions,
        NULL,
        arguments,
        environ
    );
    if (spawn_status != 0) {
        result = GL_HANDOFF_SPAWN_CALL;
        goto finish;
    }

    close_owned(&script_fd);
    close_owned(&directory_fd);
    close_owned(&home_fd);

    int child_status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &child_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child) {
        result = GL_HANDOFF_WAIT;
        goto finish;
    }
    if (WIFEXITED(child_status)) {
        result = (int32_t)WEXITSTATUS(child_status);
        goto finish;
    }
    if (WIFSIGNALED(child_status)) {
        result = GL_HANDOFF_CHILD_SIGNALED;
        goto finish;
    }
    result = GL_HANDOFF_INTERNAL;

finish:
    if (actions_ready != 0) {
        (void)posix_spawn_file_actions_destroy(&actions);
    }
    close_owned(&script_fd);
    close_owned(&directory_fd);
    close_owned(&home_fd);
    return result;
}
