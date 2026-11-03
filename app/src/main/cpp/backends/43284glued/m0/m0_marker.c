#include "m0_marker.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#define M0_PATH_CAPACITY 256
#define M0_MARKER_CAPACITY 1024

/** Identity fields parsed from the fixed ROOT_READY wire sequence. */
struct M0Ready {
    char run_id[33];
    unsigned int launcher_uid;
    int worker_pid;
    unsigned long long worker_starttime;
    unsigned long long run_dir_dev;
    unsigned long long run_dir_ino;
    unsigned int worker_uid;
    unsigned int worker_euid;
    char worker_context[128];
    char run_dir_context[128];
};

/** Accept an absolute, bounded path and extract its final 32-lowercase-hex run ID. */
static int m0_validate_run_directory(const char *path, char run_id[33]) {
    size_t length = strnlen(path, M0_PATH_CAPACITY);
    if (path[0] != '/' || length < 34 || length >= M0_PATH_CAPACITY || path[length - 33] != '/') return -1;
    memcpy(run_id, path + length - 32, 32);
    run_id[32] = '\0';
    for (size_t i = 0; i < 32; ++i) {
        if (!((run_id[i] >= '0' && run_id[i] <= '9') ||
              (run_id[i] >= 'a' && run_id[i] <= 'f'))) return -1;
    }
    for (size_t i = 0; i < length; ++i) {
        const char value = path[i];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '/' || value == '.' ||
              value == '_' || value == '-')) return -1;
        if (value == '.' && i + 1 < length && path[i + 1] == '.') return -1;
    }
    return 0;
}

/** Write all marker bytes, retrying EINTR and rejecting short completion. */
static int m0_write_all(int fd, const char *bytes, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = write(fd, bytes + offset, length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        offset += (size_t)count;
    }
    return 0;
}

/** Read a procfs context string and remove its optional trailing newline. */
static int m0_read_context(char *output, size_t capacity) {
    int fd = open("/proc/self/attr/current", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t count = read(fd, output, capacity - 1);
    close(fd);
    if (count <= 0) return -1;
    while (count > 0 && (output[count - 1] == '\n' || output[count - 1] == '\0')) --count;
    output[count] = '\0';
    return 0;
}

/** Read Linux proc field 22 (starttime) for this worker process. */
static int m0_read_starttime(unsigned long long *starttime) {
    char line[2048];
    int fd = open("/proc/self/stat", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t count = read(fd, line, sizeof(line) - 1);
    close(fd);
    if (count <= 0) return -1;
    line[count] = '\0';
    char *cursor = strrchr(line, ')');
    if (cursor == NULL) return -1;
    cursor += 2;
    for (int field = 3; field < 22; ++field) {
        cursor = strchr(cursor, ' ');
        if (cursor == NULL) return -1;
        while (*cursor == ' ') ++cursor;
    }
    char *end = NULL;
    unsigned long long value = strtoull(cursor, &end, 10);
    if (end == cursor || (*end != ' ' && *end != '\n' && *end != '\0')) return -1;
    *starttime = value;
    return 0;
}

/** Parse the exact ROOT_READY field order and reject trailing bytes. */
static int m0_parse_ready(const char *text, size_t length, struct M0Ready *ready) {
    int consumed = -1;
    int fields = sscanf(text,
        "version=1\nrun_id=%32[0-9a-f]\nlauncher_uid=%u\nworker_pid=%d\n"
        "worker_starttime=%llu\nrun_dir_dev=%llu\nrun_dir_ino=%llu\n"
        "worker_uid=%u\nworker_euid=%u\nworker_context=%127[^\n]\n"
        "run_dir_context=%127[^\n]\n%n",
        ready->run_id, &ready->launcher_uid, &ready->worker_pid,
        &ready->worker_starttime, &ready->run_dir_dev, &ready->run_dir_ino,
        &ready->worker_uid, &ready->worker_euid, ready->worker_context,
        ready->run_dir_context, &consumed);
    return fields == 10 && consumed >= 0 && (size_t)consumed == length ? 0 : -1;
}

/** Configure the fixed `glk_worker=` insmod parameter in the copied assembly data. */
int m0_prepare_worker_argument(const char *run_directory, char *argument, size_t capacity) {
    char run_id[33];
    static const char prefix[] = "glk_worker=";
    if (argument == NULL || capacity <= sizeof(prefix) ||
        m0_validate_run_directory(run_directory, run_id) != 0) return -1;
    memcpy(argument, prefix, sizeof(prefix) - 1);
    size_t prefix_length = sizeof(prefix) - 1;
    int length = snprintf(argument + prefix_length, capacity - prefix_length,
                          "%s/m0-worker", run_directory);
    return length > 0 && (size_t)length < capacity - prefix_length ? 0 : -1;
}

/** Verify the root-owned marker's exact identity and owner-only mode from the launcher domain. */
int m0_root_ready_exists(const char *run_directory) {
    char run_id[33];
    char text[M0_MARKER_CAPACITY];
    struct stat directory_stat;
    struct stat marker_stat;
    struct M0Ready ready = {0};
    if (m0_validate_run_directory(run_directory, run_id) != 0) return -1;
    int directory_fd = open(run_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory_fd < 0 || fstat(directory_fd, &directory_stat) != 0) {
        if (directory_fd >= 0) close(directory_fd);
        return -1;
    }
    int marker_fd = openat(directory_fd, "ROOT_READY", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (marker_fd < 0 || fstat(marker_fd, &marker_stat) != 0 || !S_ISREG(marker_stat.st_mode) ||
        marker_stat.st_uid != getuid() || (marker_stat.st_mode & 0777) != 0600) {
        if (marker_fd >= 0) close(marker_fd);
        close(directory_fd);
        return -1;
    }
    ssize_t count = read(marker_fd, text, sizeof(text) - 1);
    close(marker_fd);
    close(directory_fd);
    if (count <= 0) return -1;
    text[count] = '\0';
    if (m0_parse_ready(text, (size_t)count, &ready) != 0 || strcmp(ready.run_id, run_id) != 0 ||
        ready.launcher_uid != (unsigned int)getuid() || ready.worker_uid != 0 || ready.worker_euid != 0 ||
        strcmp(ready.worker_context, "u:r:init:s0") != 0 || ready.worker_pid <= 1 ||
        ready.worker_starttime == 0 || ready.run_dir_dev != (unsigned long long)directory_stat.st_dev ||
        ready.run_dir_ino != (unsigned long long)directory_stat.st_ino) return -1;
    return 0;
}

/**
 * Run the isolated worker after system-root UMH exec.
 *
 * @param run_directory the launcher-created directory passed through fixed module argv.
 * @return zero only after one identity-matching release marker is removed; otherwise nonzero.
 * @note The worker does not open/execute payload or finish and does not alter SELinux state.
 */
int m0_root_worker_main(const char *run_directory) {
    char run_id[33];
    char context[128];
    char directory_context[128];
    char ready_text[M0_MARKER_CAPACITY];
    char release_text[512];
    struct stat directory_stat;
    struct stat file_stat;
    struct M0Ready ready = {0};
    unsigned long long starttime;
    int directory_fd = -1;
    int ready_fd = -1;
    int release_fd = -1;
    int result = 20;

    if (getuid() != 0 || geteuid() != 0 || m0_validate_run_directory(run_directory, run_id) != 0 ||
        m0_read_context(context, sizeof(context)) != 0 || strcmp(context, "u:r:init:s0") != 0) return result;
    directory_fd = open(run_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory_fd < 0 || fstat(directory_fd, &directory_stat) != 0 ||
        (directory_stat.st_mode & 0777) != 0700 || directory_stat.st_uid == 0) goto done;
    ssize_t context_length = getxattr(run_directory, "security.selinux", directory_context,
                                      sizeof(directory_context) - 1);
    if (context_length <= 0) goto done;
    if (directory_context[context_length - 1] == '\0') --context_length;
    directory_context[context_length] = '\0';
    if (m0_read_starttime(&starttime) != 0) goto done;
    ready.launcher_uid = (unsigned int)directory_stat.st_uid;
    ready.worker_pid = (int)getpid();
    ready.worker_starttime = starttime;
    ready.run_dir_dev = (unsigned long long)directory_stat.st_dev;
    ready.run_dir_ino = (unsigned long long)directory_stat.st_ino;
    ready.worker_uid = (unsigned int)getuid();
    ready.worker_euid = (unsigned int)geteuid();
    strlcpy(ready.run_id, run_id, sizeof(ready.run_id));
    strlcpy(ready.worker_context, context, sizeof(ready.worker_context));
    strlcpy(ready.run_dir_context, directory_context, sizeof(ready.run_dir_context));
    int ready_length = snprintf(ready_text, sizeof(ready_text),
        "version=1\nrun_id=%s\nlauncher_uid=%u\nworker_pid=%d\nworker_starttime=%llu\n"
        "run_dir_dev=%llu\nrun_dir_ino=%llu\nworker_uid=%u\nworker_euid=%u\n"
        "worker_context=%s\nrun_dir_context=%s\n",
        ready.run_id, ready.launcher_uid, ready.worker_pid, ready.worker_starttime,
        ready.run_dir_dev, ready.run_dir_ino, ready.worker_uid, ready.worker_euid,
        ready.worker_context, ready.run_dir_context);
    if (ready_length <= 0 || (size_t)ready_length >= sizeof(ready_text)) goto done;
    ready_fd = openat(directory_fd, "ROOT_READY", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (ready_fd < 0 || m0_write_all(ready_fd, ready_text, (size_t)ready_length) != 0 ||
        fchown(ready_fd, (uid_t)ready.launcher_uid, (gid_t)-1) != 0 || fchmod(ready_fd, 0600) != 0 ||
        fsync(ready_fd) != 0 || fsync(directory_fd) != 0) goto done;
    close(ready_fd);
    ready_fd = -1;

    for (int attempt = 0; attempt < 300; ++attempt) {
        release_fd = openat(directory_fd, "RELEASE_FINISH", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (release_fd >= 0) break;
        if (errno != ENOENT) goto done;
        struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000L };
        nanosleep(&delay, NULL);
    }
    if (release_fd < 0 || fstat(release_fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_uid != ready.launcher_uid || (file_stat.st_mode & 0777) != 0600) goto done;
    ssize_t release_length = read(release_fd, release_text, sizeof(release_text) - 1);
    close(release_fd);
    release_fd = -1;
    if (release_length <= 0) goto done;
    release_text[release_length] = '\0';
    int consumed = -1;
    int fields = sscanf(release_text,
        "version=1\nrun_id=%32[0-9a-f]\nlauncher_uid=%u\nworker_pid=%d\n"
        "worker_starttime=%llu\nrun_dir_dev=%llu\nrun_dir_ino=%llu\n%n",
        ready.run_id, &ready.launcher_uid, &ready.worker_pid, &ready.worker_starttime,
        &ready.run_dir_dev, &ready.run_dir_ino, &consumed);
    if (fields != 6 || consumed < 0 || consumed != release_length ||
        strcmp(ready.run_id, run_id) != 0 || ready.launcher_uid != (unsigned int)directory_stat.st_uid ||
        ready.worker_pid != (int)getpid() || ready.worker_starttime != starttime ||
        ready.run_dir_dev != (unsigned long long)directory_stat.st_dev ||
        ready.run_dir_ino != (unsigned long long)directory_stat.st_ino) goto done;
    if (unlinkat(directory_fd, "RELEASE_FINISH", 0) != 0 || fsync(directory_fd) != 0) goto done;
    result = 0;

done:
    if (release_fd >= 0) close(release_fd);
    if (ready_fd >= 0) close(ready_fd);
    if (directory_fd >= 0) close(directory_fd);
    return result;
}

/**
 * Create one release marker as the selected launcher and wait for worker exit.
 *
 * @param run_directory App or Shizuku-owned unique run directory.
 * @return zero only after the one marker is consumed and worker PID is gone.
 * @note This fixture frame never invokes Payload/Finish.
 */
int m0_launcher_release(const char *run_directory) {
    char run_id[33];
    char ready_text[M0_MARKER_CAPACITY];
    struct M0Ready ready = {0};
    struct stat directory_stat;
    struct stat file_stat;
    int directory_fd = -1;
    int ready_fd = -1;
    int release_fd = -1;
    int result = 30;
    if (m0_validate_run_directory(run_directory, run_id) != 0) return result;
    directory_fd = open(run_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory_fd < 0 || fstat(directory_fd, &directory_stat) != 0 ||
        directory_stat.st_uid != getuid() || (directory_stat.st_mode & 0777) != 0700) goto done;
    ready_fd = openat(directory_fd, "ROOT_READY", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (ready_fd < 0 || fstat(ready_fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_uid != getuid() || (file_stat.st_mode & 0777) != 0600) goto done;
    ssize_t ready_length = read(ready_fd, ready_text, sizeof(ready_text) - 1);
    close(ready_fd);
    ready_fd = -1;
    if (ready_length <= 0) goto done;
    ready_text[ready_length] = '\0';
    if (m0_parse_ready(ready_text, (size_t)ready_length, &ready) != 0 ||
        strcmp(ready.run_id, run_id) != 0 || ready.launcher_uid != (unsigned int)getuid() ||
        ready.worker_uid != 0 || ready.worker_euid != 0 || strcmp(ready.worker_context, "u:r:init:s0") != 0 ||
        ready.worker_pid <= 1 || ready.worker_starttime == 0 ||
        ready.run_dir_dev != (unsigned long long)directory_stat.st_dev ||
        ready.run_dir_ino != (unsigned long long)directory_stat.st_ino) goto done;
    release_fd = openat(directory_fd, "RELEASE_FINISH",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (release_fd < 0) goto done;
    char release_text[512];
    int release_length = snprintf(release_text, sizeof(release_text),
        "version=1\nrun_id=%s\nlauncher_uid=%u\nworker_pid=%d\nworker_starttime=%llu\n"
        "run_dir_dev=%llu\nrun_dir_ino=%llu\n",
        ready.run_id, ready.launcher_uid, ready.worker_pid, ready.worker_starttime,
        ready.run_dir_dev, ready.run_dir_ino);
    if (release_length <= 0 || (size_t)release_length >= sizeof(release_text) ||
        m0_write_all(release_fd, release_text, (size_t)release_length) != 0 ||
        fchown(release_fd, (uid_t)ready.launcher_uid, (gid_t)-1) != 0 ||
        fchmod(release_fd, 0600) != 0 || fsync(release_fd) != 0 || fsync(directory_fd) != 0) goto done;
    close(release_fd);
    release_fd = -1;
    int duplicate_fd = openat(directory_fd, "RELEASE_FINISH",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (duplicate_fd >= 0 || errno != EEXIST) {
        if (duplicate_fd >= 0) close(duplicate_fd);
        goto done;
    }
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (fstatat(directory_fd, "RELEASE_FINISH", &file_stat, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno != ENOENT) goto done;
            if (kill((pid_t)ready.worker_pid, 0) == 0 || errno == EPERM) {
                struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000L };
                nanosleep(&delay, NULL);
                continue;
            }
            if (errno != ESRCH) goto done;
            result = 0;
            break;
        }
        if (!S_ISREG(file_stat.st_mode) || file_stat.st_uid != getuid() ||
            (file_stat.st_mode & 0777) != 0600) goto done;
        struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000L };
        nanosleep(&delay, NULL);
    }
    if (result != 0) goto done;
    printf("M0_ROOT_READY=run:%s uid:%u pid:%d context:%s dir_context:%s\n",
        ready.run_id, ready.launcher_uid, ready.worker_pid,
        ready.worker_context, ready.run_dir_context);
    printf("M0_RELEASE_FINISH_CONSUMED=1\nM0_WORKER_EXITED=1\nM0_SELINUX_CHANGED=0\n");

done:
    if (release_fd >= 0) close(release_fd);
    if (ready_fd >= 0) close(ready_fd);
    if (directory_fd >= 0) close(directory_fd);
    return result;
}
