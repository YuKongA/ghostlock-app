/**
 * @file carrier_internal.h
 * @brief 中性 Carrier 私有接口：PI 窗口共享状态、三个 Primitive 的物理状态与绑定入口。
 *
 * @note 本头只服务 `actor.c` 与 `carrier_*.c`；不是公共 ABI。
 * @note 绑定纪律：具体 Carrier 在 `prepareActor` 阶段（线程创建前）完成选择与资源
 *       准备，并把触发入口绑定到 Actor 对象的函数指针；PI 窗口内不再读取 kind、
 *       不做 switch/动态派发，只调用 prepare 时绑定的那个入口。
 * @note 窗口内禁止：JNI、锁、分配、日志、首次触页；dispatch 与预触页都在 prepare。
 * @note 阶段：G4L-3（Actor 与线性接管）/G4L-4M/4P/4T（三个 Primitive）。
 */
#ifndef GHOSTLOCK_GL43499_CORE_CARRIER_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_CARRIER_INTERNAL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>

#include "core_internal.h"
#include "kernel_internal.h"
#include "primitive.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PI 竞态共享状态：三个 worker 与 Carrier 共用的 futex 字与原子计数。
 * @note 全部内嵌在 Actor 堆对象里，不是全局；Actor 终结时随之释放。
 * @note 内存序：控制标志用 release/acquire 配对；热路径计数用默认 seq_cst，
 *       与旧实现保持一致，不在此处放松（放松需专门真机门）。
 */
struct glk43499_pi_shared {
    uint32_t wait_futex;
    uint32_t target_futex;
    uint32_t chain_futex;

    atomic_int waiter_ready;
    atomic_int waiter_waiting;
    atomic_int waiter_link_failed;
    atomic_int owner_ready;
    atomic_int owner_started;
    atomic_int owner_chain_done;
    atomic_int owner_stop;
    atomic_int route_done;
    atomic_int waiter_tid;
    atomic_int consumer_ready;
    atomic_int consumer_go;
    atomic_int consumer_stop;
    atomic_int consumer_calls;
    atomic_int consumer_success;
    atomic_int consumer_inflight;
    atomic_uint route_delay_usec;
    /** @brief execute 已请求打开窗口；由 executeActor 在 CMP_REQUEUE_PI 前发布。 */
    atomic_int execute_requested;
    /** @brief waiter 确认已进入 PI 窗口。 */
    atomic_int window_opened;
    /** @brief 窗口结束（无论 Carrier 是否成功）；execute 的唯一完成信号。 */
    atomic_int window_done;
    /** @brief 三个 worker 的「已退出」发布点；有界 join 先等它再 pthread_join。 */
    atomic_int waiter_exited;
    atomic_int owner_exited;
    atomic_int consumer_exited;
};

/** @brief Multicast Carrier 的物理状态（MCAST_BLOCK_SOURCE 单发）。 */
struct glk43499_carrier_multicast {
    int32_t socket_fd;
    uint8_t *frame;
    uint64_t frame_size;
    uint32_t address_family;
    uint64_t operation_budget;
    uint64_t timeout_ns;
    int32_t prepared;
};

/** @brief Pselect Carrier 的物理状态（fd_set 窗口 + 低号 fd 安装）。 */
struct glk43499_carrier_pselect {
    int32_t pipe_read;
    int32_t pipe_write;
    int32_t block;
    int32_t high_read;
    int32_t block_borrows_pipe;
    int32_t selected_fds_installed;
    int32_t consumer_stuck;
    int32_t stdio_backup[3];
    fd_set input_set;
    fd_set output_set;
    fd_set exception_set;
    fd_set owned_input_set;
    fd_set owned_output_set;
    fd_set owned_exception_set;
    uint8_t *frame;
    uint64_t frame_size;
    uint32_t nfds;
    uint64_t operation_budget;
    uint64_t timeout_ns;
    int32_t prepared;
};

/** @brief TCP zerocopy Carrier 的物理状态（loopback pair + 打洞 memfd + 映射）。 */
struct glk43499_carrier_tcp {
    int32_t client_fd;
    int32_t server_fd;
    int32_t punch_fd;
    void *mapping;
    size_t mapping_length;
    size_t page_size;
    uint8_t *frame;
    uint8_t *zc_request;
    uint64_t frame_size;
    uint64_t receive_size;
    uint64_t mapping_offset;
    uint64_t operation_budget;
    uint64_t timeout_ns;
    uint8_t *send_buffer;
    pthread_t punch_worker;
    int32_t punch_started;
    atomic_int punch_go;
    atomic_int punch_stop;
    atomic_int punch_phase;
    atomic_int punch_failed;
    atomic_int punch_exited;
    int32_t prepared;
};

/** @brief 一个已绑定的 Carrier：kind 只在 prepare/终结阶段读取。 */
struct glk43499_carrier {
    glk43499_primitive_kind kind;
    union {
        struct glk43499_carrier_multicast multicast_sendmsg;
        struct glk43499_carrier_pselect pselect;
        struct glk43499_carrier_tcp tcp_zerocopy_receive;
    } value;
};

/**
 * @brief Carrier 触发的**中性物理结果**：只记录发生过的操作与观察到的计数，
 *        不表达业务目标是否达成。
 *
 * @note `observed_write=1` 只表示「本次 Carrier 观察到至少一次有效的物理写」；
 *       Actor 的成功判据必须同时要求 calls>0 且 successes>0，禁止事务完成即成功。
 * @note 结构由 waiter 线程在 PI 窗口结束时填写，经 `window_done` 的 release/acquire
 *       对发布给 executeActor 读取；不加锁。
 */
struct glk43499_carrier_outcome {
    uint64_t operations;
    uint64_t calls;
    uint64_t successes;
    int32_t failed;
    int32_t observed_write;
};

/* ------------------------------------------------------------------------- */
/* G4L-4M：Multicast                                                          */
/* ------------------------------------------------------------------------- */

/**
 * @brief 准备 Multicast Carrier：校验 frame 几何并建立 UDP socket。
 * @param carrier 目标 Carrier；`kind` 已由 Actor 设置。
 * @param spec 调用期借用的 Multicast 物理参数（frame 会被深拷贝）。
 * @return 0 成功；负 errno 语义失败（调用方回滚，Page 不消费）。
 */
int32_t glk43499_carrier_multicast_prepare(
    struct glk43499_carrier *carrier,
    const struct glk43499_multicast_spec *spec);

/**
 * @brief PI 窗口：重复 stamp + setsockopt 并驱动 consumer 握手。
 * @param carrier 已 prepare 的 Carrier；只访问 multicast 分支。
 * @param shared PI 共享状态。
 * @param outcome 非空中性物理结果输出；窗口内不分配、不写日志。
 */
void glk43499_carrier_multicast_trigger(
    struct glk43499_carrier *carrier, struct glk43499_pi_shared *shared,
    struct glk43499_carrier_outcome *outcome);

/** @brief 停止新触发并排空 consumer；返回 1 表示 consumer 已排空。 */
int32_t glk43499_carrier_multicast_disarm(struct glk43499_carrier *carrier,
                                          struct glk43499_pi_shared *shared);

/** @brief 释放 Multicast Carrier 资源；返回 1 表示干净释放。 */
int32_t glk43499_carrier_multicast_release(
    struct glk43499_carrier_multicast *multicast);

/* ------------------------------------------------------------------------- */
/* G4L-4P：Pselect                                                            */
/* ------------------------------------------------------------------------- */

/**
 * @brief 准备 Pselect Carrier：pipe/timerfd/stdio 备份/fd_set 安装。
 * @param carrier 目标 Carrier；`kind` 已由 Actor 设置。
 * @param spec 调用期借用的 Pselect 物理参数（frame 会被深拷贝）。
 * @return 0 成功；负 errno 语义失败。
 */
int32_t glk43499_carrier_pselect_prepare(
    struct glk43499_carrier *carrier,
    const struct glk43499_pselect_spec *spec);

/**
 * @brief PI 窗口：park 在 pselect 的 fd_set 窗口上并驱动 consumer 握手。
 * @param carrier 已 prepare 的 Carrier；只访问 pselect 分支。
 * @param shared PI 共享状态。
 * @param outcome 非空中性物理结果输出。
 */
void glk43499_carrier_pselect_trigger(
    struct glk43499_carrier *carrier, struct glk43499_pi_shared *shared,
    struct glk43499_carrier_outcome *outcome);

/** @brief 停止新触发并排空 consumer；consumer 卡死时保留 fd 并返回 0。 */
int32_t glk43499_carrier_pselect_disarm(struct glk43499_carrier *carrier,
                                        struct glk43499_pi_shared *shared);

/** @brief 恢复 stdio、关闭全部 owned fd 与管道；返回 1 表示干净释放。 */
int32_t glk43499_carrier_pselect_release(
    struct glk43499_carrier_pselect *pselect);

/* ------------------------------------------------------------------------- */
/* G4L-4T：TCP zerocopy                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief 准备 TCP Carrier：loopback pair、打洞 memfd、映射、punch 线程。
 * @param carrier 目标 Carrier；`kind` 已由 Actor 设置。
 * @param spec 调用期借用的 TCP zerocopy 物理参数（frame 会被深拷贝）。
 * @return 0 成功；负 errno 语义失败。
 */
int32_t glk43499_carrier_tcp_prepare(
    struct glk43499_carrier *carrier,
    const struct glk43499_tcp_zerocopy_spec *spec);

/**
 * @brief PI 窗口：发送小包 + getsockopt(TCP_ZEROCOPY_RECEIVE) 并驱动 consumer。
 * @param carrier 已 prepare 的 Carrier；只访问 tcp 分支。
 * @param shared PI 共享状态。
 * @param outcome 非空中性物理结果输出。
 */
void glk43499_carrier_tcp_trigger(struct glk43499_carrier *carrier,
                                  struct glk43499_pi_shared *shared,
                                  struct glk43499_carrier_outcome *outcome);

/** @brief 停止新触发并排空 consumer；返回 1 表示 consumer 已排空。 */
int32_t glk43499_carrier_tcp_disarm(struct glk43499_carrier *carrier,
                                    struct glk43499_pi_shared *shared);

/** @brief join punch worker、munmap、关闭全部 fd；返回 1 表示干净释放。 */
int32_t glk43499_carrier_tcp_release(
    struct glk43499_carrier_tcp *tcp_zerocopy_receive);

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_CARRIER_INTERNAL_H */
