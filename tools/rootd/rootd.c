/*
 * GhostLock minimal root server (GLR1 over AF_UNIX).
 *
 * The root watcher starts this binary as uid 0. The app connects to the Unix
 * socket, sends one command, and receives stdout/stderr/exit frames. The
 * client expects little-endian fields (see RootChannelProtocol.kt).
 *
 * Usage: ghostlock-rootd <socket-path> [expected-uid]
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define GLR1_MAGIC 0x474C5231u
#define TYPE_STDOUT 1
#define TYPE_STDERR 2
#define TYPE_EXIT 3
#define MAX_FRAME (64u * 1024u)
#define MAX_COMMAND (1024u * 1024u)
#ifndef GLR_SHELL
#define GLR_SHELL "/system/bin/sh"
#endif
#ifndef GLR_STATE_DIR
#define GLR_STATE_DIR "/data/local/tmp"
#endif

static int read_full(int fd, void *buf, size_t len) {
    unsigned char *p = (unsigned char *)buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static uint32_t get_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int send_frame(int fd, uint8_t type, const void *payload, uint32_t len) {
    unsigned char head[5];
    head[0] = type;
    head[1] = (unsigned char)(len & 0xFFu);
    head[2] = (unsigned char)((len >> 8) & 0xFFu);
    head[3] = (unsigned char)((len >> 16) & 0xFFu);
    head[4] = (unsigned char)((len >> 24) & 0xFFu);
    if (write_full(fd, head, sizeof(head)) != 0) return -1;
    return len == 0 ? 0 : write_full(fd, payload, len);
}

static int peer_allowed(int fd, uid_t expected) {
#ifdef __linux__
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return 0;
    if (expected != (uid_t)-1 && cred.uid != expected) return 0;
#else
    (void)fd;
    (void)expected;
#endif
    return 1;
}

static void serve(int client, uid_t expected) {
    if (!peer_allowed(client, expected)) {
        (void)send_frame(client, TYPE_STDERR, "peer uid rejected\n", 18);
        (void)send_frame(client, TYPE_EXIT, "\001\000\000\000", 4);
        return;
    }
    unsigned char head[8];
    if (read_full(client, head, sizeof(head)) != 0) return;
    if (get_u32(head) != GLR1_MAGIC) return;
    uint32_t cmd_len = get_u32(head + 4);
    if (cmd_len == 0 || cmd_len > MAX_COMMAND) return;
    char *cmd = (char *)malloc((size_t)cmd_len + 1);
    if (!cmd) return;
    if (read_full(client, cmd, cmd_len) != 0) { free(cmd); return; }
    cmd[cmd_len] = '\0';
    unsigned char stdin_head[4];
    if (read_full(client, stdin_head, sizeof(stdin_head)) != 0) { free(cmd); return; }
    uint32_t stdin_len = get_u32(stdin_head);
    if (stdin_len > MAX_COMMAND) { free(cmd); return; }
    char *input = NULL;
    if (stdin_len > 0) {
        input = (char *)malloc(stdin_len);
        if (!input || read_full(client, input, stdin_len) != 0) { free(input); free(cmd); return; }
    }

    int out_pipe[2], err_pipe[2], in_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0 || pipe(in_pipe) != 0) { free(input); free(cmd); return; }
    pid_t pid = fork();
    if (pid < 0) { free(input); free(cmd); return; }
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(client);
        execl(GLR_SHELL, "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]); close(out_pipe[1]); close(err_pipe[1]);
    if (stdin_len > 0) (void)write_full(in_pipe[1], input, stdin_len);
    close(in_pipe[1]);
    free(input); free(cmd);

    struct pollfd fds[2] = {
        { .fd = out_pipe[0], .events = POLLIN },
        { .fd = err_pipe[0], .events = POLLIN },
    };
    char buf[4096];
    int open_fds = 2;
    while (open_fds > 0) {
        int ready = poll(fds, 2, -1);
        if (ready < 0) { if (errno == EINTR) continue; break; }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ssize_t n = read(fds[i].fd, buf, sizeof(buf));
            if (n > 0) {
                if (send_frame(client, i == 0 ? TYPE_STDOUT : TYPE_STDERR, buf, (uint32_t)n) != 0) {
                    kill(pid, SIGKILL);
                    open_fds = 0;
                    break;
                }
            } else if (n == 0) {
                close(fds[i].fd);
                fds[i].fd = -1;
                --open_fds;
            }
        }
    }
    close(out_pipe[0]); close(err_pipe[0]);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    unsigned char payload[4] = {
        (unsigned char)(code & 0xFF), (unsigned char)((code >> 8) & 0xFF),
        (unsigned char)((code >> 16) & 0xFF), (unsigned char)((code >> 24) & 0xFF),
    };
    (void)send_frame(client, TYPE_EXIT, payload, 4);
}

static void run_finish(const char *dir) {
    char request[512], script[512], log[512], result[512];
    snprintf(request, sizeof(request), "%s/root-request", dir);
    snprintf(script, sizeof(script), "%s/pending.sh", dir);
    snprintf(log, sizeof(log), GLR_STATE_DIR "/.ghostlock_finish.log");
    snprintf(result, sizeof(result), GLR_STATE_DIR "/.ghostlock_finish.exit");
    if (access(request, F_OK) != 0) return;
    unlink(request);
    int out = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        close(out);
        execl(GLR_SHELL, "sh", script, (char *)NULL);
        _exit(127);
    }
    close(out);
    int status = 0;
    if (pid > 0) (void)waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    int fd = open(result, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%d\n", code);
        (void)write(fd, buf, (size_t)n);
        close(fd);
    }
    chmod(log, 0644);
    chmod(result, 0644);
}
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <socket> [uid]\n", argv[0]); return 2; }
    uid_t expected = (uid_t)-1;
    if (argc >= 3) expected = (uid_t)strtoul(argv[2], NULL, 10);
    const char *path = argv[1];
    if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) return 2;
    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0) { perror("bind"); return 1; }
    chmod(path, 0666);
    if (listen(server, 4) != 0) { perror("listen"); return 1; }
    for (;;) {
        struct pollfd pfd = { .fd = server, .events = POLLIN };
        int ready = poll(&pfd, 1, 200);
        if (ready < 0) { if (errno == EINTR) continue; break; }
        if (ready == 0) { if (argc >= 4) run_finish(argv[3]); continue; }
        int client = accept(server, NULL, NULL);
        if (client < 0) { if (errno == EINTR) continue; break; }
        serve(client, expected);
        close(client);
        if (argc >= 4) run_finish(argv[3]);
    }
    close(server);
    unlink(path);
    return 0;
}
