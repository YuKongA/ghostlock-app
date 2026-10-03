#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHELLD_SOCKET_NAME "shelld"
#define SHELLD_BACKLOG 16

struct request_header {
    uint32_t path_len;
    uint32_t argc;
};

struct copy_thread_args {
    int src_fd;
    int dst_fd;
    bool close_dst;
};

static void fatal_perror(const char *what) {
    perror(what);
    exit(1);
}

static void fatal_msg(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static ssize_t read_all(int fd, void *buf, size_t size) {
    size_t offset = 0;

    while (offset < size) {
        ssize_t rv = read(fd, (char *)buf + offset, size - offset);
        if (rv == 0) {
            return 0;
        }
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        offset += (size_t)rv;
    }

    return (ssize_t)offset;
}

static ssize_t write_all(int fd, const void *buf, size_t size) {
    size_t offset = 0;

    while (offset < size) {
        ssize_t rv = write(fd, (const char *)buf + offset, size - offset);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        offset += (size_t)rv;
    }

    return (ssize_t)offset;
}

static void close_quietly(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

static int create_abstract_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    return fd;
}

static socklen_t make_abstract_addr(struct sockaddr_un *addr, const char *name) {
    size_t name_len = strlen(name);

    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    addr->sun_path[0] = '\0';
    memcpy(addr->sun_path + 1, name, name_len);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);
}

static int connect_control_socket(void) {
    struct sockaddr_un addr;
    socklen_t addr_len;
    int fd = create_abstract_socket();

    if (fd < 0) {
        return -1;
    }

    addr_len = make_abstract_addr(&addr, SHELLD_SOCKET_NAME);
    if (connect(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int listen_control_socket(void) {
    struct sockaddr_un addr;
    socklen_t addr_len;
    int fd = create_abstract_socket();
    int enable = 1;

    if (fd < 0) {
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        close(fd);
        return -1;
    }

    addr_len = make_abstract_addr(&addr, SHELLD_SOCKET_NAME);
    if (bind(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, SHELLD_BACKLOG) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int send_error_response(int fd, const char *message) {
    char tag = 'E';
    uint32_t length = (uint32_t)strlen(message);

    if (write_all(fd, &tag, sizeof(tag)) < 0) {
        return -1;
    }
    if (write_all(fd, &length, sizeof(length)) < 0) {
        return -1;
    }
    if (write_all(fd, message, length) < 0) {
        return -1;
    }
    return 0;
}

static int send_exit_status(int fd, int status) {
    struct {
        char tag;
        int32_t status;
    } packet;

    packet.tag = 'S';
    packet.status = status;
    return write_all(fd, &packet, sizeof(packet)) < 0 ? -1 : 0;
}

static int send_fds(int fd, const int *fds, size_t fd_count) {
    struct msghdr msg;
    struct iovec iov;
    char tag = 'F';
    char control[CMSG_SPACE(sizeof(int) * 3)];
    struct cmsghdr *cmsg;

    if (fd_count != 3) {
        errno = EINVAL;
        return -1;
    }

    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));

    iov.iov_base = &tag;
    iov.iov_len = sizeof(tag);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fd_count);
    memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * fd_count);

    return sendmsg(fd, &msg, 0) < 0 ? -1 : 0;
}

static int recv_fds(int fd, int *fds, size_t fd_count, char *tag_out) {
    struct msghdr msg;
    struct iovec iov;
    char tag = '\0';
    char control[CMSG_SPACE(sizeof(int) * 3)];
    struct cmsghdr *cmsg;

    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));

    iov.iov_base = &tag;
    iov.iov_len = sizeof(tag);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    if (recvmsg(fd, &msg, 0) <= 0) {
        return -1;
    }

    *tag_out = tag;
    if (tag != 'F') {
        return 0;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        errno = EBADMSG;
        return -1;
    }

    if ((size_t)(cmsg->cmsg_len - CMSG_LEN(0)) < sizeof(int) * fd_count) {
        errno = EBADMSG;
        return -1;
    }

    memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * fd_count);
    return 0;
}

static int recv_error_response(int fd, char **message_out) {
    uint32_t length;
    char *message;

    if (read_all(fd, &length, sizeof(length)) <= 0) {
        return -1;
    }

    message = malloc((size_t)length + 1);
    if (message == NULL) {
        return -1;
    }

    if (read_all(fd, message, length) <= 0) {
        free(message);
        return -1;
    }

    message[length] = '\0';
    *message_out = message;
    return 0;
}

static int recv_exit_status(int fd, int *status_out) {
    struct {
        char tag;
        int32_t status;
    } packet;

    if (read_all(fd, &packet, sizeof(packet)) <= 0) {
        return -1;
    }

    if (packet.tag != 'S') {
        errno = EBADMSG;
        return -1;
    }

    *status_out = packet.status;
    return 0;
}

static int send_request(int fd, const char *path, char *const *argv) {
    struct request_header header;
    uint32_t argc = 0;
    uint32_t i;

    while (argv[argc] != NULL) {
        argc++;
    }

    header.path_len = (uint32_t)strlen(path);
    header.argc = argc;

    if (write_all(fd, &header, sizeof(header)) < 0) {
        return -1;
    }
    if (write_all(fd, path, header.path_len) < 0) {
        return -1;
    }

    for (i = 0; i < argc; ++i) {
        uint32_t arg_len = (uint32_t)strlen(argv[i]);
        if (write_all(fd, &arg_len, sizeof(arg_len)) < 0) {
            return -1;
        }
        if (write_all(fd, argv[i], arg_len) < 0) {
            return -1;
        }
    }

    return 0;
}

static int recv_request(int fd, char **path_out, char ***argv_out) {
    struct request_header header;
    char *path = NULL;
    char **argv = NULL;
    uint32_t i;

    if (read_all(fd, &header, sizeof(header)) <= 0) {
        return -1;
    }

    path = malloc((size_t)header.path_len + 1);
    argv = calloc((size_t)header.argc + 1, sizeof(char *));
    if (path == NULL || argv == NULL) {
        free(path);
        free(argv);
        return -1;
    }

    if (read_all(fd, path, header.path_len) <= 0) {
        free(path);
        free(argv);
        return -1;
    }
    path[header.path_len] = '\0';

    for (i = 0; i < header.argc; ++i) {
        uint32_t arg_len;

        if (read_all(fd, &arg_len, sizeof(arg_len)) <= 0) {
            goto fail;
        }

        argv[i] = malloc((size_t)arg_len + 1);
        if (argv[i] == NULL) {
            goto fail;
        }

        if (read_all(fd, argv[i], arg_len) <= 0) {
            goto fail;
        }
        argv[i][arg_len] = '\0';
    }

    *path_out = path;
    *argv_out = argv;
    return 0;

    fail:
    for (i = 0; i < header.argc; ++i) {
        free(argv[i]);
    }
    free(argv);
    free(path);
    return -1;
}

static void free_argv(char **argv) {
    size_t i;

    if (argv == NULL) {
        return;
    }

    for (i = 0; argv[i] != NULL; ++i) {
        free(argv[i]);
    }
    free(argv);
}

static void *copy_thread_main(void *opaque) {
    struct copy_thread_args *args = opaque;
    char buffer[8192];

    for (;;) {
        ssize_t rv = read(args->src_fd, buffer, sizeof(buffer));
        if (rv == 0) {
            break;
        }
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (write_all(args->dst_fd, buffer, (size_t)rv) < 0) {
            break;
        }
    }

    if (args->close_dst) {
        close_quietly(args->dst_fd);
        args->dst_fd = -1;
    }

    return NULL;
}

static void *server_client_thread(void *opaque) {
    int client_fd = *(int *)opaque;
    char *path = NULL;
    char **argv = NULL;
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int sendable_fds[3] = {-1, -1, -1};
    pid_t pid;
    int status;

    free(opaque);

    if (recv_request(client_fd, &path, &argv) < 0) {
        send_error_response(client_fd, "failed to receive request");
        goto out;
    }

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        send_error_response(client_fd, "failed to create pipes");
        goto out;
    }

    pid = fork();
    if (pid < 0) {
        send_error_response(client_fd, "failed to fork");
        goto out;
    }

    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close_quietly(stdin_pipe[0]);
        close_quietly(stdin_pipe[1]);
        close_quietly(stdout_pipe[0]);
        close_quietly(stdout_pipe[1]);
        close_quietly(stderr_pipe[0]);
        close_quietly(stderr_pipe[1]);

        execv(path, argv);
        perror("execv");
        _exit(127);
    }

    close_quietly(stdin_pipe[0]);
    close_quietly(stdout_pipe[1]);
    close_quietly(stderr_pipe[1]);
    stdin_pipe[0] = -1;
    stdout_pipe[1] = -1;
    stderr_pipe[1] = -1;

    sendable_fds[0] = stdin_pipe[1];
    sendable_fds[1] = stdout_pipe[0];
    sendable_fds[2] = stderr_pipe[0];

    if (send_fds(client_fd, sendable_fds, 3) < 0) {
        goto child_wait;
    }

    close_quietly(sendable_fds[0]);
    close_quietly(sendable_fds[1]);
    close_quietly(sendable_fds[2]);
    sendable_fds[0] = -1;
    sendable_fds[1] = -1;
    sendable_fds[2] = -1;

    child_wait:
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            status = 127 << 8;
            break;
        }
    }
    send_exit_status(client_fd, status);

    out:
    close_quietly(sendable_fds[0]);
    close_quietly(sendable_fds[1]);
    close_quietly(sendable_fds[2]);
    close_quietly(stdin_pipe[0]);
    close_quietly(stdin_pipe[1]);
    close_quietly(stdout_pipe[0]);
    close_quietly(stdout_pipe[1]);
    close_quietly(stderr_pipe[0]);
    close_quietly(stderr_pipe[1]);
    free(path);
    free_argv(argv);
    close_quietly(client_fd);
    return NULL;
}

static int run_server(void) {
    int listen_fd = listen_control_socket();

    if (listen_fd < 0) {
        fatal_perror("listen_control_socket");
    }

    fprintf(stderr, "shelld server listening on abstract socket '%s'\n", SHELLD_SOCKET_NAME);

    for (;;) {
        int *client_fd = malloc(sizeof(*client_fd));
        pthread_t thread;

        if (client_fd == NULL) {
            fatal_perror("malloc");
        }

        *client_fd = accept(listen_fd, NULL, NULL);
        if (*client_fd < 0) {
            free(client_fd);
            if (errno == EINTR) {
                continue;
            }
            fatal_perror("accept");
        }

        if (pthread_create(&thread, NULL, server_client_thread, client_fd) != 0) {
            send_error_response(*client_fd, "failed to create worker thread");
            close_quietly(*client_fd);
            free(client_fd);
            continue;
        }

        pthread_detach(thread);
    }
}

static void exit_with_wait_status(int status) {
    if (WIFEXITED(status)) {
        exit(WEXITSTATUS(status));
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        signal(sig, SIG_DFL);
        raise(sig);
        exit(128 + sig);
    }

    exit(1);
}

static int run_client(int argc, char **argv) {
    int control_fd;
    char *path;
    char **child_argv;
    int remote_fds[3] = {-1, -1, -1};
    char tag;
    char *error_message = NULL;
    int status;
    pthread_t threads[3];
    struct copy_thread_args thread_args[3];
    bool thread_started[3] = {false, false, false};
    size_t i;

    if (argc < 3) {
        fatal_msg("usage: shelld client <path> [arg ...]");
    }

    path = argv[2];
    child_argv = calloc((size_t)(argc - 1), sizeof(char *));
    if (child_argv == NULL) {
        fatal_perror("calloc");
    }

    child_argv[0] = path;
    for (i = 3; i < (size_t)argc; ++i) {
        child_argv[i - 2] = argv[i];
    }

    control_fd = connect_control_socket();
    if (control_fd < 0) {
        fatal_perror("connect_control_socket");
    }

    if (send_request(control_fd, path, child_argv) < 0) {
        fatal_perror("send_request");
    }

    if (recv_fds(control_fd, remote_fds, 3, &tag) < 0) {
        fatal_perror("recv_fds");
    }

    if (tag == 'E') {
        if (recv_error_response(control_fd, &error_message) == 0) {
            fatal_msg("server error: %s", error_message);
        }
        fatal_msg("server returned an error");
    }

    if (tag != 'F') {
        fatal_msg("unexpected server response tag '%c'", tag);
    }

    thread_args[0].src_fd = STDIN_FILENO;
    thread_args[0].dst_fd = remote_fds[0];
    thread_args[0].close_dst = true;
    thread_args[1].src_fd = remote_fds[1];
    thread_args[1].dst_fd = STDOUT_FILENO;
    thread_args[1].close_dst = false;
    thread_args[2].src_fd = remote_fds[2];
    thread_args[2].dst_fd = STDERR_FILENO;
    thread_args[2].close_dst = false;

    for (i = 0; i < 3; ++i) {
        if (pthread_create(&threads[i], NULL, copy_thread_main, &thread_args[i]) != 0) {
            fatal_perror("pthread_create");
        }
        thread_started[i] = true;
    }

    if (recv_exit_status(control_fd, &status) < 0) {
        fatal_perror("recv_exit_status");
    }

    for (i = 0; i < 3; ++i) {
        if (thread_started[i]) {
            pthread_join(threads[i], NULL);
        }
    }

    close_quietly(remote_fds[0]);
    close_quietly(remote_fds[1]);
    close_quietly(remote_fds[2]);
    close_quietly(control_fd);
    free(child_argv);
    free(error_message);
    exit_with_wait_status(status);
    return 0;
}

static void usage(FILE *stream) {
    fprintf(stream,
            "usage:\n"
            "  shelld server\n"
            "  shelld client <path> [arg ...]\n");
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "server") == 0) {
        return run_server();
    }

    if (strcmp(argv[1], "client") == 0) {
        return run_client(argc, argv);
    }

    usage(stderr);
    return 1;
}
