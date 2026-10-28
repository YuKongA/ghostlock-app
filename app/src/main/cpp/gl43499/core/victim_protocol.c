/**
 * @file victim_protocol.c
 * @brief 通用 victim helper transport 的机械实现（posix_spawn + fd 协议 + raw 拷贝）。
 *
 * @note 本文件是 backend-agnostic 机制层：**零 fork、零业务逻辑、零状态机、零解析**。
 *       控制帧只按外层长度搬运；opcode/args/校验语义由 Kotlin 与 helper 解释。
 * @note 控制通道 fd3 是双向 socketpair（读命令+写回复）；raw 通道 fd4 是共享 memfd。
 * @note 无全局状态：所有资源挂在调用方持有的不透明 token 上；同一 token 的并发由
 *       调用方串行化（Kotlin `VictimSession` 已持有对象锁）。
 * @note 阶段：批 A（task-52）；不接 Flow/JNI 生产路径。
 */
#include "victim_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <linux/memfd.h>
#endif

extern char **environ;

/** @brief 内部 helper 句柄（不透明 token = 本结构指针）。 */
struct glk_victim_handle_impl {
    int32_t control_fd; /**< 父侧写端（helper 读端为 fd 3）。 */
    int32_t raw_fd; /**< 共享 raw memfd（helper 侧 fd 4）。 */
    int32_t pid; /**< helper PID；已回收后仍保留原值。 */
    int32_t reaped; /**< 1=已 waitpid 回收（exit_code 有效）。 */
    int32_t exit_code; /**< 机械退出码（WIFEXITED?status:128+signal）。 */
    uint64_t raw_size; /**< raw memfd 容量。 */
};

/**
 * @brief token → 内部指针（不校验存活；0 返回 NULL）。
 * @param token 不透明 token。
 * @return 内部结构指针或 NULL。
 */
static struct glk_victim_handle_impl *glk_victim_from_token(
    glk_victim_token token) {
    return (struct glk_victim_handle_impl *) (uintptr_t) token;
}

/**
 * @brief 单次时钟读取（毫秒，单调钟；仅用于有界等待）。
 * @return 单调毫秒数。
 */
static int64_t glk_victim_now_ms(void) {
    struct timespec now;
    (void) clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t) now.tv_sec * INT64_C(1000) + (int64_t) now.tv_nsec / (INT64_C(1000) * INT64_C(1000));
}

/**
 * @brief 有界 sleep（毫秒；wait 轮询间隔）。
 * @param usec 微秒。
 */
static void glk_victim_sleep_us(int64_t usec) {
    struct timespec request;
    request.tv_sec = (time_t) (usec / (INT64_C(1000) * INT64_C(1000)));
    request.tv_nsec = (long) ((usec % (INT64_C(1000) * INT64_C(1000))) * INT64_C(1000));
    (void) nanosleep(&request, NULL);
}

/**
 * @brief 把等待毫秒换算为绝对 deadline。
 * @param timeout_ms 相对毫秒（>0）。
 * @return 绝对毫秒 deadline。
 */
static int64_t glk_victim_deadline(int32_t timeout_ms) {
    return glk_victim_now_ms() + (int64_t) timeout_ms;
}

/**
 * @brief 有界 poll（等待事件）。
 * @param fd 目标 fd。
 * @param events POLLIN/POLLOUT。
 * @param deadline_ms 绝对毫秒 deadline。
 * @return 1=事件就绪；0=超时；-1=系统错误（errno）。
 */
static int32_t glk_victim_poll_until(int32_t fd, short events,
                                     int64_t deadline_ms) {
    for (;;) {
        const int64_t remaining = deadline_ms - glk_victim_now_ms();
        if (remaining <= 0) return 0;
        struct pollfd item;
        memset(&item, 0, sizeof(item));
        item.fd = fd;
        item.events = events;
        const int32_t timeout = remaining > INT32_MAX ? INT32_MAX : (int32_t) remaining;
        const int32_t ready = poll(&item, 1, timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ready == 0) return 0;
        return 1;
    }
}

/**
 * @brief 有界精确读取（阻塞到读满/EOF/超时）。
 * @param fd 源 fd。
 * @param out 输出缓冲。
 * @param length 目标字节数。
 * @param deadline_ms 绝对毫秒 deadline。
 * @return GLK_VICTIM_OK / TIMEOUT / EOF / SYSTEM。
 */
static int32_t glk_victim_read_exact_until(int32_t fd, uint8_t *out,
                                           uint32_t length,
                                           int64_t deadline_ms) {
    uint32_t done = 0;
    while (done < length) {
        const int32_t ready = glk_victim_poll_until(fd, POLLIN, deadline_ms);
        if (ready == 0) return GLK_VICTIM_ERR_TIMEOUT;
        if (ready < 0) return GLK_VICTIM_ERR_SYSTEM;
        const ssize_t read_size = read(fd, out + done, (size_t) (length - done));
        if (read_size == 0) return GLK_VICTIM_ERR_EOF;
        if (read_size < 0) {
            if (errno == EINTR) continue;
            return GLK_VICTIM_ERR_SYSTEM;
        }
        done += (uint32_t) read_size;
    }
    return GLK_VICTIM_OK;
}

/**
 * @brief 阻塞精确读取（helper 侧/等待路径；无超时）。
 * @param fd 源 fd。
 * @param out 输出缓冲。
 * @param length 目标字节数。
 * @return GLK_VICTIM_OK / EOF / SYSTEM。
 */
static int32_t glk_victim_read_exact(int32_t fd, uint8_t *out,
                                     uint32_t length) {
    uint32_t done = 0;
    while (done < length) {
        const ssize_t read_size = read(fd, out + done, (size_t) (length - done));
        if (read_size == 0) return GLK_VICTIM_ERR_EOF;
        if (read_size < 0) {
            if (errno == EINTR) continue;
            return GLK_VICTIM_ERR_SYSTEM;
        }
        done += (uint32_t) read_size;
    }
    return GLK_VICTIM_OK;
}

/**
 * @brief 有界精确写入（阻塞到写满/超时）。
 * @param fd 目标 fd。
 * @param bytes 源字节。
 * @param length 目标字节数。
 * @param deadline_ms 绝对毫秒 deadline。
 * @return GLK_VICTIM_OK / TIMEOUT / EOF / SYSTEM。
 */
static int32_t glk_victim_write_exact_until(int32_t fd, const uint8_t *bytes,
                                            uint32_t length,
                                            int64_t deadline_ms) {
    uint32_t done = 0;
    while (done < length) {
        const int32_t ready = glk_victim_poll_until(fd, POLLOUT, deadline_ms);
        if (ready == 0) return GLK_VICTIM_ERR_TIMEOUT;
        if (ready < 0) return GLK_VICTIM_ERR_SYSTEM;
        const ssize_t written = write(fd, bytes + done, (size_t) (length - done));
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EPIPE) return GLK_VICTIM_ERR_EOF;
            return GLK_VICTIM_ERR_SYSTEM;
        }
        done += (uint32_t) written;
    }
    return GLK_VICTIM_OK;
}

/**
 * @brief 读小端 u32（协议长度前缀）。
 * @param bytes 输入（至少 4 字节）。
 * @return 值。
 */
static uint32_t glk_victim_load_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) |
           ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
}

/**
 * @brief 写小端 u32（协议长度前缀）。
 * @param bytes 输出（至少 4 字节）。
 * @param value 值。
 */
static void glk_victim_store_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) (value & (uint32_t) UINT8_MAX);
    bytes[1] = (uint8_t) ((value >> 8) & (uint32_t) UINT8_MAX);
    bytes[2] = (uint8_t) ((value >> 16) & (uint32_t) UINT8_MAX);
    bytes[3] = (uint8_t) ((value >> 24) & (uint32_t) UINT8_MAX);
}

/**
 * @brief 校验线帧外层边界（前缀长度 vs 总长度 vs body 上限）。
 * @param frame 线帧。
 * @param frame_len 总长度。
 * @return 1=合法；0=非法。
 */
static int32_t glk_victim_frame_valid(const uint8_t *frame,
                                      uint32_t frame_len) {
    if (frame == NULL || frame_len < GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES) {
        return 0;
    }
    const uint32_t body_len = glk_victim_load_u32(frame);
    if (body_len < GLK_VICTIM_BODY_OVERHEAD_BYTES ||
        body_len > GLK_VICTIM_FRAME_MAX_BODY) {
        return 0;
    }
    return GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES + body_len == frame_len;
}

/* ------------------------------------------------------------------------- */
/* E4 零 fork ①：链接期 --wrap 计数（Debug/measurement only）                  */
/* ------------------------------------------------------------------------- */
/*
 * 严格 ② 口径（task-60）：CMake 只在 Debug 传 `-Wl,--wrap=fork/vfork` 并定义
 * `GLK_VICTIM_FORK_WRAP=1`；Release/RelWithDebInfo 不传 wrap、不定义该宏，
 * 因而产物不引用 `__wrap_…`/`__real_…`，也不导入 fork/vfork（见 tools 验证脚本）。
 * 计数 API 在 Release 保持可用（恒 0、reset no-op），不改变调用方语义。
 */
#if defined(GLK_VICTIM_FORK_WRAP)
/** @brief fork 调用累计计数（debug 证据；生产路径必须恒 0）。 */
static _Atomic uint32_t g_glk_victim_fork_count = ATOMIC_VAR_INIT(0);
/** @brief vfork 调用累计计数（debug 证据；生产路径必须恒 0）。 */
static _Atomic uint32_t g_glk_victim_vfork_count = ATOMIC_VAR_INIT(0);

/** @brief libc 真实 fork（由链接器 `--wrap=fork` 提供；仅 Debug）。 */
extern pid_t __real_fork(void);
/** @brief libc 真实 vfork（由链接器 `--wrap=vfork` 提供；仅 Debug）。 */
extern pid_t __real_vfork(void);

pid_t __wrap_fork(void) {
    (void) atomic_fetch_add_explicit(&g_glk_victim_fork_count, 1u,
                                     memory_order_relaxed);
    return __real_fork();
}

pid_t __wrap_vfork(void) {
    (void) atomic_fetch_add_explicit(&g_glk_victim_vfork_count, 1u,
                                     memory_order_relaxed);
    return __real_vfork();
}
#endif /* GLK_VICTIM_FORK_WRAP */

uint32_t glk_victim_fork_count(void) {
#if defined(GLK_VICTIM_FORK_WRAP)
    return atomic_load_explicit(&g_glk_victim_fork_count, memory_order_relaxed);
#else
    return 0U;
#endif
}

uint32_t glk_victim_vfork_count(void) {
#if defined(GLK_VICTIM_FORK_WRAP)
    return atomic_load_explicit(&g_glk_victim_vfork_count, memory_order_relaxed);
#else
    return 0U;
#endif
}

void glk_victim_fork_count_reset(void) {
#if defined(GLK_VICTIM_FORK_WRAP)
    atomic_store_explicit(&g_glk_victim_fork_count, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_glk_victim_vfork_count, 0u, memory_order_relaxed);
#endif
}

uint32_t glk_victim_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = UINT32_MAX;
    if (data == NULL && len != 0) return 0;
    for (size_t index = 0; index < len; ++index) {
        crc ^= (uint32_t) data[index];
        for (int32_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t) -(int32_t) (crc & UINT32_C(1));
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return crc ^ UINT32_MAX;
}

int32_t glk_victim_write_frame_fd(int32_t fd, const uint8_t *frame,
                                  uint32_t frame_len) {
    if (fd < 0 || !glk_victim_frame_valid(frame, frame_len)) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    uint8_t prefix[GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES];
    glk_victim_store_u32(prefix, frame_len - GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES);
    /* 先写长度前缀，再写 body：helper/parent 两侧都按流顺序读取。 */
    uint32_t done = 0;
    while (done < (uint32_t) sizeof(prefix)) {
        const ssize_t written =
            write(fd, prefix + done, sizeof(prefix) - (size_t) done);
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EPIPE) return GLK_VICTIM_ERR_EOF;
            return GLK_VICTIM_ERR_SYSTEM;
        }
        done += (uint32_t) written;
    }
    done = 0;
    const uint8_t *body = frame + GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES;
    const uint32_t body_len = frame_len - GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES;
    while (done < body_len) {
        const ssize_t written = write(fd, body + done, (size_t) (body_len - done));
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EPIPE) return GLK_VICTIM_ERR_EOF;
            return GLK_VICTIM_ERR_SYSTEM;
        }
        done += (uint32_t) written;
    }
    return GLK_VICTIM_OK;
}

int32_t glk_victim_read_frame_fd(int32_t fd, uint8_t *out_frame,
                                 uint32_t capacity, uint32_t *out_len) {
    if (fd < 0 || out_frame == NULL || out_len == NULL ||
        capacity < GLK_VICTIM_FRAME_MAX_WIRE) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    uint8_t prefix[GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES];
    int32_t status = glk_victim_read_exact(fd, prefix, sizeof(prefix));
    if (status != GLK_VICTIM_OK) return status;
    const uint32_t body_len = glk_victim_load_u32(prefix);
    if (body_len < GLK_VICTIM_BODY_OVERHEAD_BYTES ||
        body_len > GLK_VICTIM_FRAME_MAX_BODY) {
        return GLK_VICTIM_ERR_BAD_FRAME;
    }
    memcpy(out_frame, prefix, sizeof(prefix));
    status = glk_victim_read_exact(
        fd, out_frame + GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES, body_len);
    if (status != GLK_VICTIM_OK) return status;
    *out_len = GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES + body_len;
    return GLK_VICTIM_OK;
}

glk_victim_token glk_victim_spawn(const struct glk_victim_spawn_request *request,
                                  struct glk_victim_spawn_result *result) {
    if (result != NULL) {
        result->pid = 0;
        result->system_error = 0;
    }
    if (request == NULL || result == NULL || request->helper_path == NULL ||
        request->helper_path[0] == '\0' || request->argv == NULL ||
        request->argv[0] == NULL) {
        return 0;
    }
    if (request->raw_size < GLK_VICTIM_RAW_HEADER_BYTES ||
        request->raw_size > (uint64_t) INT64_MAX) {
        return 0;
    }
    int32_t control_pair[2] = {-1, -1};
    /* fd3 契约是双向控制通道（helper 读命令 + 写回复）：必须用 socketpair，
     * 单向 pipe2 会让 helper 回复写读端失败（E4 实测 EBADF/-1005）。 */
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, control_pair) != 0) {
        if (result != NULL) result->system_error = errno;
        return 0;
    }
    const int32_t raw_fd = (int32_t) syscall(SYS_memfd_create,
                                             "glk-victim-raw", MFD_CLOEXEC);
    if (raw_fd < 0) {
        result->system_error = errno;
        (void) close(control_pair[0]);
        (void) close(control_pair[1]);
        return 0;
    }
    if (ftruncate(raw_fd, (off_t) request->raw_size) != 0) {
        result->system_error = errno;
        (void) close(raw_fd);
        (void) close(control_pair[0]);
        (void) close(control_pair[1]);
        return 0;
    }
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_t actions;
    int32_t attributes_ready = 0;
    int32_t actions_ready = 0;
    int32_t spawn_status = posix_spawnattr_init(&attributes);
    if (spawn_status == 0) {
        attributes_ready = 1;
        spawn_status = posix_spawn_file_actions_init(&actions);
        if (spawn_status == 0) actions_ready = 1;
    }
    if (spawn_status == 0) {
        /* E4/A16 实测：posix_spawnattr_setflags(CLOEXEC_DEFAULT) 在本设备会让
         * bionic ApplyAttrs 在 vfork 子进程内 SIGSEGV（栈顶 libsigchain/
         * sigaction64）；不设置 attrs flags，fd 纪律由下列 addclose+adddup2 与
         * helper close_range(5,~0) 最终收敛（fd 集合 {0,1,2,3,4}）。 */
#if defined(GLK_VICTIM_SPAWN_ATTR_CLOEXEC_DEFAULT) && \
    defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
        spawn_status = posix_spawnattr_setflags(&attributes,
                                                POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
    }
    /* N02 修订（A16/mondrian E4 实测）：**不 addclose** 任何 fd ——
     * control_pair/raw_fd 的编号可能恰好是 3/4（A16 上实测为 3/4/5），
     * addclose 会先关掉后续 adddup2 的源 fd，导致 helper 起不来/通道异常。
     * fd 收敛交给：adddup2(control→3, raw→4) 覆盖 3/4，helper 启动
     * close_range(5,~0) 清掉其余继承 fd（最终集合 {0,1,2,3,4}）。 */
    if (spawn_status == 0) {
        /* control→3（读端）、raw→4。 */
        spawn_status = posix_spawn_file_actions_adddup2(
            &actions, control_pair[0], GLK_VICTIM_CONTROL_FD);
    }
    if (spawn_status == 0) {
        spawn_status = posix_spawn_file_actions_adddup2(
            &actions, raw_fd, GLK_VICTIM_RAW_FD);
    }
    pid_t child = 0;
    if (spawn_status == 0) {
        /* A16 实测：传 &attributes 时 bionic ApplyAttrs 在 vfork 子进程内 SIGSEGV
         * （libsigchain/sigaction64），即使 flags=0；传 NULL attrs 走 bionic 默认
         * 属性，不影响 fd 纪律（file_actions + helper close_range 已覆盖）。 */
        spawn_status = posix_spawn(
            &child, request->helper_path, &actions, NULL,
            (char *const *) request->argv,
            request->envp == NULL ? environ : (char *const *) request->envp);
    }
    if (actions_ready != 0) (void) posix_spawn_file_actions_destroy(&actions);
    if (attributes_ready != 0) (void) posix_spawnattr_destroy(&attributes);
    if (spawn_status != 0) {
        result->system_error = spawn_status;
        (void) close(raw_fd);
        (void) close(control_pair[0]);
        (void) close(control_pair[1]);
        return 0;
    }
    /* 父进程只保留 control 写端与 raw memfd；control 读端归子进程 fd 3。 */
    (void) close(control_pair[0]);
    struct glk_victim_handle_impl *handle =
        (struct glk_victim_handle_impl *) calloc(1, sizeof(*handle));
    if (handle == NULL) {
        (void) kill(child, SIGKILL);
        (void) waitpid(child, NULL, 0);
        (void) close(raw_fd);
        (void) close(control_pair[1]);
        result->system_error = ENOMEM;
        return 0;
    }
    handle->control_fd = control_pair[1];
    handle->raw_fd = raw_fd;
    handle->pid = (int32_t) child;
    handle->reaped = 0;
    handle->exit_code = -1;
    handle->raw_size = request->raw_size;
    result->pid = handle->pid;
    return (glk_victim_token) (uintptr_t) handle;
}

int32_t glk_victim_send(glk_victim_token token, const uint8_t *frame,
                        uint32_t frame_len, int32_t timeout_ms) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL || handle->control_fd < 0 || timeout_ms <= 0 ||
        !glk_victim_frame_valid(frame, frame_len)) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    return glk_victim_write_exact_until(handle->control_fd, frame, frame_len,
                                        glk_victim_deadline(timeout_ms));
}

int32_t glk_victim_receive(glk_victim_token token, uint8_t *out_frame,
                           uint32_t capacity, uint32_t *out_len,
                           int32_t timeout_ms) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL || handle->control_fd < 0 || out_frame == NULL ||
        out_len == NULL || timeout_ms <= 0 ||
        capacity < GLK_VICTIM_FRAME_MAX_WIRE) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    const int64_t deadline = glk_victim_deadline(timeout_ms);
    uint8_t prefix[GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES];
    int32_t status =
        glk_victim_read_exact_until(handle->control_fd, prefix, sizeof(prefix), deadline);
    if (status != GLK_VICTIM_OK) return status;
    const uint32_t body_len = glk_victim_load_u32(prefix);
    if (body_len < GLK_VICTIM_BODY_OVERHEAD_BYTES ||
        body_len > GLK_VICTIM_FRAME_MAX_BODY) {
        return GLK_VICTIM_ERR_BAD_FRAME;
    }
    if (GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES + body_len > capacity) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    memcpy(out_frame, prefix, sizeof(prefix));
    status = glk_victim_read_exact_until(
        handle->control_fd, out_frame + GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES,
        body_len, deadline);
    if (status != GLK_VICTIM_OK) return status;
    *out_len = GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES + body_len;
    return GLK_VICTIM_OK;
}

int32_t glk_victim_read_raw(glk_victim_token token, uint64_t offset,
                            uint8_t *out, uint32_t length) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL || handle->raw_fd < 0 || out == NULL || length == 0) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    if (offset > handle->raw_size ||
        (uint64_t) length > handle->raw_size - offset) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    uint32_t done = 0;
    while (done < length) {
        const ssize_t read_size = pread(handle->raw_fd, out + done,
                                        (size_t) (length - done),
                                        (off_t) (offset + done));
        if (read_size < 0) {
            if (errno == EINTR) continue;
            return GLK_VICTIM_ERR_SYSTEM;
        }
        if (read_size == 0) return GLK_VICTIM_ERR_EOF;
        done += (uint32_t) read_size;
    }
    return GLK_VICTIM_OK;
}

int32_t glk_victim_raw_size(glk_victim_token token, uint64_t *out_size) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL || out_size == NULL) return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    *out_size = handle->raw_size;
    return GLK_VICTIM_OK;
}

int32_t glk_victim_pid(glk_victim_token token, uint32_t *out_pid) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL || out_pid == NULL) return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    *out_pid = (uint32_t) handle->pid;
    return GLK_VICTIM_OK;
}

/**
 * @brief 把 waitpid status 换算为机械退出码。
 * @param status waitpid 输出。
 * @return WIFEXITED?exit:128+signal。
 */
static int32_t glk_victim_exit_code(int32_t status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/**
 * @brief 有界 waitpid 并缓存退出码。
 * @param handle 非空内部 handle。
 * @param deadline_ms 绝对 deadline。
 * @param out_exit 非空。
 * @return GLK_VICTIM_OK / TIMEOUT / WAIT 错误。
 */
static int32_t glk_victim_wait_impl(struct glk_victim_handle_impl *handle,
                                    int64_t deadline_ms, int32_t *out_exit) {
    if (handle->reaped != 0) {
        *out_exit = handle->exit_code;
        return GLK_VICTIM_OK;
    }
    if (handle->pid <= 0) return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    for (;;) {
        int32_t status = 0;
        const pid_t waited = waitpid((pid_t) handle->pid, &status, WNOHANG);
        if (waited == (pid_t) handle->pid) {
            handle->reaped = 1;
            handle->exit_code = glk_victim_exit_code(status);
            *out_exit = handle->exit_code;
            return GLK_VICTIM_OK;
        }
        if (waited < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) {
                handle->reaped = 1;
                *out_exit = handle->exit_code;
                return GLK_VICTIM_OK;
            }
            return GLK_VICTIM_ERR_SYSTEM;
        }
        if (glk_victim_now_ms() >= deadline_ms) return GLK_VICTIM_ERR_TIMEOUT;
        glk_victim_sleep_us(INT64_C(1000));
    }
}

int32_t glk_victim_wait(glk_victim_token token, int32_t timeout_ms,
                        int32_t *out_exit) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL || out_exit == NULL || timeout_ms <= 0) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    return glk_victim_wait_impl(handle, glk_victim_deadline(timeout_ms), out_exit);
}

int32_t glk_victim_kill(glk_victim_token token, int32_t *out_exit) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL || out_exit == NULL) return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    if (handle->reaped == 0 && handle->pid > 0) {
        if (kill((pid_t) handle->pid, SIGKILL) != 0 && errno != ESRCH) {
            return GLK_VICTIM_ERR_SYSTEM;
        }
        const int32_t waited =
            glk_victim_wait_impl(handle, glk_victim_deadline(2000), out_exit);
        if (waited != GLK_VICTIM_OK) return waited;
    } else {
        *out_exit = handle->exit_code;
    }
    return GLK_VICTIM_OK;
}

/**
 * @brief 关闭父侧 fd 并 free handle（不 kill/wait）。
 * @param handle 非空内部 handle。
 */
static void glk_victim_release_handle(struct glk_victim_handle_impl *handle) {
    if (handle->control_fd >= 0) {
        (void) close(handle->control_fd);
        handle->control_fd = -1;
    }
    if (handle->raw_fd >= 0) {
        (void) close(handle->raw_fd);
        handle->raw_fd = -1;
    }
    free(handle);
}

int32_t glk_victim_detach(glk_victim_token token) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL) return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    /* rooted child 驻留：只释放父侧传输资源，不 kill、不 waitpid。 */
    glk_victim_release_handle(handle);
    return GLK_VICTIM_OK;
}

int32_t glk_victim_close(glk_victim_token token) {
    struct glk_victim_handle_impl *handle = glk_victim_from_token(token);
    if (handle == NULL) return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    if (handle->reaped == 0 && handle->pid > 0) {
        (void) glk_victim_kill(token, &handle->exit_code);
    }
    glk_victim_release_handle(handle);
    return GLK_VICTIM_OK;
}
