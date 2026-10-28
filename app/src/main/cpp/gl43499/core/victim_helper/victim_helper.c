/**
 * @file victim_helper.c
 * @brief 通用 victim helper PIE：命令循环 + 机械能力（probeUid/perfSelfSample/execScript/park/bye）。
 *
 * 设计约束（批 A，task-52）：
 * - **零 fork**：自身由父进程 posix_spawn 启动；execScript 也只用 posix_spawn；
 * - **零业务逻辑/零状态机/零解析**：不解析 perf 样本、不投票、不选址、不重试；
 *   perf 只把采样环整块（按请求容量截断）写入 raw memfd；execScript 只回退出码/输出；
 * - **fd 契约**：control=3、raw=4；启动后关闭 fd≥5 并断言残留=EBADF；
 * - **无 backend 私有依赖**：只 include 通用 `victim_protocol.h` + libc/linux 头。
 *
 * @note 允许 libc malloc/free（I2：helper 是 exec 后新单线程进程，不复制 JVM 地址空间）。
 * @note 阶段：批 A（task-52）；真机 exec/uid/seccomp 属 E4/E5，未验证。
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "victim_protocol.h"

extern char **environ;

/** @brief 输出排空/读取分块（机制 IO 大小，非目标参数）。 */
#define GLK_HELPER_IO_CHUNK_BYTES 1024u

/** @brief helper 控制帧缓冲（栈上；WIRE_MAX 很小）。 */
struct glk_helper_buffer {
    uint8_t bytes[GLK_VICTIM_FRAME_MAX_WIRE];
    uint32_t length;
};

/**
 * @brief 读小端 u16。
 * @param bytes 输入（≥2 字节）。
 * @return 值。
 */
static uint16_t glk_helper_load_u16(const uint8_t *bytes) {
    return (uint16_t) ((uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8));
}

/**
 * @brief 读小端 u32。
 * @param bytes 输入（≥4 字节）。
 * @return 值。
 */
static uint32_t glk_helper_load_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) |
           ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
}

/**
 * @brief 读小端 u64。
 * @param bytes 输入（≥8 字节）。
 * @return 值。
 */
static uint64_t glk_helper_load_u64(const uint8_t *bytes) {
    uint64_t value = 0;
    for (int32_t index = 7; index >= 0; --index) {
        value = (value << 8) | (uint64_t) bytes[index];
    }
    return value;
}

/**
 * @brief 写小端 u16。
 * @param bytes 输出（≥2 字节）。
 * @param value 值。
 */
static void glk_helper_store_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) (value & (uint16_t) UINT8_MAX);
    bytes[1] = (uint8_t) ((value >> 8) & (uint16_t) UINT8_MAX);
}

/**
 * @brief 写小端 u32。
 * @param bytes 输出（≥4 字节）。
 * @param value 值。
 */
static void glk_helper_store_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) (value & (uint32_t) UINT8_MAX);
    bytes[1] = (uint8_t) ((value >> 8) & (uint32_t) UINT8_MAX);
    bytes[2] = (uint8_t) ((value >> 16) & (uint32_t) UINT8_MAX);
    bytes[3] = (uint8_t) ((value >> 24) & (uint32_t) UINT8_MAX);
}

/**
 * @brief 写小端 u64。
 * @param bytes 输出（≥8 字节）。
 * @param value 值。
 */
static void glk_helper_store_u64(uint8_t *bytes, uint64_t value) {
    for (int32_t index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t) ((value >> (8 * index)) & (uint64_t) UINT8_MAX);
    }
}

/**
 * @brief 单调毫秒时钟（有界等待用）。
 * @return 毫秒。
 */
static int64_t glk_helper_now_ms(void) {
    struct timespec now;
    (void) clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t) now.tv_sec * INT64_C(1000) + (int64_t) now.tv_nsec / (INT64_C(1000) * INT64_C(1000));
}

/**
 * @brief 有界 sleep（毫秒）。
 * @param ms 毫秒。
 */
static void glk_helper_sleep_ms(int64_t ms) {
    struct timespec request;
    request.tv_sec = (time_t) (ms / INT64_C(1000));
    request.tv_nsec = (long) ((ms % INT64_C(1000)) * (INT64_C(1000) * INT64_C(1000)));
    (void) nanosleep(&request, NULL);
}

/**
 * @brief 把 waitpid status 换算为机械退出码。
 * @param status waitpid 输出。
 * @return WIFEXITED?exit:128+signal。
 */
static int32_t glk_helper_exit_code(int32_t status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/**
 * @brief 构造并写入一个回复帧。
 *
 * @param kind_payload payload 区（status+data；调用方已组装）。
 * @param payload_len payload 长度。
 * @return 0=成功；否则 C API 错误。
 * @note 回复帧 body 的 version/kind/payloadLen/crc 由本函数统一封装。
 */
static int32_t glk_helper_write_reply(const uint8_t *payload,
                                      uint32_t payload_len) {
    if (payload == NULL || payload_len > GLK_VICTIM_FRAME_MAX_BODY -
                                             GLK_VICTIM_BODY_OVERHEAD_BYTES) {
        return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    }
    struct glk_helper_buffer frame;
    (void) memset(&frame, 0, sizeof(frame));
    const uint32_t body_len = GLK_VICTIM_BODY_OVERHEAD_BYTES + payload_len;
    frame.length = GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES + body_len;
    uint8_t *prefix = frame.bytes;
    uint8_t *body = frame.bytes + GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES;
    glk_helper_store_u32(prefix, body_len);
    body[0] = (uint8_t) GLK_VICTIM_PROTOCOL_VERSION;
    body[1] = (uint8_t) GLK_VICTIM_FRAME_KIND_REPLY;
    glk_helper_store_u16(body + 2, (uint16_t) payload_len);
    if (payload_len != 0) memcpy(body + 4, payload, payload_len);
    const uint32_t crc = glk_victim_crc32(body, GLK_VICTIM_BODY_HEADER_BYTES + payload_len);
    glk_helper_store_u32(body + GLK_VICTIM_BODY_HEADER_BYTES + payload_len, crc);
    return glk_victim_write_frame_fd(GLK_VICTIM_CONTROL_FD, frame.bytes,
                                     frame.length);
}

/**
 * @brief 写一个 OK 回复（status + data）。
 *
 * @param data 数据区（可为 NULL/0）。
 * @param data_len 数据长度。
 * @return 0=成功；否则 C API 错误。
 */
static int32_t glk_helper_reply_ok(const uint8_t *data, uint32_t data_len) {
    uint8_t payload[GLK_VICTIM_FRAME_MAX_BODY];
    if (data_len > sizeof(payload) - 1) return GLK_VICTIM_ERR_INVALID_ARGUMENT;
    payload[0] = (uint8_t) GLK_VICTIM_STATUS_OK;
    if (data_len != 0) memcpy(payload + 1, data, data_len);
    return glk_helper_write_reply(payload, data_len + 1);
}

/**
 * @brief 写一个 ERROR 回复（status + error code + errno）。
 *
 * @param error_code GLK_VICTIM_ERROR_*。
 * @param system_error 机制 errno（0=无）。
 * @return 0=成功；否则 C API 错误。
 */
static int32_t glk_helper_reply_error(uint32_t error_code,
                                      int32_t system_error) {
    uint8_t payload[6];
    payload[0] = (uint8_t) GLK_VICTIM_STATUS_ERROR;
    payload[1] = (uint8_t) error_code;
    glk_helper_store_u32(payload + 2, (uint32_t) system_error);
    return glk_helper_write_reply(payload, sizeof(payload));
}

/**
 * @brief 关闭 fd≥5 并断言残留 fd 不可用（fd 契约自检）。
 *
 * 优先 `close_range(5, ~0)`；内核不支持时退化为逐 fd close（上限用 RLIMIT_NOFILE）。
 *
 * @return 0=自检通过；1=契约破坏。
 */
static int32_t glk_helper_converge_fds(void) {
#if defined(SYS_close_range)
    if (syscall(SYS_close_range, 5u, ~0u, 0u) == 0) {
        /* 断言：5 号 fd 必须已不可用（典型 close_range 路径）。 */
        errno = 0;
        return fcntl(5, F_GETFD) == -1 && errno == EBADF ? 0 : 1;
    }
#endif
    long limit = sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > 65536L) limit = 65536L;
    for (int32_t fd = 5; fd < (int32_t) limit; ++fd) {
        (void) close(fd);
    }
    errno = 0;
    if (fcntl(5, F_GETFD) != -1) return 1;
    return errno == EBADF ? 0 : 1;
}

/**
 * @brief 启动断言：control=3/raw=4 必须有效。
 * @return 0=通过；1=契约破坏。
 */
static int32_t glk_helper_assert_contract_fds(void) {
    if (fcntl(GLK_VICTIM_CONTROL_FD, F_GETFD) < 0) return 1;
    if (fcntl(GLK_VICTIM_RAW_FD, F_GETFD) < 0) return 1;
    return 0;
}

/**
 * @brief 在 raw memfd 指定偏移写 raw 帧头 + 数据。
 *
 * @param offset 写入起点（16 对齐）。
 * @param data 数据（可为 NULL/0）。
 * @param data_len 数据长度。
 * @param truncated 是否截断。
 * @return 0=成功；否则 errno。
 */
static int32_t glk_helper_write_raw(uint64_t offset, const uint8_t *data,
                                    uint32_t data_len, int32_t truncated) {
    uint8_t header[GLK_VICTIM_RAW_HEADER_BYTES];
    glk_helper_store_u32(header, GLK_VICTIM_RAW_MAGIC);
    glk_helper_store_u16(header + 4, (uint16_t) GLK_VICTIM_RAW_VERSION);
    glk_helper_store_u16(header + 6,
                         (uint16_t) (truncated != 0 ? GLK_VICTIM_RAW_FLAG_TRUNCATED : 0u));
    glk_helper_store_u64(header + 8, (uint64_t) data_len);
    uint32_t done = 0;
    while (done < (uint32_t) sizeof(header)) {
        const ssize_t written =
            pwrite(GLK_VICTIM_RAW_FD, header + done,
                   sizeof(header) - (size_t) done, (off_t) (offset + done));
        if (written < 0) {
            if (errno == EINTR) continue;
            return errno;
        }
        done += (uint32_t) written;
    }
    done = 0;
    while (done < data_len) {
        const ssize_t written = pwrite(GLK_VICTIM_RAW_FD, data + done,
                                       (size_t) (data_len - done),
                                       (off_t) (offset + sizeof(header) + done));
        if (written < 0) {
            if (errno == EINTR) continue;
            return errno;
        }
        done += (uint32_t) written;
    }
    return 0;
}

/**
 * @brief @return raw memfd 容量（字节）。
 */
static uint64_t glk_helper_raw_size(void) {
    struct stat info;
    if (fstat(GLK_VICTIM_RAW_FD, &info) != 0) return 0;
    return (uint64_t) info.st_size;
}

/**
 * @brief 处理 probeUid。
 * @return 0=成功写出回复；否则 C API 错误。
 */
static int32_t glk_helper_probe_uid(void) {
    uint8_t data[4];
    glk_helper_store_u32(data, (uint32_t) getuid());
    return glk_helper_reply_ok(data, sizeof(data));
}

/**
 * @brief 处理 probeStatus：读 `/proc/self/status` raw（≤4 KiB；超限 IO err，不截断）。
 *
 * @return 0=成功写出回复；否则 C API 错误。
 * @note 机制-only：不做解析/判定；raw 在 (4087,4096] 超出 frame v1 单帧回复能力
 *   ⇒ IO err（不分片/不截断当证据，I3）。
 */
static int32_t glk_helper_probe_status(void) {
    const int32_t fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return glk_helper_reply_error(GLK_VICTIM_ERROR_IO, errno);
    uint8_t data[GLK_VICTIM_PROBE_STATUS_MAX_BYTES];
    uint32_t total = 0;
    int32_t failure = 0;
    int32_t system_error = 0;
    for (;;) {
        const ssize_t got = read(fd, data + total, (size_t) (sizeof(data) - total));
        if (got < 0) {
            if (errno == EINTR) continue;
            failure = GLK_VICTIM_ERROR_IO;
            system_error = errno;
            break;
        }
        if (got == 0) break;
        total += (uint32_t) got;
        if (total == (uint32_t) sizeof(data)) {
            uint8_t extra = 0;
            const ssize_t more = read(fd, &extra, 1u);
            if (more < 0) {
                failure = GLK_VICTIM_ERROR_IO;
                system_error = errno;
            } else if (more > 0) {
                failure = GLK_VICTIM_ERROR_IO;
                system_error = EOVERFLOW;
            }
            break;
        }
    }
    (void) close(fd);
    if (failure != 0) return glk_helper_reply_error((uint32_t) failure, system_error);
    if (total > GLK_VICTIM_PROBE_STATUS_MAX_REPLY_BYTES) {
        return glk_helper_reply_error(GLK_VICTIM_ERROR_IO, ERANGE);
    }
    return glk_helper_reply_ok(data, total);
}

/**
 * @brief 处理 probeCommSet：写 self comm marker（15B 规则；不解析业务）。
 *
 * @param args marker 字节（调用方已编码）。
 * @param args_len marker 长度。
 * @return 0=成功写出回复；否则 C API 错误。
 * @note 只接受 `[A-Za-z0-9._-]`（与 Kotlin 行协议校验一致）；超 15B/非法 ⇒ BAD_ARGS。
 */
static int32_t glk_helper_probe_comm_set(const uint8_t *args, uint32_t args_len) {
    if (args == NULL || args_len == 0 ||
        args_len > GLK_VICTIM_PROBE_COMM_MAX_BYTES) {
        return glk_helper_reply_error(GLK_VICTIM_ERROR_BAD_ARGS, EINVAL);
    }
    for (uint32_t index = 0; index < args_len; ++index) {
        const uint8_t value = args[index];
        const int32_t allowed =
            (value >= (uint8_t) 'A' && value <= (uint8_t) 'Z') ||
            (value >= (uint8_t) 'a' && value <= (uint8_t) 'z') ||
            (value >= (uint8_t) '0' && value <= (uint8_t) '9') ||
            value == (uint8_t) '.' || value == (uint8_t) '_' ||
            value == (uint8_t) '-';
        if (!allowed) return glk_helper_reply_error(GLK_VICTIM_ERROR_BAD_ARGS, EINVAL);
    }
    const int32_t fd = open("/proc/self/comm", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return glk_helper_reply_error(GLK_VICTIM_ERROR_IO, errno);
    uint32_t done = 0;
    int32_t failure = 0;
    int32_t system_error = 0;
    while (done < args_len) {
        const ssize_t written = write(fd, args + done, (size_t) (args_len - done));
        if (written < 0) {
            if (errno == EINTR) continue;
            failure = GLK_VICTIM_ERROR_IO;
            system_error = errno;
            break;
        }
        done += (uint32_t) written;
    }
    if (close(fd) != 0 && failure == 0) {
        failure = GLK_VICTIM_ERROR_IO;
        system_error = errno;
    }
    if (failure != 0) return glk_helper_reply_error((uint32_t) failure, system_error);
    return glk_helper_reply_ok(NULL, 0);
}

/**
 * @brief 处理 probeComm：读 self comm 长度/首字节/bytes（len0/8/15 可判定）。
 *
 * @return 0=成功写出回复；否则 C API 错误。
 * @note 回复 data = `[u8 len][u8 c0][len bytes]`；去尾部 `\n`/`\0`；
 *   超 15B ⇒ IO err（不截断）；len0 时 c0=0 且无 bytes。
 */
static int32_t glk_helper_probe_comm(void) {
    const int32_t fd = open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return glk_helper_reply_error(GLK_VICTIM_ERROR_IO, errno);
    uint8_t raw[GLK_VICTIM_PROBE_COMM_MAX_BYTES + 2u];
    uint32_t total = 0;
    int32_t failure = 0;
    int32_t system_error = 0;
    for (;;) {
        const ssize_t got = read(fd, raw + total, (size_t) (sizeof(raw) - total));
        if (got < 0) {
            if (errno == EINTR) continue;
            failure = GLK_VICTIM_ERROR_IO;
            system_error = errno;
            break;
        }
        if (got == 0) break;
        total += (uint32_t) got;
        if (total == (uint32_t) sizeof(raw)) {
            uint8_t extra = 0;
            const ssize_t more = read(fd, &extra, 1u);
            if (more < 0) {
                failure = GLK_VICTIM_ERROR_IO;
                system_error = errno;
            } else if (more > 0) {
                failure = GLK_VICTIM_ERROR_IO;
                system_error = EOVERFLOW;
            }
            break;
        }
    }
    (void) close(fd);
    if (failure != 0) return glk_helper_reply_error((uint32_t) failure, system_error);
    while (total > 0 && (raw[total - 1u] == (uint8_t) '\n' ||
                         raw[total - 1u] == 0u)) {
        total--;
    }
    if (total > GLK_VICTIM_PROBE_COMM_MAX_BYTES) {
        return glk_helper_reply_error(GLK_VICTIM_ERROR_IO, ERANGE);
    }
    uint8_t data[2u + GLK_VICTIM_PROBE_COMM_MAX_BYTES];
    data[0] = (uint8_t) total;
    data[1] = total == 0 ? 0u : raw[0];
    if (total != 0) memcpy(data + 2, raw, total);
    return glk_helper_reply_ok(data, 2u + total);
}

/**
 * @brief 处理 perfSelfSample：只做 perf syscall + 环整块拷贝到 raw memfd。
 *
 * @param args 参数区（10×u64LE）。
 * @param args_len 参数长度（必须=80）。
 * @return 0=成功写出回复；否则 C API 错误。
 */
static int32_t glk_helper_perf_self_sample(const uint8_t *args,
                                           uint32_t args_len) {
    if (args_len != 10u * 8u) return glk_helper_reply_error(
        GLK_VICTIM_ERROR_BAD_ARGS, EINVAL);
    const uint64_t attr_type = glk_helper_load_u64(args + 0);
    const uint64_t attr_config = glk_helper_load_u64(args + 8);
    const uint64_t attr_sample_period = glk_helper_load_u64(args + 16);
    const uint64_t attr_sample_type = glk_helper_load_u64(args + 24);
    const uint64_t attr_sample_regs_intr = glk_helper_load_u64(args + 32);
    const uint64_t attr_flags = glk_helper_load_u64(args + 40);
    const uint64_t ring_pages = glk_helper_load_u64(args + 48);
    const uint64_t iterations = glk_helper_load_u64(args + 56);
    const uint64_t raw_offset = glk_helper_load_u64(args + 64);
    const uint64_t raw_capacity = glk_helper_load_u64(args + 72);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (ring_pages == 0 || iterations == 0 || page_size <= 0 ||
        raw_capacity < GLK_VICTIM_RAW_HEADER_BYTES ||
        raw_offset % GLK_VICTIM_RAW_HEADER_BYTES != 0 ||
        raw_capacity > glk_helper_raw_size() - raw_offset) {
        return glk_helper_reply_error(GLK_VICTIM_ERROR_BAD_ARGS, EINVAL);
    }
    const uint64_t data_size = ring_pages * (uint64_t) page_size;
    const uint64_t mmap_size = data_size + (uint64_t) page_size;
    struct perf_event_attr attr;
    (void) memset(&attr, 0, sizeof(attr));
    attr.size = (uint32_t) sizeof(attr);
    attr.type = (uint32_t) attr_type;
    attr.config = attr_config;
    attr.sample_period = attr_sample_period;
    attr.sample_type = attr_sample_type;
    attr.sample_regs_intr = attr_sample_regs_intr;
    attr.disabled = 1;
    /* attr_flags 对应 perf_event_open 第 5 个参数（syscall flags，非 attr 字段）。 */
    const int32_t perf_fd = (int32_t) syscall(SYS_perf_event_open, &attr,
                                              (pid_t) 0, -1, -1,
                                              (unsigned long) attr_flags);
    if (perf_fd < 0) {
        return glk_helper_reply_error(GLK_VICTIM_ERROR_PERF, errno);
    }
    void *ring = mmap(NULL, (size_t) mmap_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, perf_fd, 0);
    if (ring == MAP_FAILED) {
        const int32_t saved = errno;
        (void) close(perf_fd);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_PERF, saved);
    }
    int32_t system_error = 0;
    if (ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0) != 0 ||
        ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
        system_error = errno;
    }
    if (system_error == 0) {
        for (uint64_t index = 0; index < iterations; ++index) {
            (void) syscall(SYS_getpid);
        }
    }
    if (system_error == 0 && ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0) != 0) {
        system_error = errno;
    }
    if (system_error != 0) {
        (void) munmap(ring, (size_t) mmap_size);
        (void) close(perf_fd);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_PERF, system_error);
    }
    volatile uint64_t *meta = (volatile uint64_t *) ring;
    /* N03：与 perf.c:330-333 一致 —— acquire load data_head + acquire fence 后再
     * 读 ring 数据；tail 用 release store，防编译器/CPU 把数据读重排到 head 之前。 */
    const uint64_t data_head = __atomic_load_n(&meta[0], __ATOMIC_ACQUIRE);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint8_t *ring_data = (const uint8_t *) ring + page_size;
    const uint64_t capacity_body = raw_capacity - GLK_VICTIM_RAW_HEADER_BYTES;
    const uint64_t copy_len = data_size < capacity_body ? data_size : capacity_body;
    const int32_t truncated = copy_len < data_size ? 1 : 0;
    uint8_t *scratch = (uint8_t *) malloc((size_t) copy_len);
    if (scratch == NULL) {
        (void) munmap(ring, (size_t) mmap_size);
        (void) close(perf_fd);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_PERF, ENOMEM);
    }
    memcpy(scratch, ring_data, (size_t) copy_len);
    /* 释放环空间（tail=head）后卸载；数据已拷贝（release 与 acquire 配对）。 */
    __atomic_store_n(&meta[1], data_head, __ATOMIC_RELEASE);
    (void) munmap(ring, (size_t) mmap_size);
    (void) close(perf_fd);
    const int32_t write_error =
        glk_helper_write_raw(raw_offset, scratch, (uint32_t) copy_len, truncated);
    free(scratch);
    if (write_error != 0) {
        return glk_helper_reply_error(GLK_VICTIM_ERROR_IO, write_error);
    }
    uint8_t data[32];
    glk_helper_store_u64(data + 0, raw_offset);
    glk_helper_store_u64(data + 8, copy_len);
    glk_helper_store_u64(data + 16, data_head);
    glk_helper_store_u32(data + 24, 0);
    glk_helper_store_u32(data + 28,
                         truncated != 0 ? GLK_VICTIM_RAW_FLAG_TRUNCATED : 0u);
    return glk_helper_reply_ok(data, sizeof(data));
}

/**
 * @brief execScript 的子进程 argv 构造结果（malloc 所有权在本函数内）。
 */
struct glk_helper_argv {
    char **items;
    uint32_t count;
};

/**
 * @brief 解析协议参数区的长度前缀字节串。
 *
 * @param cursor 当前读取位置（前进）。
 * @param end 参数区结束位置。
 * @param out 输出串（malloc；调用方 free）。
 * @param out_len 输出长度。
 * @return 0=成功；1=越界。
 */
static int32_t glk_helper_next_bytes(const uint8_t **cursor,
                                     const uint8_t *end, char **out,
                                     uint16_t *out_len) {
    if (end - *cursor < 2) return 1;
    const uint16_t length = glk_helper_load_u16(*cursor);
    *cursor += 2;
    if ((size_t) (end - *cursor) < (size_t) length) return 1;
    char *value = (char *) malloc((size_t) length + 1u);
    if (value == NULL) return 1;
    memcpy(value, *cursor, length);
    value[length] = '\0';
    *cursor += length;
    *out = value;
    if (out_len != NULL) *out_len = length;
    return 0;
}

/**
 * @brief 释放 argv 数组与其中的字符串。
 * @param argv argv 结构。
 */
static void glk_helper_free_argv(struct glk_helper_argv *argv) {
    if (argv->items == NULL) return;
    for (uint32_t index = 0; index < argv->count; ++index) {
        free(argv->items[index]);
        argv->items[index] = NULL;
    }
    free(argv->items);
    argv->items = NULL;
    argv->count = 0;
}

/**
 * @brief 处理 execScript：helper 内 posix_spawn `/system/bin/sh <script>`。
 *
 * @param args 参数区（path/argv/env/timeout）。
 * @param args_len 参数长度。
 * @return 0=成功写出回复；否则 C API 错误。
 */
static int32_t glk_helper_exec_script(const uint8_t *args, uint32_t args_len) {
    const uint8_t *cursor = args;
    const uint8_t *end = args + args_len;
    char *script_path = NULL;
    uint16_t script_path_len = 0;
    if (glk_helper_next_bytes(&cursor, end, &script_path, &script_path_len) != 0 ||
        script_path_len == 0 || script_path[0] != '/') {
        free(script_path);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_BAD_ARGS, EINVAL);
    }
    if (end - cursor < 2) {
        free(script_path);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_BAD_ARGS, EINVAL);
    }
    const uint16_t extra_argc = glk_helper_load_u16(cursor);
    cursor += 2;
    if (extra_argc > GLK_VICTIM_MAX_ARG_COUNT) {
        free(script_path);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_BAD_ARGS, E2BIG);
    }
    struct glk_helper_argv extra_argv = {(char **) calloc(
                                             (size_t) extra_argc, sizeof(char *)),
                                         extra_argc};
    int32_t parse_error = extra_argv.items == NULL && extra_argc != 0 ? 1 : 0;
    for (uint32_t index = 0; parse_error == 0 && index < extra_argc; ++index) {
        char *value = NULL;
        uint16_t value_len = 0;
        if (glk_helper_next_bytes(&cursor, end, &value, &value_len) != 0) {
            parse_error = 1;
            break;
        }
        if (value_len == 0) free(value);
        extra_argv.items[index] = value_len == 0 ? NULL : value;
    }
    uint16_t env_count = 0;
    if (parse_error == 0) {
        if (end - cursor < 2) {
            parse_error = 1;
        } else {
            env_count = glk_helper_load_u16(cursor);
            cursor += 2;
            if (env_count > GLK_VICTIM_MAX_ARG_COUNT) parse_error = 1;
        }
    }
    struct glk_helper_argv env_argv = {(char **) calloc(
                                           (size_t) env_count, sizeof(char *)),
                                       env_count};
    if (parse_error == 0 && env_argv.items == NULL && env_count != 0) {
        parse_error = 1;
    }
    for (uint32_t index = 0; parse_error == 0 && index < env_count; ++index) {
        char *value = NULL;
        uint16_t value_len = 0;
        if (glk_helper_next_bytes(&cursor, end, &value, &value_len) != 0) {
            parse_error = 1;
            break;
        }
        env_argv.items[index] = value_len == 0 ? NULL : value;
        if (value_len == 0) free(value);
    }
    uint64_t timeout_ms = 0;
    if (parse_error == 0) {
        if (end - cursor != 8) {
            parse_error = 1;
        } else {
            timeout_ms = glk_helper_load_u64(cursor);
            if (timeout_ms == 0 || timeout_ms > (uint64_t) INT32_MAX) parse_error = 1;
        }
    }
    if (parse_error != 0) {
        free(script_path);
        glk_helper_free_argv(&extra_argv);
        glk_helper_free_argv(&env_argv);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_BAD_ARGS, EINVAL);
    }
    /* 子 argv = /system/bin/sh <script> [extra...]，结尾 NULL。 */
    const uint32_t child_argc = 2u + extra_argc;
    char **child_argv = (char **) calloc((size_t) child_argc + 1u, sizeof(char *));
    if (child_argv == NULL) {
        free(script_path);
        glk_helper_free_argv(&extra_argv);
        glk_helper_free_argv(&env_argv);
        return glk_helper_reply_error(GLK_VICTIM_ERROR_EXEC, ENOMEM);
    }
    child_argv[0] = (char *) "/system/bin/sh";
    child_argv[1] = script_path;
    for (uint32_t index = 0; index < extra_argc; ++index) {
        child_argv[2 + index] = extra_argv.items[index];
    }
    char *const *child_env = env_count == 0 ? environ : env_argv.items;
    int32_t output_pipe[2] = {-1, -1};
    int32_t reply_error = 0;
    uint32_t helper_errno = 0;
    if (pipe2(output_pipe, O_CLOEXEC) != 0) {
        reply_error = GLK_VICTIM_ERROR_EXEC;
        helper_errno = (uint32_t) errno;
    }
    pid_t child = 0;
    if (reply_error == 0) {
        posix_spawn_file_actions_t actions;
        int32_t actions_ready = 0;
        int32_t spawn_status = posix_spawn_file_actions_init(&actions);
        if (spawn_status == 0) {
            actions_ready = 1;
            if (spawn_status == 0) {
                spawn_status = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], 1);
            }
            if (spawn_status == 0) {
                spawn_status = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], 2);
            }
            if (spawn_status == 0) {
                spawn_status = posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
            }
            if (spawn_status == 0) {
                spawn_status = posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
            }
            if (spawn_status == 0) {
                spawn_status = posix_spawn_file_actions_addclose(
                    &actions, GLK_VICTIM_CONTROL_FD);
            }
            if (spawn_status == 0) {
                spawn_status = posix_spawn_file_actions_addclose(&actions, GLK_VICTIM_RAW_FD);
            }
        }
        if (spawn_status == 0) {
            spawn_status = posix_spawn(&child, "/system/bin/sh", &actions, NULL,
                                       child_argv, child_env);
        }
        if (actions_ready != 0) (void) posix_spawn_file_actions_destroy(&actions);
        if (spawn_status != 0) {
            reply_error = GLK_VICTIM_ERROR_EXEC;
            helper_errno = (uint32_t) spawn_status;
        }
    }
    (void) close(output_pipe[1]);
    uint8_t *output = NULL;
    const uint64_t raw_size = glk_helper_raw_size();
    const uint64_t capacity = raw_size > GLK_VICTIM_RAW_HEADER_BYTES
                                  ? raw_size - GLK_VICTIM_RAW_HEADER_BYTES
                                  : 0;
    uint32_t output_capacity = capacity > UINT32_MAX ? UINT32_MAX : (uint32_t) capacity;
    uint32_t output_len = 0;
    int32_t truncation = 0;
    int32_t timed_out = 0;
    int32_t waited_status = 0;
    if (reply_error == 0) {
        output = (uint8_t *) malloc(output_capacity == 0 ? 1u : (size_t) output_capacity);
        if (output == NULL) {
            reply_error = GLK_VICTIM_ERROR_EXEC;
            helper_errno = ENOMEM;
        }
    }
    if (reply_error == 0) {
        const int64_t deadline = glk_helper_now_ms() + (int64_t) timeout_ms;
        int32_t child_reaped = 0;
        for (;;) {
            if (child_reaped == 0 && glk_helper_now_ms() >= deadline) {
                (void) kill(child, SIGKILL);
                timed_out = 1;
            }
            if (child_reaped == 0) {
                struct pollfd waiter;
                (void) memset(&waiter, 0, sizeof(waiter));
                waiter.fd = output_pipe[0];
                waiter.events = POLLIN;
                const int64_t remaining = deadline - glk_helper_now_ms();
                const int32_t poll_timeout =
                    remaining <= 0 ? 0 : (remaining > 100 ? 100 : (int32_t) remaining);
                const int32_t ready = poll(&waiter, 1, poll_timeout);
                if (ready > 0) {
                    for (;;) {
                        uint8_t scratch[GLK_HELPER_IO_CHUNK_BYTES];
                        const ssize_t read_size =
                            read(output_pipe[0], scratch, sizeof(scratch));
                        if (read_size <= 0) break;
                        for (ssize_t index = 0; index < read_size; ++index) {
                            if (output_len < output_capacity) {
                                output[output_len] = scratch[index];
                                output_len++;
                            } else {
                                truncation = 1;
                            }
                        }
                    }
                }
                const pid_t waited = waitpid(child, &waited_status, WNOHANG);
                if (waited == child) {
                    child_reaped = 1;
                } else if (waited < 0 && errno != EINTR) {
                    reply_error = GLK_VICTIM_ERROR_EXEC;
                    helper_errno = (uint32_t) errno;
                    break;
                }
                glk_helper_sleep_ms(1);
            } else {
                /* 子进程已退出：非阻塞排空管道剩余数据。 */
                for (;;) {
                    struct pollfd waiter;
                    (void) memset(&waiter, 0, sizeof(waiter));
                    waiter.fd = output_pipe[0];
                    waiter.events = POLLIN;
                    if (poll(&waiter, 1, 0) <= 0) break;
                    uint8_t tail[GLK_HELPER_IO_CHUNK_BYTES];
                    const ssize_t read_size =
                        read(output_pipe[0], tail, sizeof(tail));
                    if (read_size <= 0) break;
                    for (ssize_t index = 0; index < read_size; ++index) {
                        if (output_len < output_capacity) {
                            output[output_len] = tail[index];
                            output_len++;
                        } else {
                            truncation = 1;
                        }
                    }
                }
                break;
            }
        }
    }
    if (output_pipe[0] >= 0) (void) close(output_pipe[0]);
    if (reply_error == 0) {
        const int32_t write_error =
            glk_helper_write_raw(0, output, output_len, truncation);
        if (write_error != 0) {
            reply_error = GLK_VICTIM_ERROR_IO;
            helper_errno = (uint32_t) write_error;
        }
    }
    free(output);
    if (reply_error != 0) {
        if (child > 0) {
            (void) kill(child, SIGKILL);
            (void) waitpid(child, NULL, 0);
        }
        free(script_path);
        glk_helper_free_argv(&extra_argv);
        glk_helper_free_argv(&env_argv);
        free(child_argv);
        return glk_helper_reply_error(reply_error, (int32_t) helper_errno);
    }
    uint8_t data[24];
    const int32_t exit_code = glk_helper_exit_code(waited_status);
    glk_helper_store_u32(data + 0, (uint32_t) exit_code);
    glk_helper_store_u64(data + 4, (uint64_t) output_len);
    glk_helper_store_u32(data + 12, (uint32_t) (truncation != 0 ? 1 : 0));
    glk_helper_store_u32(data + 16, (uint32_t) (timed_out != 0 ? 1 : 0));
    glk_helper_store_u32(data + 20, 0);
    free(script_path);
    glk_helper_free_argv(&extra_argv);
    glk_helper_free_argv(&env_argv);
    free(child_argv);
    return glk_helper_reply_ok(data, sizeof(data));
}

/**
 * @brief 处理 park：回复后 futex 等待（永不返回；rooted child 驻留）。
 * @return 仅在回复失败时返回 C API 错误。
 */
static int32_t glk_helper_park(void) {
    const int32_t reply = glk_helper_reply_ok(NULL, 0);
    if (reply != 0) return reply;
    int32_t word = 0;
    for (;;) {
        (void) syscall(SYS_futex, &word, FUTEX_WAIT, 0, NULL, NULL, 0);
    }
}

/**
 * @brief 处理 bye：回复后正常退出。
 * @return 仅在回复失败时返回 C API 错误（否则不返回）。
 */
static int32_t glk_helper_bye(void) {
    /* E4 零 fork ①：把本 helper 链接单元的 fork/vfork 计数随 bye 回执上报
     * （生产必须双 0；posix_spawn 内部 clone/vfork 不计入 fork/vfork 符号）。 */
    uint8_t counts[8];
    glk_helper_store_u32(counts, glk_victim_fork_count());
    glk_helper_store_u32(counts + 4, glk_victim_vfork_count());
    const int32_t reply = glk_helper_reply_ok(counts, sizeof(counts));
    if (reply != 0) return reply;
    _exit(0);
}

/**
 * @brief 解析并校验一条命令帧（version/kind/payloadLen/crc）。
 *
 * @param frame 完整线帧。
 * @param frame_len 线帧长度。
 * @param out_opcode 输出 opcode。
 * @param out_args 输出参数区指针（指向 frame 内）。
 * @param out_args_len 输出参数长度。
 * @return 0=合法；否则 C API 错误。
 */
static int32_t glk_helper_parse_command(const uint8_t *frame, uint32_t frame_len,
                                        uint8_t *out_opcode,
                                        const uint8_t **out_args,
                                        uint32_t *out_args_len) {
    if (frame == NULL || frame_len < GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES +
                                       GLK_VICTIM_BODY_OVERHEAD_BYTES + 1u) {
        return GLK_VICTIM_ERR_BAD_FRAME;
    }
    const uint32_t body_len = glk_helper_load_u32(frame);
    if (body_len != frame_len - GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES ||
        body_len > GLK_VICTIM_FRAME_MAX_BODY) {
        return GLK_VICTIM_ERR_BAD_FRAME;
    }
    const uint8_t *body = frame + GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES;
    if (body[0] != (uint8_t) GLK_VICTIM_PROTOCOL_VERSION ||
        body[1] != (uint8_t) GLK_VICTIM_FRAME_KIND_COMMAND) {
        return GLK_VICTIM_ERR_BAD_FRAME;
    }
    const uint16_t payload_len = glk_helper_load_u16(body + 2);
    if ((uint32_t) payload_len + GLK_VICTIM_BODY_OVERHEAD_BYTES != body_len ||
        payload_len == 0) {
        return GLK_VICTIM_ERR_BAD_FRAME;
    }
    const uint32_t crc = glk_victim_crc32(body, GLK_VICTIM_BODY_HEADER_BYTES + payload_len);
    if (crc != glk_helper_load_u32(body + GLK_VICTIM_BODY_HEADER_BYTES + payload_len)) {
        return GLK_VICTIM_ERR_BAD_FRAME;
    }
    *out_opcode = body[GLK_VICTIM_BODY_HEADER_BYTES];
    *out_args = body + GLK_VICTIM_BODY_HEADER_BYTES + 1u;
    *out_args_len = (uint32_t) payload_len - 1u;
    return GLK_VICTIM_OK;
}

/**
 * @brief helper 主循环：读帧 → 校验 → 机制分派 → 回复。
 * @return 进程退出码（0=收到 bye）。
 */
static int32_t glk_helper_run(void) {
    struct glk_helper_buffer command;
    for (;;) {
        command.length = 0;
        const int32_t read_status = glk_victim_read_frame_fd(
            GLK_VICTIM_CONTROL_FD, command.bytes, (uint32_t) sizeof(command.bytes),
            &command.length);
        if (read_status == GLK_VICTIM_ERR_EOF) return 0;
        if (read_status != GLK_VICTIM_OK) {
            (void) glk_helper_reply_error(GLK_VICTIM_ERROR_INVALID_FRAME, 0);
            return 2;
        }
        uint8_t opcode = 0;
        const uint8_t *args = NULL;
        uint32_t args_len = 0;
        if (glk_helper_parse_command(command.bytes, command.length, &opcode,
                                     &args, &args_len) != GLK_VICTIM_OK) {
            (void) glk_helper_reply_error(GLK_VICTIM_ERROR_INVALID_FRAME, 0);
            continue;
        }
        int32_t dispatch = 0;
        switch (opcode) {
            case GLK_VICTIM_OPCODE_PROBE_UID:
                dispatch = glk_helper_probe_uid();
                break;
            case GLK_VICTIM_OPCODE_PERF_SELF_SAMPLE:
                dispatch = glk_helper_perf_self_sample(args, args_len);
                break;
            case GLK_VICTIM_OPCODE_EXEC_SCRIPT:
                dispatch = glk_helper_exec_script(args, args_len);
                break;
            case GLK_VICTIM_OPCODE_PARK:
                dispatch = glk_helper_park();
                break;
            case GLK_VICTIM_OPCODE_BYE:
                dispatch = glk_helper_bye();
                break;
            case GLK_VICTIM_OPCODE_PROBE_STATUS:
                dispatch = glk_helper_probe_status();
                break;
            case GLK_VICTIM_OPCODE_PROBE_COMM_SET:
                dispatch = glk_helper_probe_comm_set(args, args_len);
                break;
            case GLK_VICTIM_OPCODE_PROBE_COMM:
                dispatch = glk_helper_probe_comm();
                break;
            default:
                dispatch = glk_helper_reply_error(GLK_VICTIM_ERROR_UNSUPPORTED, 0);
                break;
        }
        if (dispatch != GLK_VICTIM_OK) return 3;
    }
}

/**
 * @brief helper 入口：fd 契约自检 → 命令循环。
 * @return 进程退出码。
 */
int main(void) {
    (void) signal(SIGPIPE, SIG_IGN);
    if (glk_helper_assert_contract_fds() != 0) return 1;
    if (glk_helper_converge_fds() != 0) return 1;
    return glk_helper_run();
}
