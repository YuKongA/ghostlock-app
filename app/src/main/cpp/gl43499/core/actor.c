/**
 * @file actor.c
 * @brief 43499 Actor 的物理实现：Page→Actor 线性接管、三 worker PI 竞态与固定终结顺序。
 *
 * @note 移植自 2.0.8 `race.cpp`（PiRace 三线程、CMP_REQUEUE_PI、route_done 收尾、
 *       stop→join 顺序）与旧 `support.cpp` 的 partial-start 清理，改写为 C11 +
 *       pthread/裸 syscall；不 include 也不链接 `legacy` 各子目录。旧
 *       session/backend/route controller/Flow 层不引入。
 * @note 冻结顺序（不可改）：
 *       stop new trigger → drain consumer inflight → disarm Carrier →
 *       detach PI/同步 → join waiter→owner→consumer →
 *       close Carrier → release owned Page → release Actor。
 *       顺序证据：`legacy2/core/race.cpp:361-363,413-415` 与
 *       `docs/analysis/43499-43284-resource-ownership.md` 顺序检查段。
 * @note PI 窗口：所有 Carrier 选择、资源准备、frame 深拷贝、预触页与 consumer
 *       分派都在 prepare 完成；窗口内只调用 prepare 时绑定的触发入口，不读
 *       spec/kind、不分配、不加锁、不写日志、不做首次触页。
 * @note 本批（G4L-3）不实现任何具体 Primitive：`prepareActor` 对三个 kind 均返回
 *       `UNSUPPORTED`（计划 §5：首个真实 Primitive 通过前保持 UNSUPPORTED）。
 *       G4L-4M/4P/4T 分别在四个分派点增加自己的分支。
 * @note 并发：wrapper 保证同一 Actor 的 execute/cancel/close 串行；worker 之间
 *       只用对象内原子/futex 协调，无全局状态、无锁。
 */

#include "carrier_internal.h"

#include "actor.h"

#include "page_internal.h"

#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/** @brief Actor 生命周期状态（只在 JNI 串行调用线程内迁移）。 */
enum {
    GLK43499_ACTOR_PREPARED = 0,
    GLK43499_ACTOR_EXECUTING = 1,
    GLK43499_ACTOR_RAN = 2,
    GLK43499_ACTOR_FAULTED = 3,
    GLK43499_ACTOR_CANCELLED = 4,
    GLK43499_ACTOR_CLOSING = 5
};

/** @brief 一个 Actor 的全部物理状态；token 即本对象地址。 */
struct glk43499_actor_impl {
    struct glk43499_page_impl *page;
    struct glk43499_carrier carrier;
    struct glk43499_pi_shared shared;

    /** @brief waiter 写入、execute 读取的中性 Carrier 物理结果（无锁，经 window_done 发布）。 */
    struct glk43499_carrier_outcome carrier_outcome;

    pthread_t waiter_thread;
    pthread_t owner_thread;
    pthread_t consumer_thread;
    int32_t waiter_started;
    int32_t owner_started;
    int32_t consumer_started;

    uint32_t owner_cpu;
    uint32_t waiter_cpu;
    uint32_t consumer_cpu;
    int32_t owner_priority;
    int32_t waiter_priority;
    int32_t consumer_priority;

    uint64_t prepare_timeout_ns;
    uint64_t parked_timeout_ns;
    uint64_t execution_timeout_ns;
    uint64_t shutdown_timeout_ns;

    uint32_t thread_count;
    glk43499_primitive_kind primitive_kind;
    int32_t state;
    /** @brief 1 表示 dirty/indeterminate：整链保留到进程退出，不 free 不 close。 */
    int32_t retained;
    int32_t window_opened;
    int32_t carrier_armed;
    int32_t carrier_disarmed;
    int32_t consumer_stuck;
    int32_t carrier_prepared;
};

/* ========================================================================== */
/* 基础助手                                                                    */
/* ========================================================================== */

/**
 * @brief 裸 futex 系统调用。
 * @param uaddr 用户态 futex 字（4 字节对齐，内嵌在 Actor 对象内）。
 * @param op futex 操作码。
 * @param val 操作值。
 * @param timeout 超时或 cast 后的第二个值（CMP_REQUEUE_PI 用 `(void *)1`）。
 * @param uaddr2 第二个 futex 地址。
 * @param val3 第三个值。
 * @return 系统调用返回值；-1 失败并保留 errno。
 * @note 阶段：waiter/owner/consumer 的 PI 协调。
 */
static long glk43499_actor_futex(uint32_t *uaddr, int32_t op, uint32_t val,
                                 const struct timespec *timeout,
                                 uint32_t *uaddr2, uint32_t val3) {
    return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

/** @brief 与内核 `struct sched_attr` 前 6 个字段对齐的本地视图。 */
struct glk43499_actor_sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
    uint32_t sched_util_min;
    uint32_t sched_util_max;
};

/**
 * @brief 对指定 TID 设置 SCHED_BATCH + nice，触发内核 PI 链遍历。
 * @param tid 目标线程 TID（waiter 线程发布的 gettid）。
 * @param nice_value -20..19 的 nice 值（调用方已校验）。
 * @return 0 成功；-1 失败（errno 保留）。
 * @note `sched_policy=3`（SCHED_BATCH）且改变 nice 是旧的物理触发方式；
 *       失败不是致命错误：consumer 会退回 PI lock 探测，见 consumer 循环。
 * @note 阶段：consumer 线程的每次调用（PI 窗口内，但只做 syscall，不分配）。
 */
static int32_t glk43499_actor_set_nice(int32_t tid, int32_t nice_value) {
    if (tid <= 0) return -1;
    struct glk43499_actor_sched_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = (uint32_t) sizeof(attr);
    attr.sched_policy = 3; /* SCHED_BATCH */
    attr.sched_nice = nice_value;
    errno = 0;
    return (int32_t) syscall(SYS_sched_setattr, tid, &attr, 0);
}

/**
 * @brief 有界 join 一个 worker：先等它发布「已退出」，再 pthread_join。
 * @param exit_flag 该 worker 的退出发布原子（见 `struct glk43499_pi_shared`）。
 * @param thread 已 start 的 pthread。
 * @param started 非空标志；0 表示线程从未创建。
 * @param timeout_ns 本次等待的相对预算。
 * @return 1 已 join 或本来未启动；0 表示线程在预算内仍未退出（调用方按 dirty
 *         处理，且绝不调用 `pthread_join`，避免无限阻塞）。
 * @note bionic 没有 `pthread_timedjoin_np`：这里用「退出标志 + 无界 join」组合，
 *       只有标志已发布（线程即将 return）时才进入 join，时间可证有界。
 * @note 阶段：prepare 回滚与 close 的终结阶梯。
 */
static int32_t glk43499_actor_join_thread(
    struct glk43499_actor_impl *actor, atomic_int *exit_flag, pthread_t thread,
    int32_t *started, uint64_t timeout_ns) {
    if (started == NULL || *started == 0) return 1;
    const uint64_t deadline = glk43499_internal_deadline(timeout_ns);
    while (atomic_load(exit_flag) == 0) {
        if (glk43499_internal_expired(deadline)) return 0;
        (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
    }
    if (pthread_join(thread, NULL) != 0) return 0;
    *started = 0;
    (void) actor;
    return 1;
}

/**
 * @brief 排空 consumer 的 in-flight 调用。
 * @param actor 非空 Actor。
 * @param timeout_ns 相对预算。
 * @return 1 已排空；0 超时（consumer 可能仍在遍历，调用方按 dirty 处理）。
 * @note 先停新触发（consumer_go=0），再等 inflight 归零。
 */
static int32_t glk43499_actor_drain_consumer(
    struct glk43499_actor_impl *actor, uint64_t timeout_ns) {
    atomic_store(&actor->shared.consumer_go, 0);
    const uint64_t deadline = glk43499_internal_deadline(timeout_ns);
    while (atomic_load(&actor->shared.consumer_inflight) != 0) {
        if (glk43499_internal_expired(deadline)) return 0;
        (void) sched_yield();
    }
    return 1;
}

/**
 * @brief 请求全部 worker 停止并唤醒 parked waiter。
 * @param actor 非空 Actor。
 * @note 顺序：先停新触发（execute_requested/consumer_go），再置 stop 并唤醒
 *       wait_futex；唤醒只让 waiter 走出 WAIT_REQUEUE_PI，不产生 Carrier 触发。
 */
static void glk43499_actor_request_stop(struct glk43499_actor_impl *actor) {
    atomic_store(&actor->shared.execute_requested, 0);
    atomic_store(&actor->shared.consumer_go, 0);
    atomic_store(&actor->shared.consumer_stop, 1);
    atomic_store(&actor->shared.owner_stop, 1);
    (void) glk43499_actor_futex(&actor->shared.wait_futex, FUTEX_WAKE,
                                (uint32_t) INT32_MAX, NULL, NULL, 0);
}

/* ========================================================================== */
/* 三个 worker 线程                                                            */
/* ========================================================================== */

/**
 * @brief consumer 线程：对 waiter TID 反复触发内核 PI 遍历。
 * @param arg 非空 `struct glk43499_actor_impl *`。
 * @return 固定 NULL。
 * @note 只读写 Actor 对象内原子；每次调用是一对 sched_setattr/PI-lock 探测。
 * @note 阶段：prepare 创建，execute 驱动，close/cancel 停止。
 */
static void *glk43499_actor_consumer_run(void *arg) {
    struct glk43499_actor_impl *actor = (struct glk43499_actor_impl *) arg;
    (void) glk43499_internal_pin_cpu(actor->consumer_cpu);
    (void) glk43499_actor_set_nice((int32_t) syscall(SYS_gettid),
                                   actor->consumer_priority);
    atomic_store(&actor->shared.consumer_ready, 1);

    int32_t seen_sequence = 0;
    while (atomic_load(&actor->shared.consumer_stop) == 0) {
        const int32_t sequence = atomic_load(&actor->shared.consumer_go);
        if (sequence == 0 || sequence == seen_sequence) {
            (void) sched_yield();
            continue;
        }
        seen_sequence = sequence;
        const int32_t waiter_tid = atomic_load(&actor->shared.waiter_tid);
        uint64_t calls_this_sequence = 0;
        while (atomic_load(&actor->shared.consumer_stop) == 0 &&
               atomic_load(&actor->shared.consumer_go) == sequence) {
            for (uint64_t burst = 0; burst < GLK43499_PHYS_CONSUMER_BURST;
                 ++burst) {
                if (atomic_load(&actor->shared.consumer_stop) != 0 ||
                    atomic_load(&actor->shared.consumer_go) != sequence) {
                    break;
                }
                (void) atomic_fetch_add(&actor->shared.consumer_calls, 1);
                atomic_store(&actor->shared.consumer_inflight, 1);
                int32_t result =
                    glk43499_actor_set_nice(waiter_tid, actor->consumer_priority);
                if (result != 0) {
                    struct timespec spin;
                    glk43499_internal_timespec(
                        GLK43499_PHYS_CONSUMER_LOCK_PI_SPIN_NS, &spin);
                    if (glk43499_actor_futex(&actor->shared.target_futex,
                                             FUTEX_LOCK_PI, 0, &spin, NULL,
                                             0) == 0) {
                        (void) glk43499_actor_futex(&actor->shared.target_futex,
                                                    FUTEX_UNLOCK_PI, 0, NULL,
                                                    NULL, 0);
                        result = 0;
                    }
                }
                if (result == 0) {
                    (void) atomic_fetch_add(&actor->shared.consumer_success, 1);
                }
                atomic_store(&actor->shared.consumer_inflight, 0);
                ++calls_this_sequence;
                if (calls_this_sequence >= GLK43499_PHYS_CONSUMER_MAX) {
                    atomic_store(&actor->shared.consumer_go, 0);
                    break;
                }
            }
        }
    }
    atomic_store(&actor->shared.consumer_exited, 1);
    return NULL;
}

/**
 * @brief owner 线程：持有 target PI 锁与 chain 锁，直到被要求停止。
 * @param arg 非空 `struct glk43499_actor_impl *`。
 * @return 固定 NULL。
 * @note 无论从哪条路径退出，退出前都发布 `owner_chain_done`，避免 waiter 悬挂。
 * @note 阶段：prepare 创建，close/cancel 停止。
 */
static void *glk43499_actor_owner_run(void *arg) {
    struct glk43499_actor_impl *actor = (struct glk43499_actor_impl *) arg;
    (void) glk43499_internal_pin_cpu(actor->owner_cpu);
    (void) glk43499_actor_set_nice((int32_t) syscall(SYS_gettid),
                                   actor->owner_priority);

    const long target_locked = glk43499_actor_futex(
        &actor->shared.target_futex, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    atomic_store(&actor->shared.owner_ready, 1);

    const uint64_t ready_deadline =
        glk43499_internal_deadline(actor->prepare_timeout_ns);
    while (atomic_load(&actor->shared.waiter_ready) == 0 &&
           atomic_load(&actor->shared.owner_stop) == 0) {
        if (glk43499_internal_expired(ready_deadline)) break;
        (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
    }
    if (atomic_load(&actor->shared.waiter_ready) == 0) {
        /* waiter 未就绪：不再取 chain，直接收尾。 */
        if (target_locked == 0) {
            (void) glk43499_actor_futex(&actor->shared.target_futex,
                                        FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
        }
        atomic_store(&actor->shared.owner_chain_done, 1);
        atomic_store(&actor->shared.owner_exited, 1);
        return NULL;
    }

    atomic_store(&actor->shared.owner_started, 1);
    /* chain 锁由 waiter 先取；本调用会阻塞到 waiter 交还（窗口结束后）。 */
    (void) glk43499_actor_futex(&actor->shared.chain_futex, FUTEX_LOCK_PI, 0,
                                NULL, NULL, 0);
    atomic_store(&actor->shared.owner_chain_done, 1);

    while (atomic_load(&actor->shared.owner_stop) == 0) {
        (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
    }
    if (target_locked == 0) {
        (void) glk43499_actor_futex(&actor->shared.target_futex, FUTEX_UNLOCK_PI,
                                    0, NULL, NULL, 0);
    }
    atomic_store(&actor->shared.owner_chain_done, 1);
    atomic_store(&actor->shared.owner_exited, 1);
    return NULL;
}

/**
 * @brief waiter 公共前置：绑定 CPU/优先级、取 chain PI 锁、等待 owner、park。
 * @param actor 非空 Actor。
 * @return 1 表示 execute 已请求打开窗口、应执行 Carrier 触发；
 *         0 表示无需触发（execute 未到达或 parked 超时）；
 *         -1 表示 chain 锁失败（失败/收尾标志已发布，调用方不得再收尾）。
 * @note 只在 prepare 创建后由对应静态 waiter 入口调用；返回前已离开内核 futex 等待。
 * @note 窗口内不分配、不加锁、不写日志。
 */
static int32_t glk43499_actor_waiter_arm(struct glk43499_actor_impl *actor) {
    (void) glk43499_internal_pin_cpu(actor->waiter_cpu);
    const int32_t own_tid = (int32_t) syscall(SYS_gettid);
    atomic_store(&actor->shared.waiter_tid, own_tid);
    (void) glk43499_actor_set_nice(own_tid, actor->waiter_priority);

    if (glk43499_actor_futex(&actor->shared.chain_futex, FUTEX_LOCK_PI, 0, NULL,
                             NULL, 0) != 0) {
        atomic_store(&actor->shared.waiter_link_failed, 1);
        atomic_store(&actor->shared.waiter_ready, 1);
        atomic_store(&actor->shared.window_done, 1);
        atomic_store(&actor->shared.route_done, 1);
        atomic_store(&actor->shared.owner_chain_done, 1);
        atomic_store(&actor->shared.waiter_exited, 1);
        return -1;
    }
    atomic_store(&actor->shared.waiter_ready, 1);

    const uint64_t ready_deadline =
        glk43499_internal_deadline(actor->prepare_timeout_ns);
    while (atomic_load(&actor->shared.owner_started) == 0 &&
           atomic_load(&actor->shared.owner_stop) == 0) {
        if (glk43499_internal_expired(ready_deadline)) break;
        (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
    }

    struct timespec parked_timeout;
    glk43499_internal_timespec(actor->parked_timeout_ns, &parked_timeout);
    atomic_store(&actor->shared.waiter_waiting, 1);
    errno = 0;
    (void) glk43499_actor_futex(&actor->shared.wait_futex,
                                FUTEX_WAIT_REQUEUE_PI, 0, &parked_timeout,
                                &actor->shared.target_futex, 0);
    const int32_t wait_errno = errno;
    return (atomic_load(&actor->shared.execute_requested) != 0 &&
            wait_errno != ETIMEDOUT)
               ? 1
               : 0;
}

/**
 * @brief waiter 公共收尾：发布 window_done、解除 chain 关联、等 owner 解除、发布退出。
 * @param actor 非空 Actor；只由已成功 arm 的入口调用。
 * @note 顺序：先发布完成信号，再 unlock chain，再等 owner_chain_done；不 free 任何资源。
 */
static void glk43499_actor_waiter_finish(struct glk43499_actor_impl *actor) {
    atomic_store(&actor->shared.window_done, 1);
    atomic_store(&actor->shared.route_done, 1);
    (void) glk43499_actor_futex(&actor->shared.chain_futex, FUTEX_UNLOCK_PI, 0,
                                NULL, NULL, 0);
    const uint64_t chain_deadline =
        glk43499_internal_deadline(actor->shutdown_timeout_ns);
    while (atomic_load(&actor->shared.owner_chain_done) == 0) {
        if (glk43499_internal_expired(chain_deadline)) break;
        (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
    }
    atomic_store(&actor->shared.waiter_exited, 1);
}

/**
 * @brief Multicast waiter 静态入口：窗口内直接调用 multicast trigger。
 * @param arg 非空 `struct glk43499_actor_impl *`。
 * @return 固定 NULL。
 * @note PI 窗口零动态分派：入口由 prepare 的普通 switch 选定；窗口内无函数指针、
 *       无 kind 读取、无 switch，直接调用本 Carrier 的触发函数。
 */
static void *glk43499_actor_waiter_multicast(void *arg) {
    struct glk43499_actor_impl *actor = (struct glk43499_actor_impl *) arg;
    const int32_t trigger = glk43499_actor_waiter_arm(actor);
    if (trigger == 1) {
        atomic_store(&actor->shared.window_opened, 1);
        glk43499_carrier_multicast_trigger(&actor->carrier, &actor->shared,
                                           &actor->carrier_outcome);
    }
    if (trigger >= 0) glk43499_actor_waiter_finish(actor);
    return NULL;
}

/**
 * @brief Pselect waiter 静态入口：窗口内直接调用 pselect trigger。
 * @param arg 非空 `struct glk43499_actor_impl *`。
 * @return 固定 NULL。
 * @note 窗口内零动态分派（同上）。
 */
static void *glk43499_actor_waiter_pselect(void *arg) {
    struct glk43499_actor_impl *actor = (struct glk43499_actor_impl *) arg;
    const int32_t trigger = glk43499_actor_waiter_arm(actor);
    if (trigger == 1) {
        atomic_store(&actor->shared.window_opened, 1);
        glk43499_carrier_pselect_trigger(&actor->carrier, &actor->shared,
                                         &actor->carrier_outcome);
    }
    if (trigger >= 0) glk43499_actor_waiter_finish(actor);
    return NULL;
}

/**
 * @brief TCP zerocopy waiter 静态入口：窗口内直接调用 tcp trigger。
 * @param arg 非空 `struct glk43499_actor_impl *`。
 * @return 固定 NULL。
 * @note 窗口内零动态分派（同上）。
 */
static void *glk43499_actor_waiter_tcp(void *arg) {
    struct glk43499_actor_impl *actor = (struct glk43499_actor_impl *) arg;
    const int32_t trigger = glk43499_actor_waiter_arm(actor);
    if (trigger == 1) {
        atomic_store(&actor->shared.window_opened, 1);
        glk43499_carrier_tcp_trigger(&actor->carrier, &actor->shared,
                                     &actor->carrier_outcome);
    }
    if (trigger >= 0) glk43499_actor_waiter_finish(actor);
    return NULL;
}

/* ========================================================================== */
/* Carrier 分派点（G4L-4M/4P/4T 各自增加一个分支）                             */
/* ========================================================================== */

/**
 * @brief 在 prepare 阶段选择并准备具体 Carrier。
 * @param actor 非空 Actor（`carrier.kind` 由本函数写入）。
 * @param spec 调用期借用的 tagged union（frame 由具体 Carrier 深拷贝）。
 * @param system_error 失败时写入 errno 语义错误码。
 * @return GLK43499_CALL_OK / GLK43499_CALL_UNSUPPORTED / 其它失败状态。
 * @note 这里是唯一读取 `spec->kind` 的位置；executeActor 不读 spec/kind。
 * @note 阶段：prepareActor 的线程创建前。
 */
static int32_t glk43499_actor_carrier_prepare(
    struct glk43499_actor_impl *actor,
    const struct glk43499_primitive_spec *spec, int32_t *system_error) {
    *system_error = 0;
    actor->carrier.kind = spec->kind;
    switch (spec->kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG: {
            const int32_t result = glk43499_carrier_multicast_prepare(
                &actor->carrier, &spec->value.multicast_sendmsg);
            if (result == 0) return GLK43499_CALL_OK;
            *system_error = result < 0 ? -result : result;
            return GLK43499_CALL_SYSTEM_ERROR;
        }
        case GLK43499_PRIMITIVE_PSELECT: {
            const int32_t result = glk43499_carrier_pselect_prepare(
                &actor->carrier, &spec->value.pselect);
            if (result == 0) return GLK43499_CALL_OK;
            *system_error = result < 0 ? -result : result;
            return GLK43499_CALL_SYSTEM_ERROR;
        }
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE: {
            const int32_t result = glk43499_carrier_tcp_prepare(
                &actor->carrier, &spec->value.tcp_zerocopy_receive);
            if (result == 0) return GLK43499_CALL_OK;
            *system_error = result < 0 ? -result : result;
            return GLK43499_CALL_SYSTEM_ERROR;
        }
        default:
            /* 其余 kind 尚未实现：各自 Carrier 批次接入一个分支。 */
            return GLK43499_CALL_UNSUPPORTED;
    }
}

/**
 * @brief 停止新触发并排空 Carrier 的 consumer 握手。
 * @param actor 非空 Actor。
 * @return 1 已 disarm；0 表示 consumer 卡死（dirty）。
 * @note 只在窗口结束后调用（execute/close），不在 PI 窗口内。
 */
static int32_t glk43499_actor_carrier_disarm(
    struct glk43499_actor_impl *actor) {
    switch (actor->carrier.kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG:
            return glk43499_carrier_multicast_disarm(&actor->carrier,
                                                     &actor->shared);
        case GLK43499_PRIMITIVE_PSELECT:
            return glk43499_carrier_pselect_disarm(&actor->carrier,
                                                   &actor->shared);
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE:
            return glk43499_carrier_tcp_disarm(&actor->carrier,
                                               &actor->shared);
        default:
            atomic_store(&actor->shared.consumer_go, 0);
            return atomic_load(&actor->shared.consumer_inflight) == 0 ? 1 : 0;
    }
}

/**
 * @brief 释放 Carrier 拥有的全部物理资源。
 * @param actor 非空 Actor。
 * @return 1 干净释放；0 表示资源无法安全释放（整链 retained）。
 * @note 只在三个 worker 全部 join 之后调用。
 */
static int32_t glk43499_actor_carrier_release(
    struct glk43499_actor_impl *actor) {
    switch (actor->carrier.kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG:
            return glk43499_carrier_multicast_release(
                &actor->carrier.value.multicast_sendmsg);
        case GLK43499_PRIMITIVE_PSELECT:
            return glk43499_carrier_pselect_release(
                &actor->carrier.value.pselect);
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE:
            return glk43499_carrier_tcp_release(
                &actor->carrier.value.tcp_zerocopy_receive);
        default:
            return 1;
    }
}

/* ========================================================================== */
/* 请求校验                                                                    */
/* ========================================================================== */

/**
 * @brief 校验输出 ABI 之外的全部 ActorSetup 物理参数。
 * @param setup 已通过 ABI 头校验的 setup。
 * @return 1 接受；0 拒绝（INVALID_ARGUMENT）。
 * @note 只做非零/范围/物理上限校验；不填默认值。
 */
static int32_t glk43499_actor_valid_setup(
    const struct glk43499_actor_setup *setup) {
    if (setup->thread_count != 3 || setup->reserved32 != 0) return 0;
    if (!glk43499_internal_valid_cpu(setup->owner_cpu) ||
        !glk43499_internal_valid_cpu(setup->waiter_cpu) ||
        !glk43499_internal_valid_cpu(setup->consumer_cpu)) {
        return 0;
    }
    const int32_t priorities[3] = {
        setup->owner_priority, setup->waiter_priority,
        setup->consumer_priority,
    };
    for (size_t index = 0; index < 3; ++index) {
        if (priorities[index] < -20 || priorities[index] > 19) return 0;
    }
    const uint64_t timeouts[4] = {
        setup->prepare_timeout_ns, setup->parked_timeout_ns,
        setup->execution_timeout_ns, setup->shutdown_timeout_ns,
    };
    for (size_t index = 0; index < 4; ++index) {
        if (timeouts[index] == 0 ||
            timeouts[index] > UINT64_C(3600000000000)) {
            return 0;
        }
    }
    return glk43499_internal_zero_u64(
        setup->reserved, sizeof(setup->reserved) / sizeof(setup->reserved[0]));
}

/**
 * @brief 校验 PrimitiveSpec 的 ABI、保留字段与选中 branch 的尾零。
 * @param primitive 调用期借用；可为 NULL。
 * @return 1 接受；0 拒绝（BAD_ABI）。
 * @note 与 `api_unavailable.c` 的历史校验同形；未选 union 分支必须保持全零。
 */
static int32_t glk43499_actor_valid_primitive(
    const struct glk43499_primitive_spec *primitive) {
    if (primitive == NULL ||
        !glk43499_internal_valid_header(&primitive->abi,
                                        (uint32_t) sizeof(*primitive)) ||
        primitive->reserved32 != 0 ||
        !glk43499_internal_zero_u64(
            primitive->reserved,
            sizeof(primitive->reserved) / sizeof(primitive->reserved[0]))) {
        return 0;
    }
    const struct glk43499_primitive_addresses *addresses = NULL;
    const uint64_t *reserved = NULL;
    size_t reserved_count = 0;
    size_t selected_size = 0;
    switch (primitive->kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG:
            addresses = &primitive->value.multicast_sendmsg.addresses;
            reserved = primitive->value.multicast_sendmsg.reserved;
            reserved_count = sizeof(primitive->value.multicast_sendmsg.reserved) /
                             sizeof(primitive->value.multicast_sendmsg.reserved[0]);
            selected_size = sizeof(primitive->value.multicast_sendmsg);
            if (primitive->value.multicast_sendmsg.reserved32 != 0) return 0;
            break;
        case GLK43499_PRIMITIVE_PSELECT:
            addresses = &primitive->value.pselect.addresses;
            reserved = primitive->value.pselect.reserved;
            reserved_count = sizeof(primitive->value.pselect.reserved) /
                             sizeof(primitive->value.pselect.reserved[0]);
            selected_size = sizeof(primitive->value.pselect);
            break;
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE:
            addresses = &primitive->value.tcp_zerocopy_receive.addresses;
            reserved = primitive->value.tcp_zerocopy_receive.reserved;
            reserved_count =
                sizeof(primitive->value.tcp_zerocopy_receive.reserved) /
                sizeof(primitive->value.tcp_zerocopy_receive.reserved[0]);
            selected_size = sizeof(primitive->value.tcp_zerocopy_receive);
            break;
        default:
            return 0;
    }
    const uint8_t *union_bytes = (const uint8_t *) &primitive->value;
    for (size_t index = selected_size; index < sizeof(primitive->value);
         ++index) {
        if (union_bytes[index] != 0) return 0;
    }
    return glk43499_internal_zero_u64(
               addresses->reserved,
               sizeof(addresses->reserved) / sizeof(addresses->reserved[0])) &&
           glk43499_internal_zero_u64(reserved, reserved_count);
}

/* ========================================================================== */
/* partial-start 回滚                                                          */
/* ========================================================================== */

/**
 * @brief 回滚一个已部分启动的 Actor（Page 尚未提交所有权）。
 * @param actor 非空 Actor。
 * @return 1 完整回滚（调用方可以 free Actor，Page 仍归 Kotlin）；
 *         0 表示有 worker 无法 join 或 Carrier 资源无法释放（必须 dirty 保留）。
 * @note 顺序：stop → drain → disarm → join waiter→owner→consumer → close Carrier。
 * @note 阶段：prepareActor 的失败路径。
 */
static int32_t glk43499_actor_rollback(struct glk43499_actor_impl *actor) {
    glk43499_actor_request_stop(actor);
    const uint64_t budget = actor->shutdown_timeout_ns;
    if (glk43499_actor_drain_consumer(actor, budget) == 0) return 0;
    if (glk43499_actor_carrier_disarm(actor) == 0) return 0;
    if (!glk43499_actor_join_thread(actor, &actor->shared.waiter_exited,
                                    actor->waiter_thread,
                                    &actor->waiter_started, budget)) {
        return 0;
    }
    if (!glk43499_actor_join_thread(actor, &actor->shared.owner_exited,
                                    actor->owner_thread, &actor->owner_started,
                                    budget)) {
        return 0;
    }
    if (!glk43499_actor_join_thread(actor, &actor->shared.consumer_exited,
                                    actor->consumer_thread,
                                    &actor->consumer_started, budget)) {
        return 0;
    }
    if (glk43499_actor_carrier_release(actor) == 0) return 0;
    return 1;
}

/* ========================================================================== */
/* 公开入口                                                                    */
/* ========================================================================== */

/**
 * @brief 创建 Actor、消费 Page，并在线程创建前绑定具体 Carrier。
 * @param page 由调用方唯一持有且尚未消费的 Page token。
 * @param setup 调用期借用；thread_count 必须为 3。
 * @param primitive 调用期借用；选中分支与 frame bytes 被深拷贝。
 * @param result 必填输出。
 * @return Actor token；成功时 Actor 接管 Page，调用方必须将 Page token 置 0；
 *         clean 失败返回 0 且 Page 仍归调用方；dirty/indeterminate 失败时
 *         receipt.page_consumed 为 1，调用方也必须置 0，Native 保留资源到进程退出。
 * @note 入口顺序：输出 ABI → setup/primitive ABI → token 非零 → 物理参数 →
 *       资源/线程准备 → 全部就绪后才提交 Page 所有权。
 * @note 阶段：G4L-3；三个 kind 在本批均返回 UNSUPPORTED（计划 §5）。
 */
glk43499_handle glk43499_prepare_actor(
    glk43499_handle page, const struct glk43499_actor_setup *setup,
    const struct glk43499_primitive_spec *primitive,
    struct glk43499_actor_prepare_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_INVALID_HANDLE;
    }
    memset(&result->receipt, 0, sizeof(result->receipt));
    if (setup == NULL ||
        !glk43499_internal_valid_header(&setup->abi,
                                        (uint32_t) sizeof(*setup)) ||
        !glk43499_actor_valid_primitive(primitive)) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_BAD_ABI,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }
    if (page == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }
    if (!glk43499_actor_valid_setup(setup)) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }

    struct glk43499_actor_impl *actor =
        (struct glk43499_actor_impl *) calloc(
            1, sizeof(struct glk43499_actor_impl));
    if (actor == NULL) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, ENOMEM, 0);
        return GLK43499_INVALID_HANDLE;
    }
    atomic_init(&actor->shared.waiter_ready, 0);
    atomic_init(&actor->shared.waiter_waiting, 0);
    atomic_init(&actor->shared.waiter_link_failed, 0);
    atomic_init(&actor->shared.owner_ready, 0);
    atomic_init(&actor->shared.owner_started, 0);
    atomic_init(&actor->shared.owner_chain_done, 0);
    atomic_init(&actor->shared.owner_stop, 0);
    atomic_init(&actor->shared.route_done, 0);
    atomic_init(&actor->shared.waiter_tid, 0);
    atomic_init(&actor->shared.consumer_ready, 0);
    atomic_init(&actor->shared.consumer_go, 0);
    atomic_init(&actor->shared.consumer_stop, 0);
    atomic_init(&actor->shared.consumer_calls, 0);
    atomic_init(&actor->shared.consumer_success, 0);
    atomic_init(&actor->shared.consumer_inflight, 0);
    atomic_init(&actor->shared.route_delay_usec, 0);
    atomic_init(&actor->shared.execute_requested, 0);
    atomic_init(&actor->shared.window_opened, 0);
    atomic_init(&actor->shared.window_done, 0);
    atomic_init(&actor->shared.waiter_exited, 0);
    atomic_init(&actor->shared.owner_exited, 0);
    atomic_init(&actor->shared.consumer_exited, 0);

    actor->page = NULL;
    actor->owner_cpu = setup->owner_cpu;
    actor->waiter_cpu = setup->waiter_cpu;
    actor->consumer_cpu = setup->consumer_cpu;
    actor->owner_priority = setup->owner_priority;
    actor->waiter_priority = setup->waiter_priority;
    actor->consumer_priority = setup->consumer_priority;
    actor->prepare_timeout_ns = setup->prepare_timeout_ns;
    actor->parked_timeout_ns = setup->parked_timeout_ns;
    actor->execution_timeout_ns = setup->execution_timeout_ns;
    actor->shutdown_timeout_ns = setup->shutdown_timeout_ns;
    actor->thread_count = setup->thread_count;
    actor->primitive_kind = primitive->kind;
    actor->state = GLK43499_ACTOR_PREPARED;
    actor->carrier.kind = GLK43499_PRIMITIVE_INVALID;

    int32_t system_error = 0;
    const int32_t prepare_status =
        glk43499_actor_carrier_prepare(actor, primitive, &system_error);
    if (prepare_status != GLK43499_CALL_OK) {
        /* Carrier 未建立：没有任何物理资源需要回滚，直接释放候选对象。 */
        free(actor);
        glk43499_internal_set_call(&result->call, prepare_status,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, system_error, 0);
        return GLK43499_INVALID_HANDLE;
    }
    actor->carrier_prepared = 1;

    /* 线程入口在 prepare（窗口外）用普通 switch 选定：窗口内零动态分派。 */
    void *(*waiter_entry)(void *) = NULL;
    switch (actor->primitive_kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG:
            waiter_entry = glk43499_actor_waiter_multicast;
            break;
        case GLK43499_PRIMITIVE_PSELECT:
            waiter_entry = glk43499_actor_waiter_pselect;
            break;
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE:
            waiter_entry = glk43499_actor_waiter_tcp;
            break;
        default:
            waiter_entry = NULL;
            break;
    }
    if (waiter_entry == NULL) {
        (void) glk43499_actor_carrier_release(actor);
        free(actor);
        glk43499_internal_set_call(&result->call, GLK43499_CALL_UNSUPPORTED,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }

    /* 线程创建顺序：consumer → owner → waiter（与旧实现一致）。 */
    int32_t create_error = 0;
    if (pthread_create(&actor->consumer_thread, NULL,
                       glk43499_actor_consumer_run, actor) != 0) {
        create_error = errno != 0 ? errno : EAGAIN;
    } else {
        actor->consumer_started = 1;
        if (pthread_create(&actor->owner_thread, NULL,
                           glk43499_actor_owner_run, actor) != 0) {
            create_error = errno != 0 ? errno : EAGAIN;
        } else {
            actor->owner_started = 1;
            if (pthread_create(&actor->waiter_thread, NULL, waiter_entry,
                               actor) != 0) {
                create_error = errno != 0 ? errno : EAGAIN;
            } else {
                actor->waiter_started = 1;
            }
        }
    }
    if (create_error == 0) {
        /* 三线程就绪：consumer_ready、owner_started 且 waiter 已 park 在
         * WAIT_REQUEUE_PI（waiter_waiting）——只有此时 CMP_REQUEUE_PI 才可安全发出。 */
        const uint64_t ready_deadline =
            glk43499_internal_deadline(actor->prepare_timeout_ns);
        for (;;) {
            const int32_t ready =
                atomic_load(&actor->shared.consumer_ready) != 0 &&
                atomic_load(&actor->shared.owner_started) != 0 &&
                atomic_load(&actor->shared.waiter_waiting) != 0;
            if (ready) break;
            if (glk43499_internal_expired(ready_deadline)) {
                create_error = ETIMEDOUT;
                break;
            }
            (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
        }
        if (create_error == 0 &&
            atomic_load(&actor->shared.waiter_link_failed) != 0) {
            create_error = EPROTO;
        }
    }

    if (create_error != 0) {
        const int32_t rollback_clean = glk43499_actor_rollback(actor);
        if (rollback_clean) {
            /* clean 回滚：Page 所有权从未提交，原样归还 Kotlin（无需 release）。 */
            free(actor);
            glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                       GLK43499_EFFECT_NOT_APPLIED,
                                       GLK43499_RESOURCE_INVALID, create_error,
                                       0);
            return GLK43499_INVALID_HANDLE;
        }
        /* 无法证明完整回滚：必须真正持有 Page 才能报告 page_consumed=1。 */
        struct glk43499_page_impl *retained_page = glk43499_page_from_handle(page);
        retained_page->state = 2; /* CONSUMED：整链由 Actor→Page 独占并 retained */
        actor->page = retained_page;
        actor->retained = 1;
        result->receipt.page_content_generation =
            glk43499_page_content_generation(retained_page);
        result->receipt.primitive_kind = actor->primitive_kind;
        result->receipt.thread_count = actor->thread_count;
        result->receipt.page_consumed = 1;
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_DIRTY,
                                   GLK43499_RESOURCE_QUARANTINED, create_error,
                                   1);
        return GLK43499_INVALID_HANDLE;
    }

    /* 提交点：Carrier、同步对象与三线程全部就绪，Page 所有权转移给 Actor。 */
    struct glk43499_page_impl *page_impl = glk43499_page_from_handle(page);
    page_impl->state = 2; /* CONSUMED：此后只有 Actor 持有该 Page */
    actor->page = page_impl;

    result->receipt.page_content_generation =
        glk43499_page_content_generation(page_impl);
    result->receipt.primitive_kind = actor->primitive_kind;
    result->receipt.thread_count = actor->thread_count;
    result->receipt.page_consumed = 1;
    result->receipt.owner_ready = 1;
    result->receipt.waiter_ready = 1;
    result->receipt.consumer_ready = 1;
    result->receipt.carrier_prepared = 1;
    for (size_t index = 0;
         index < sizeof(result->receipt.reserved) /
                     sizeof(result->receipt.reserved[0]);
         ++index) {
        result->receipt.reserved[index] = 0;
    }
    glk43499_internal_set_call(&result->call, GLK43499_CALL_OK,
                               GLK43499_EFFECT_CLEAN_SUCCESS,
                               GLK43499_RESOURCE_PREPARED, 0, 0);
    return glk43499_internal_ptr_handle(actor);
}

/**
 * @brief 执行 prepare 时已绑定的物理 Carrier（恰好一次）。
 * @param actor PREPARED 状态的 Actor handle。
 * @param result 必填输出。
 * @return glk43499_call_status。
 * @note 本入口没有 route/tag/spec 参数，也不读取 spec/kind；PI 窗口内不分配、
 *       不打印、不加锁，只做 prepare 已绑定的 Carrier 调用与内联原子操作。
 * @note 恰好一次：第二次 execute 返回 INVALID_ARGUMENT 且不改变物理状态。
 * @note 阶段：G4L-3。
 */
int32_t glk43499_execute_actor(glk43499_handle actor,
                               struct glk43499_actor_execute_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    memset(&result->receipt, 0, sizeof(result->receipt));
    if (actor == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct glk43499_actor_impl *impl =
        (struct glk43499_actor_impl *) glk43499_internal_handle_ptr(actor);
    if (impl->retained != 0 || impl->state != GLK43499_ACTOR_PREPARED) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_PREPARED, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    impl->state = GLK43499_ACTOR_EXECUTING;

    /* settle：prepare 已保证 waiter 落在 WAIT_REQUEUE_PI，这里再留一个物理间隙。 */
    (void) usleep((useconds_t) GLK43499_PHYS_SETUP_SETTLE_US);
    /* 打开窗口：先发布 execute_requested，再 requeue。 */
    atomic_store(&impl->shared.execute_requested, 1);
    errno = 0;
    (void) glk43499_actor_futex(&impl->shared.wait_futex,
                                FUTEX_CMP_REQUEUE_PI, 1,
                                (const struct timespec *) (uintptr_t) 1,
                                &impl->shared.target_futex, 0);

    const uint64_t deadline =
        glk43499_internal_deadline(impl->execution_timeout_ns);
    int32_t window_done = 0;
    for (;;) {
        if (atomic_load(&impl->shared.window_done) != 0) {
            window_done = 1;
            break;
        }
        if (glk43499_internal_expired(deadline)) break;
        (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
    }

    if (!window_done) {
        /* PI worker 仍活着：不得 unwind，Actor/Page/Carrier 整链保留到进程退出。
         * 本分支不释放 Page（release 只允许发生在 close 的记账路径）。 */
        impl->retained = 1;
        impl->state = GLK43499_ACTOR_FAULTED;
        result->receipt.page_content_generation =
            glk43499_page_content_generation(impl->page);
        result->receipt.window_opened =
            (uint32_t) atomic_load(&impl->shared.window_opened);
        result->receipt.carrier_armed = result->receipt.window_opened;
        result->receipt.consumer_inflight_at_return =
            (uint64_t) atomic_load(&impl->shared.consumer_inflight);
        result->receipt.retained_for_process_exit = 1;
        glk43499_internal_set_call(&result->call, GLK43499_CALL_TIMEOUT,
                                   GLK43499_EFFECT_DIRTY,
                                   GLK43499_RESOURCE_QUARANTINED, ETIMEDOUT, 1);
        return GLK43499_CALL_TIMEOUT;
    }

    impl->window_opened = (int32_t) atomic_load(&impl->shared.window_opened);
    impl->carrier_armed = impl->window_opened;

    /* 窗口已关闭：停触发并排空 consumer，再 disarm Carrier。 */
    int32_t drained = glk43499_actor_drain_consumer(impl,
                                                    impl->execution_timeout_ns);
    if (drained == 0) {
        impl->consumer_stuck = 1;
        impl->retained = 1;
        impl->state = GLK43499_ACTOR_FAULTED;
        result->receipt.page_content_generation =
            glk43499_page_content_generation(impl->page);
        result->receipt.window_opened = (uint32_t) impl->window_opened;
        result->receipt.carrier_armed = (uint32_t) impl->carrier_armed;
        result->receipt.consumer_inflight_at_return =
            (uint64_t) atomic_load(&impl->shared.consumer_inflight);
        result->receipt.retained_for_process_exit = 1;
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_DIRTY,
                                   GLK43499_RESOURCE_QUARANTINED, 0, 1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }
    const int32_t disarm_clean = glk43499_actor_carrier_disarm(impl);
    impl->carrier_disarmed = disarm_clean;
    if (disarm_clean == 0) {
        impl->consumer_stuck = 1;
        impl->retained = 1;
        impl->state = GLK43499_ACTOR_FAULTED;
    } else {
        impl->state = GLK43499_ACTOR_RAN;
    }

    const uint64_t calls =
        (uint64_t) atomic_load(&impl->shared.consumer_calls);
    const uint64_t successes =
        (uint64_t) atomic_load(&impl->shared.consumer_success);
    result->receipt.page_content_generation =
        glk43499_page_content_generation(impl->page);
    result->receipt.consumer_call_count = calls;
    result->receipt.consumer_success_count = successes;
    result->receipt.consumer_inflight_at_return =
        (uint64_t) atomic_load(&impl->shared.consumer_inflight);
    result->receipt.owner_ready = 1;
    result->receipt.waiter_parked = 1;
    result->receipt.carrier_armed = (uint32_t) impl->carrier_armed;
    result->receipt.window_opened = (uint32_t) impl->window_opened;
    result->receipt.carrier_disarmed = (uint32_t) impl->carrier_disarmed;
    result->receipt.retained_for_process_exit = (uint32_t) impl->retained;
    for (size_t index = 0;
         index < sizeof(result->receipt.reserved) /
                     sizeof(result->receipt.reserved[0]);
         ++index) {
        result->receipt.reserved[index] = 0;
    }

    if (impl->retained != 0) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_DIRTY,
                                   GLK43499_RESOURCE_QUARANTINED, 0, 1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }
    if (impl->window_opened == 0) {
        /* execute 到达前 waiter 已 parked 超时：没有任何 Carrier 动作发生。 */
        glk43499_internal_set_call(&result->call, GLK43499_CALL_TIMEOUT,
                                   GLK43499_EFFECT_CLEAN_FAILURE,
                                   GLK43499_RESOURCE_PREPARED, ETIMEDOUT, 0);
        return GLK43499_CALL_TIMEOUT;
    }
    const int32_t observed_write =
        impl->carrier_outcome.observed_write != 0 && calls > 0 && successes > 0;
    if (!observed_write) {
        /* 事务完成不等于物理写成功：必须由 Carrier 中性结果与真实计数共同证明。 */
        const int32_t failure =
            impl->carrier_outcome.failed != 0 ? impl->carrier_outcome.failed : EIO;
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INTERNAL_ERROR,
                                   GLK43499_EFFECT_CLEAN_FAILURE,
                                   GLK43499_RESOURCE_PREPARED, failure, 0);
        return GLK43499_CALL_INTERNAL_ERROR;
    }
    glk43499_internal_set_call(&result->call, GLK43499_CALL_OK,
                               GLK43499_EFFECT_CLEAN_SUCCESS,
                               GLK43499_RESOURCE_RUNNING, 0, 0);
    return GLK43499_CALL_OK;
}

/**
 * @brief 请求停止新触发并排空正在执行的 Carrier 操作（不销毁 Actor）。
 * @param actor 有效 Actor handle。
 * @param result 必填输出。
 * @return glk43499_call_status；不消费 token。
 * @note 阶段：G4L-3。
 */
int32_t glk43499_cancel_actor(glk43499_handle actor,
                              struct glk43499_close_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (actor == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_close(result, GLK43499_CALL_INVALID_ARGUMENT,
                                    GLK43499_EFFECT_NOT_APPLIED,
                                    GLK43499_RESOURCE_INVALID, 0, 0, 0, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct glk43499_actor_impl *impl =
        (struct glk43499_actor_impl *) glk43499_internal_handle_ptr(actor);
    glk43499_actor_request_stop(impl);
    const int32_t drained =
        glk43499_actor_drain_consumer(impl, impl->shutdown_timeout_ns);
    (void) glk43499_actor_carrier_disarm(impl);
    if (impl->state == GLK43499_ACTOR_PREPARED ||
        impl->state == GLK43499_ACTOR_EXECUTING) {
        impl->state = GLK43499_ACTOR_CANCELLED;
    }
    glk43499_internal_set_close(
        result, GLK43499_CALL_OK, GLK43499_EFFECT_NOT_APPLIED,
        drained ? GLK43499_RESOURCE_PREPARED : GLK43499_RESOURCE_RUNNING, 0, 1,
        0, 0, 0);
    return GLK43499_CALL_OK;
}

/**
 * @brief 按固定顺序终结 Actor 及其拥有的 Page。
 * @param actor 由调用方唯一持有的 Actor token；wrapper 在调用前置 0。
 * @param result 必填输出。
 * @return glk43499_call_status；输出 ABI 无效或 token 为 0 时未接受（调用方
 *         恢复原 token）；一旦接受，无论释放或保留结果都消费 token。
 * @note 顺序：stop → drain consumer inflight → disarm → detach PI/同步 →
 *       join waiter→owner→consumer → close Carrier → release Page → free Actor。
 * @note 未知引用使 Actor、Carrier 与 Page 整体保留到进程退出（不 free、不 close）。
 * @note 阶段：G4L-3 终结。
 */
int32_t glk43499_close_actor(glk43499_handle actor,
                             struct glk43499_close_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (actor == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_close(result, GLK43499_CALL_INVALID_ARGUMENT,
                                    GLK43499_EFFECT_NOT_APPLIED,
                                    GLK43499_RESOURCE_INVALID, 0, 0, 0, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct glk43499_actor_impl *impl =
        (struct glk43499_actor_impl *) glk43499_internal_handle_ptr(actor);
    impl->state = GLK43499_ACTOR_CLOSING;

    if (impl->retained != 0) {
        /* 已经 dirty：不得再碰任何资源。 */
        glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                    GLK43499_EFFECT_DIRTY,
                                    GLK43499_RESOURCE_QUARANTINED, 0, 1, 0, 0,
                                    1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }

    glk43499_actor_request_stop(impl);
    const uint64_t budget = impl->shutdown_timeout_ns;
    if (glk43499_actor_drain_consumer(impl, budget) == 0) {
        impl->retained = 1;
        glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                    GLK43499_EFFECT_DIRTY,
                                    GLK43499_RESOURCE_QUARANTINED, 0, 1, 0, 0,
                                    1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }
    if (glk43499_actor_carrier_disarm(impl) == 0) {
        impl->retained = 1;
        glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                    GLK43499_EFFECT_DIRTY,
                                    GLK43499_RESOURCE_QUARANTINED, 0, 1, 0, 0,
                                    1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }

    /* detach PI/同步：依次 join waiter→owner→consumer（冻结顺序）。 */
    int32_t joined = 1;
    if (!glk43499_actor_join_thread(impl, &impl->shared.waiter_exited,
                                    impl->waiter_thread, &impl->waiter_started,
                                    budget)) {
        joined = 0;
    }
    if (joined &&
        !glk43499_actor_join_thread(impl, &impl->shared.owner_exited,
                                    impl->owner_thread, &impl->owner_started,
                                    budget)) {
        joined = 0;
    }
    if (joined &&
        !glk43499_actor_join_thread(impl, &impl->shared.consumer_exited,
                                    impl->consumer_thread,
                                    &impl->consumer_started, budget)) {
        joined = 0;
    }
    if (!joined) {
        impl->retained = 1;
        glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                    GLK43499_EFFECT_DIRTY,
                                    GLK43499_RESOURCE_QUARANTINED, 0, 1, 0, 0,
                                    1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }

    if (glk43499_actor_carrier_release(impl) == 0) {
        impl->retained = 1;
        glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                    GLK43499_EFFECT_DIRTY,
                                    GLK43499_RESOURCE_QUARANTINED, 0, 1, 1, 0,
                                    1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }

    /* 完整所有权链：Carrier 已释放 → Page 释放（记账）→ Actor 释放。
     * Page 释放失败不得报 released：Actor/Page 身份整体保留到进程退出。 */
    if (glk43499_page_release_checked(impl->page) == 0) {
        impl->retained = 1;
        glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                    GLK43499_EFFECT_DIRTY,
                                    GLK43499_RESOURCE_QUARANTINED, 0, 1, 1, 0,
                                    1);
        return GLK43499_CALL_SYSTEM_ERROR;
    }
    impl->page = NULL;
    free(impl);
    glk43499_internal_set_close(result, GLK43499_CALL_OK,
                                GLK43499_EFFECT_CLEAN_SUCCESS,
                                GLK43499_RESOURCE_CLOSED, 0, 1, 1, 1, 0);
    return GLK43499_CALL_OK;
}
