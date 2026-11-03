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
    /** @brief R3a-2/B7：multicast ghost disarm 的 FUTEX_LOCK_PI ret/errno（可证伪）。 */
    atomic_int ghost_disarm_result;
    atomic_int ghost_disarm_errno;
    /** @brief task-108 追加：waiter futex 证据（进入/返回/ret/errno/dispatch）+ 时间戳。 */
    atomic_int waiter_in_futex;
    atomic_int waiter_wait_ret;
    atomic_int waiter_wait_errno;
    atomic_int waiter_dispatched;
    atomic_int target_value_at_requeue;
    atomic_uint_fast64_t waiter_futex_enter_ns;
    atomic_uint_fast64_t waiter_futex_exit_ns;
    atomic_uint_fast64_t settle_done_ns;
    atomic_uint_fast64_t requeue_call_ns;
    atomic_int owner_tid;
    /* task-113：carrier 崩溃前快照（只写 atomic；无 IO/日志）。 */
    atomic_int carrier_started;
    atomic_int carrier_frames_sent;
    atomic_int carrier_step;
    atomic_int carrier_last_errno;
    atomic_uint_fast64_t route_enter_ns;
    atomic_uint_fast64_t route_exit_ns;
    atomic_uint_fast64_t waiter_parked_timeout_ns;
    atomic_int waiter_op;
    atomic_int waiter_val;
    atomic_int waiter_val3;
    atomic_int waiter_uaddr2_tid;

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
    /* P0a：来自请求的 arm 时序/序列与 disarm 上限（外部不可见）。 */
    uint64_t arm_hold_ns;
    uint64_t arm_sequence;
    uint64_t disarm_spin_limit;
    /* A 决策（R2b）：ghost disarm 参数来自 multicast_spec 请求字段。 */
    int32_t ghost_pid;
    uint32_t futex_waiter_flag;
    uint32_t disarm_required;
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
    /* P0a：来自请求的尝试延时与 disarm 上限。 */
    uint64_t per_attempt_delay_ns;
    uint64_t disarm_spin_limit;
    /* R3a-2/P1/P7/P10：consumer 起跑延迟、select 分支、fd 备份起点步长。 */
    uint64_t enter_delay_ns;
    uint64_t fd_reserve_stride;
    uint64_t select_use_timeval;
    /* R3a-2/P8：最后一次 select 的 ret/errno（进入回执 last_step/last_errno）。 */
    int32_t select_result;
    int32_t select_errno;
    /* R3a-2/P11：prepare 清理不干净时上报 retained（禁止假 released）。 */
    int32_t retained;
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
    /* P0a：来自请求的 punch/post hold、地址偏移、disarm 与轮询参数。 */
    uint64_t punch_spin;
    uint64_t post_hold_ns;
    uint64_t zc_address_offset;
    uint64_t disarm_spin_limit;
    uint64_t poll_us;
    /* R3b：发送小包与映射页的填充字节各有唯一请求来源（tcp_spec 双字段）。 */
    uint64_t send_fill_byte;
    uint64_t mapping_fill_byte;
    /* R3a-2/T2–T4：zc 请求体四字段偏移、arm 序列与 punch 相位独立上限。 */
    uint64_t arm_sequence;
    uint64_t zc_length_offset;
    uint64_t zc_task_offset;
    uint64_t zc_lock_offset;
    uint64_t punch_phase_spin;
    uint64_t final_task_address;
    uint64_t final_lock_address;
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
/** @brief Carrier 回执里的 `last_step` 语义（可证伪诊断；不是业务结果）。 */
typedef int32_t glk43499_carrier_step;
enum {
    GLK43499_CARRIER_STEP_NONE = 0,
    GLK43499_CARRIER_STEP_SEND = 1,
    GLK43499_CARRIER_STEP_PUNCH = 2,
    GLK43499_CARRIER_STEP_ARM = 3,
    GLK43499_CARRIER_STEP_REQUEST = 4,
    GLK43499_CARRIER_STEP_IO = 5,
    GLK43499_CARRIER_STEP_INFLIGHT = 6
};

struct glk43499_carrier_outcome {
    uint64_t operations;
    uint64_t calls;
    uint64_t successes;
    int32_t failed;
    int32_t observed_write;
    /* R3a-2：失败定位（最后执行的物理步骤与 errno 语义错误码）。 */
    glk43499_carrier_step last_step;
    int32_t last_errno;
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
 * @param spec 调用期借用的 TCP zerocopy 物理参数（frame 会被深拷贝；
 *   `send_fill_byte`/`mapping_fill_byte` 是发送缓冲与映射页的唯一填充来源）。
 * @return 0 成功；负 errno 语义失败。
 * @note R3b：删除 prepare 的额外 fill 参数；两个填充字节改由 tcp spec 携带
 *   （legacy2 `route_tcp_session.cpp:61-64` send=0x33 / mapping=0x55，A 决策）。
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
