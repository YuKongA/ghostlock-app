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

/** @brief 映射大小的最小页数（zc 地址所在页 + 至少一页余量；结构性下限）。 */
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

/**
 * @brief 校验 zc 请求体四个字段偏移（T2/T3/T5）。
 * @param spec 已通过 ABI 头校验的 TCP 规格。
 * @return 1 接受；0 拒绝（越界/未对齐/重叠）。
 * @note 白名单具体值（旧三版 0x18/0x20/0x28/0x30）属于 Kotlin generator 的职责；
 *       Native 只保证任何被请求的偏移在 frame 内、按字段宽度对齐且互不重叠，
 *       从而不可能因偏移回绕/越界而写坏原生堆（T5）。
 */
static int32_t glk43499_tcp_valid_zc_offsets(
    const struct glk43499_tcp_zerocopy_spec *spec) {
    const uint64_t sizes[4] = {
        (uint64_t) sizeof(uint64_t),
        (uint64_t) sizeof(uint32_t),
        (uint64_t) sizeof(uint64_t),
        (uint64_t) sizeof(uint64_t),
    };
    const uint64_t offsets[4] = {
        spec->zc_address_offset,
        spec->zc_length_offset,
        spec->zc_task_offset,
        spec->zc_lock_offset,
    };
    uint64_t ranges[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    for (size_t index = 0; index < 4; ++index) {
        if (offsets[index] % sizes[index] != 0) return 0;
        uint64_t end = 0;
        if (!glk43499_internal_add_u64(offsets[index], sizes[index], &end) ||
            end > spec->frame_size) {
            return 0;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            if (offsets[index] < ranges[previous][1] &&
                ranges[previous][0] < end) {
                return 0;
            }
        }
        ranges[index][0] = offsets[index];
        ranges[index][1] = end;
    }
    return 1;
}

/**
 * @brief 按请求偏移把 zc 字段值写入工作缓冲（PI 窗口内；无分配/无日志）。
 * @param target 非空 zc 工作缓冲。
 * @param frame_size 工作缓冲长度（prepare 已校验所有偏移界内）。
 * @param offset 字段偏移。
 * @param value 字段值指针。
 * @param size 字段字节数。
 * @return 1 写入；0 越界（调用方按物理失败收口）。
 */
static int32_t glk43499_tcp_store_field(uint8_t *target, uint64_t frame_size,
                                        uint64_t offset, const void *value,
                                        uint64_t size) {
    uint64_t end = 0;
    if (!glk43499_internal_add_u64(offset, size, &end) || end > frame_size) {
        return 0;
    }
    memcpy(target + offset, value, (size_t) size);
    return 1;
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
    /* B15：attempt 计数最终收窄到 int32（consumer_go/回执），域上限显式。 */
    if (spec->operation_budget == 0 ||
        spec->operation_budget > (uint64_t) INT32_MAX) {
        return -EINVAL;
    }
    /* 超时非零且 deadline 加法不溢出（不内置 1 小时目标上限）。 */
    const uint64_t now_ns = glk43499_internal_now_ns();
    if (spec->timeout_ns == 0 ||
        spec->timeout_ns > UINT64_MAX - now_ns) {
        return -EINVAL;
    }
    const long page_size_long = sysconf(_SC_PAGESIZE);
    if (page_size_long <= 0) return -EINVAL;
    const size_t page_size = (size_t) page_size_long;
    /* A 决策（R2b）：映射/帧容量上限来自请求字段 max_mapping/max_frame。 */
    const uint64_t min_mapping =
        (uint64_t) page_size * GLK43499_TCP_MIN_MAPPING_PAGES;
    if (spec->max_mapping == 0 ||
        spec->max_mapping % (uint64_t) page_size != 0 ||
        spec->max_mapping < min_mapping ||
        spec->mapping_size < min_mapping ||
        spec->mapping_size > spec->max_mapping ||
        spec->mapping_size % (uint64_t) page_size != 0) {
        return -EINVAL;
    }
    /* R3a-2/T6：punch 洞从 page_size 起，zerocopy 目标页必须是紧邻第一页；
     * mapping_offset 必须严格等于页大小（旧实现用 mapping+page_size）。 */
    if (spec->mapping_offset != (uint64_t) page_size) return -EINVAL;
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
    if (spec->max_frame == 0 || spec->receive_size == 0 ||
        spec->receive_size > spec->max_frame) {
        return -EINVAL;
    }
    if (spec->frame_size == 0 ||
        spec->frame_size > spec->max_frame ||
        spec->frame_size < 2 * (uint64_t) sizeof(uint64_t)) {
        return -EINVAL;
    }
    /* R3a-2 重要项：旧实现要求 zc 请求体与发送小包都落在已映射页内
     * （`route_tcp.cpp:286-295`）；长度与帧容量都不得越过一页。 */
    if (spec->frame_size > (uint64_t) page_size ||
        spec->receive_size > (uint64_t) page_size) {
        return -EINVAL;
    }
    /* R3a-2/T1/T4/T5：请求参数 fail-closed 校验（不填默认值）。
     * - arm_sequence/punch_phase_spin 必须非零（旧 arm=16、punch 相位等待独立上限）；
     * - zc 四字段偏移：对齐/在 frame 内/互不重叠（白名单值由 Kotlin generator 负责）；
     * - R3b：两个填充字节各自来自 tcp spec（不允许复用 landing.fill_byte）。 */
    if (spec->punch_spin == 0 || spec->post_hold_ns == 0 ||
        spec->arm_sequence == 0 || spec->punch_phase_spin == 0) {
        return -EINVAL;
    }
    if (spec->send_fill_byte > UINT8_MAX ||
        spec->mapping_fill_byte > UINT8_MAX) {
        return -EINVAL;
    }
    if (!glk43499_tcp_valid_zc_offsets(spec)) return -EINVAL;

    tcp->page_size = page_size;
    tcp->mapping_length = (size_t) spec->mapping_size;
    tcp->receive_size = spec->receive_size;
    tcp->mapping_offset = spec->mapping_offset;
    tcp->frame_size = spec->frame_size;
    tcp->operation_budget = spec->operation_budget;
    tcp->timeout_ns = spec->timeout_ns;
    tcp->punch_spin = spec->punch_spin;
    tcp->post_hold_ns = spec->post_hold_ns;
    tcp->zc_address_offset = spec->zc_address_offset;
    /* R3a-2/T2–T4：四字段偏移、arm 序列、punch 相位独立上限与最终地址。 */
    tcp->arm_sequence = spec->arm_sequence;
    tcp->zc_length_offset = spec->zc_length_offset;
    tcp->zc_task_offset = spec->zc_task_offset;
    tcp->zc_lock_offset = spec->zc_lock_offset;
    tcp->punch_phase_spin = spec->punch_phase_spin;
    tcp->final_task_address = spec->addresses.final_task_address;
    tcp->final_lock_address = spec->addresses.final_lock_address;
    /* R3b：两个填充字节在 prepare 使用前已由 tcp spec 提供（旧实现由 Actor 在
     * prepare 之后才赋值 ⇒ 映射页与发送缓冲全 0，审计 T1）。 */
    tcp->send_fill_byte = spec->send_fill_byte;
    tcp->mapping_fill_byte = spec->mapping_fill_byte;

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
    memset(tcp->send_buffer, (int) tcp->send_fill_byte,
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
            bytes[offset] = (uint8_t) tcp->mapping_fill_byte;
        }
    }

    /* B11：pthread_create 返回错误码；bionic 不保证同步写 errno，禁止读 errno。 */
    const int32_t create_status = pthread_create(
        &tcp->punch_worker, NULL, glk43499_carrier_tcp_punch_run, tcp);
    if (create_status != 0) {
        (void) glk43499_carrier_tcp_discard(tcp);
        return -create_status;
    }
    tcp->punch_started = 1;
    tcp->prepared = 1;
    return 0;
}

/**
 * @brief PI 窗口内执行 TCP 触发：发送小包 + getsockopt(TCP_ZEROCOPY_RECEIVE)。
 * @param carrier 已 prepare 的 Carrier；只访问 tcp 分支。
 * @param shared PI 共享状态。
 * @note R3a-2/T1–T5：与 legacy2 `route_tcp.cpp:300-352` 对齐 ——
 *       仅 `attempt >= arm_sequence` 且 getsockopt 成功后才 arm consumer；
 *       arm 后先清 consumer_go 再等 inflight 归零（旧 `tcp_wait_for_consumer_idle` 顺序）；
 *       zc 请求体四个字段（地址/长度/task/lock）全部由 Native 按请求偏移写入，
 *       任何加法/越界都按物理失败收口（窗口内不分配、不日志、不首次触页）。
 *       中性结果写 last_step/last_errno，供 execute 回执做失败定位。
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
    uint8_t *const request = tcp->zc_request;
    const uint64_t length_field = tcp->receive_size;
    const uint64_t task_field = tcp->final_task_address;
    const uint64_t lock_field = tcp->final_lock_address;

    uint64_t operations = 0;
    int32_t last_errno = 0;
    glk43499_carrier_step last_step = GLK43499_CARRIER_STEP_NONE;
    for (uint64_t attempt = 1; attempt <= tcp->operation_budget && won == 0;
         ++attempt) {
        if (glk43499_internal_expired(deadline)) break;
        ++operations;
        const int32_t calls_before = atomic_load(&shared->consumer_calls);
        const int32_t success_before = atomic_load(&shared->consumer_success);
        last_step = GLK43499_CARRIER_STEP_SEND;
        errno = 0;
        /* G09：MSG_NOSIGNAL 抑制对端已关时 SIGPIPE（legacy backend:431）。 */
        (void) send(tcp->server_fd, tcp->send_buffer,
                    (size_t) tcp->receive_size, MSG_DONTWAIT | MSG_NOSIGNAL);
        /* punch 相位等待：phase=1 等待用独立上限（T 重要项），phase=0 后按
         * 请求的 punch_spin 有界自旋（旧实现相位 1 无界）。 */
        last_step = GLK43499_CARRIER_STEP_PUNCH;
        for (uint64_t spin = 0;
             spin < tcp->punch_phase_spin &&
             atomic_load(&tcp->punch_phase) != 0;
             ++spin) {
            (void) sched_yield();
        }
        for (uint64_t spin = 0;
             spin < tcp->punch_spin &&
             atomic_load(&tcp->punch_phase) == 0 &&
             atomic_load(&tcp->punch_failed) == 0;
             ++spin) {
            (void) sched_yield();
        }
        if (atomic_load(&tcp->punch_failed) != 0) {
            last_errno = atomic_load(&tcp->punch_failed);
            break;
        }

        /* zc 请求体：模板深拷贝 + Native 拥有的四个字段（T2/T3 偏移、T5 界内）。 */
        last_step = GLK43499_CARRIER_STEP_REQUEST;
        memcpy(request, tcp->frame, (size_t) tcp->frame_size);
        uintptr_t mapping_address = 0;
        if (!glk43499_internal_add_uintptr((uintptr_t) tcp->mapping,
                                           tcp->mapping_offset,
                                           &mapping_address)) {
            last_errno = EOVERFLOW;
            break;
        }
        const uint64_t address_field = (uint64_t) mapping_address;
        if (!glk43499_tcp_store_field(request, tcp->frame_size,
                                      tcp->zc_address_offset, &address_field,
                                      (uint64_t) sizeof(address_field)) ||
            !glk43499_tcp_store_field(request, tcp->frame_size,
                                      tcp->zc_length_offset, &length_field,
                                      (uint64_t) sizeof(uint32_t)) ||
            !glk43499_tcp_store_field(request, tcp->frame_size,
                                      tcp->zc_task_offset, &task_field,
                                      (uint64_t) sizeof(task_field)) ||
            !glk43499_tcp_store_field(request, tcp->frame_size,
                                      tcp->zc_lock_offset, &lock_field,
                                      (uint64_t) sizeof(lock_field))) {
            last_errno = EOVERFLOW;
            break;
        }
        last_step = GLK43499_CARRIER_STEP_IO;
        socklen_t request_length = (socklen_t) tcp->frame_size;
        errno = 0;
        const int32_t receive_result =
            getsockopt(tcp->client_fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE,
                       request, &request_length);
        if (receive_result != 0) {
            last_errno = errno != 0 ? errno : EIO;
        } else if (attempt >= tcp->arm_sequence) {
            /* T4：arm 序列语义（旧 `i >= arm_seq && ret == 0`）。 */
            last_step = GLK43499_CARRIER_STEP_ARM;
            atomic_store(&shared->consumer_go, (int32_t) attempt);
            for (uint64_t spin = 0; spin < tcp->post_hold_ns; ++spin) {
                __asm__ volatile("yield" ::: "memory");
            }
            atomic_store(&shared->consumer_go, 0);
            last_step = GLK43499_CARRIER_STEP_INFLIGHT;
            for (uint64_t spin = 0;
                 spin < tcp->disarm_spin_limit &&
                 atomic_load(&shared->consumer_inflight) != 0;
                 ++spin) {
                (void) sched_yield();
            }
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
                     : (last_errno != 0
                            ? last_errno
                            : (int32_t) atomic_load(&tcp->punch_failed));
        outcome->observed_write = won;
        outcome->last_step = last_step;
        outcome->last_errno = last_errno;
    }
}

int32_t glk43499_carrier_tcp_disarm(struct glk43499_carrier *carrier,
                                    struct glk43499_pi_shared *shared) {
    struct glk43499_carrier_tcp *tcp = &carrier->value.tcp_zerocopy_receive;
    atomic_store(&shared->consumer_go, 0);
    atomic_store(&tcp->punch_go, 0);
    atomic_store(&tcp->punch_stop, 1);
    for (uint64_t spin = 0; spin < tcp->disarm_spin_limit; ++spin) {
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
        /* 等待预算来自请求 timeout_ns；不内置 1 秒目标常量。 */
        const uint64_t deadline = glk43499_internal_deadline(tcp->timeout_ns);
        while (atomic_load(&tcp->punch_exited) == 0) {
            if (glk43499_internal_expired(deadline)) {
                /* punch worker 未知引用：整链保留，不 munmap 不 close。 */
                return 0;
            }
            (void) usleep((useconds_t) tcp->poll_us);
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
    tcp->punch_spin = 0;
    tcp->post_hold_ns = 0;
    tcp->zc_address_offset = 0;
    tcp->arm_sequence = 0;
    tcp->zc_length_offset = 0;
    tcp->zc_task_offset = 0;
    tcp->zc_lock_offset = 0;
    tcp->punch_phase_spin = 0;
    tcp->final_task_address = 0;
    tcp->final_lock_address = 0;
    tcp->disarm_spin_limit = 0;
    tcp->poll_us = 0;
    tcp->send_fill_byte = 0;
    tcp->mapping_fill_byte = 0;
    tcp->prepared = 0;
    return 1;
}
