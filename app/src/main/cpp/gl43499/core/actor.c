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

#include <time.h>

/**
 * @brief task-108：单调时钟 ns（诊断时间戳；PI 窗口内允许只读时钟）。
 * @return CLOCK_MONOTONIC 纳秒；失败返回 0。
 */
static uint64_t glk43499_actor_now_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t) now.tv_sec * UINT64_C(1000000000) + (uint64_t) now.tv_nsec;
}

#include "actor.h"
#include "cpu_pin_internal.h"

#include "page_internal.h"

#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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

    /* P0a：全部来自请求的 PI 竞态/等待参数（无默认回退）。 */
    uint64_t consumer_max;
    uint64_t consumer_burst;
    uint64_t lock_pi_spin_ns;
    uint64_t poll_us;
    uint64_t setup_settle_us;
    int64_t requeue_expected;
    uint64_t disarm_spin_limit;

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
 * @note `sched_policy=3`（SCHED_BATCH）+ 轮转 nice 是旧的物理触发方式（R3/B3）；
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
        (void) usleep((useconds_t) actor->poll_us);
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
    /* G08：consumer 恒 pin（legacy 同形；不接受 no-pin 哨兵）。 */
    (void) glk43499_internal_pin_cpu(actor->consumer_cpu);
    /* R3/B3：consumer 不再自设固定 nice；niceness 只以轮转值作用在 waiter TID 上
     * （legacy `threads.cpp:135-138`：calls%19+1；19 由系统头 PRIO_MAX-1 派生）。 */
    atomic_store(&actor->shared.consumer_ready, 1);

    int32_t seen_sequence = 0;
    while (atomic_load(&actor->shared.consumer_stop) == 0) {
        const int32_t sequence = atomic_load(&actor->shared.consumer_go);
        if (sequence == 0 || sequence == seen_sequence) {
            (void) sched_yield();
            continue;
        }
        seen_sequence = sequence;
        /* R3a-2/P1：consumer 起跑延迟（旧 `race.cpp:256-259`：每个 sequence 先
         * usleep(route_delay_usec) 再开始 burst）。延迟由 carrier 在 PI 窗口内发布、
         * consumer 侧执行，不污染 waiter 的拷贝→walk 序列。 */
        const uint32_t delay_usec =
            atomic_load(&actor->shared.route_delay_usec);
        if (delay_usec > 0) (void) usleep((useconds_t) delay_usec);
        const int32_t waiter_tid = atomic_load(&actor->shared.waiter_tid);
        uint64_t calls_this_sequence = 0;
        while (atomic_load(&actor->shared.consumer_stop) == 0 &&
               atomic_load(&actor->shared.consumer_go) == sequence) {
            for (uint64_t burst = 0; burst < actor->consumer_burst;
                 ++burst) {
                if (atomic_load(&actor->shared.consumer_stop) != 0 ||
                    atomic_load(&actor->shared.consumer_go) != sequence) {
                    break;
                }
                (void) atomic_fetch_add(&actor->shared.consumer_calls, 1);
                atomic_store(&actor->shared.consumer_inflight, 1);
                /* R3/B3 nice 轮转：值域 1..PRIO_MAX-1（旧 calls%19+1 的等价派生）。 */
                const int32_t rotated_nice =
                    (int32_t) ((uint64_t) atomic_load(
                                   &actor->shared.consumer_calls) %
                               (uint64_t) (PRIO_MAX - 1)) +
                    1;
                int32_t result =
                    glk43499_actor_set_nice(waiter_tid, rotated_nice);
                if (result != 0) {
                    struct timespec spin;
                    glk43499_internal_timespec(
                        actor->lock_pi_spin_ns, &spin);
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
                if (calls_this_sequence >= actor->consumer_max) {
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
    /* G08：owner may be no-pin sentinel（首次真机门禁=只 pin consumer）。 */
    if (glk43499_internal_cpu_pin_required(actor->owner_cpu)) {
        (void) glk43499_internal_pin_cpu(actor->owner_cpu);
    }
    /* R3/B3：owner 不再预置 SCHED_BATCH+nice（legacy 只在 consumer 侧轮转）。 */

    const long target_locked = glk43499_actor_futex(
        &actor->shared.target_futex, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    atomic_store(&actor->shared.owner_ready, 1);

    const uint64_t ready_deadline =
        glk43499_internal_deadline(actor->prepare_timeout_ns);
    while (atomic_load(&actor->shared.waiter_ready) == 0 &&
           atomic_load(&actor->shared.owner_stop) == 0) {
        if (glk43499_internal_expired(ready_deadline)) break;
        (void) usleep((useconds_t) actor->poll_us);
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
        (void) usleep((useconds_t) actor->poll_us);
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
    /* G08：waiter may be no-pin sentinel（首次真机门禁=只 pin consumer）。 */
    if (glk43499_internal_cpu_pin_required(actor->waiter_cpu)) {
        (void) glk43499_internal_pin_cpu(actor->waiter_cpu);
    }
    const int32_t own_tid = (int32_t) syscall(SYS_gettid);
    atomic_store(&actor->shared.waiter_tid, own_tid);
    /* R3/B3：waiter 不预置 nice；轮转只由 consumer 作用于本 TID。 */

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
        (void) usleep((useconds_t) actor->poll_us);
    }

    /* task-111（legacy2 证据）：legacy2/core/race.cpp:171-189 的 waiter timeout 是
     * **绝对 CLOCK_MONOTONIC deadline**（clock_gettime + wait window 累加），不是相对
     * timespec。E1 实测相对 {1,0} 被内核当作已过期 absolute ⇒ 32µs ETIMEDOUT；
     * 此处逐行对拍 legacy2 构造方式（不改失败/超时语义，timeout 预算仍=parked_timeout_ns）。 */
    struct timespec parked_timeout;
    (void) clock_gettime(CLOCK_MONOTONIC, &parked_timeout);
    parked_timeout.tv_sec += (time_t) (actor->parked_timeout_ns / UINT64_C(1000000000));
    parked_timeout.tv_nsec += (long) (actor->parked_timeout_ns % UINT64_C(1000000000));
    if (parked_timeout.tv_nsec >= 1000000000L) {
        parked_timeout.tv_sec++;
        parked_timeout.tv_nsec -= 1000000000L;
    }
    atomic_store(&actor->shared.waiter_waiting, 1);
    atomic_store(&actor->shared.waiter_parked_timeout_ns, actor->parked_timeout_ns);
    atomic_store(&actor->shared.waiter_op, FUTEX_WAIT_REQUEUE_PI);
    atomic_store(&actor->shared.waiter_val, 0);
    atomic_store(&actor->shared.waiter_val3, 0);
    atomic_store(&actor->shared.waiter_uaddr2_tid,
                 (int32_t) __atomic_load_n(&actor->shared.target_futex, __ATOMIC_ACQUIRE));
    atomic_store(&actor->shared.waiter_in_futex, 1);
    atomic_store(&actor->shared.waiter_futex_enter_ns,
                 glk43499_actor_now_ns());
    errno = 0;
    const long wait_ret = glk43499_actor_futex(
        &actor->shared.wait_futex, FUTEX_WAIT_REQUEUE_PI, 0, &parked_timeout,
        &actor->shared.target_futex, 0);
    const int32_t wait_errno = errno;
    atomic_store(&actor->shared.waiter_futex_exit_ns,
                 glk43499_actor_now_ns());
    atomic_store(&actor->shared.waiter_in_futex, 0);
    atomic_store(&actor->shared.waiter_wait_ret, (int32_t) wait_ret);
    atomic_store(&actor->shared.waiter_wait_errno, wait_errno);
    /* task-112（legacy2 证据）：legacy2/core/race.cpp:183-193 在 waiter 的
     * FUTEX_WAIT_REQUEUE_PI 返回后显式 `(void) wait_ret;` 并**无条件 route_dispatch**；
     * 不因 wait_ret/errno（含 EDEADLK/ETIMEDOUT）或 execute_requested 中止 dispatch。
     * 本实现逐行对齐：保留 wait_ret/errno 记录（诊断），dispatch 恒 1。
     * 例外：legacy waiter_thread 无 stop/cancel 门控（race.cpp:183-230），故无例外。 */
    atomic_store(&actor->shared.waiter_dispatched, 1);
    return 1;
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
        (void) usleep((useconds_t) actor->poll_us);
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
        /* Step B2（Lead 批准 semantic change；legacy route_multicast.cpp:133-139 的
         * ROUTE_OK 判据）：先执行 carrier，仅 `stamp 成功 或 consumer_success>0`
         * （由 carrier_outcome.observed_write 表达）后才置 window_opened=1；失败/未发送
         * 不得进入写窗口尾段（wait_ret 仍忽略，race.cpp:183-193）。 */
        atomic_store(&actor->shared.route_enter_ns, glk43499_actor_now_ns());
        glk43499_carrier_multicast_trigger(&actor->carrier, &actor->shared,
                                           &actor->carrier_outcome);
        atomic_store(&actor->shared.route_exit_ns, glk43499_actor_now_ns());
        if (actor->carrier_outcome.observed_write != 0) {
            atomic_store(&actor->shared.window_opened, 1);
        }
    }
    if (trigger >= 0) {
        /* D3（kernel5 multicast ghost disarm）：路由后 dummy PI 移除
         * pi_blocked_on ghost；参数全部来自请求，失败/0=不做 disarm（同 legacy
         * FAILURE→不做 语义），不影响 window 完成信号。 */
        const struct glk43499_carrier_multicast *multicast =
            &actor->carrier.value.multicast_sendmsg;
        if (multicast->disarm_required != 0) {
            uint32_t dummy_pi = multicast->futex_waiter_flag |
                                (uint32_t) multicast->ghost_pid;
            struct timespec expired = {0, 0};
            errno = 0;
            const long disarm_result =
                glk43499_actor_futex(&dummy_pi, FUTEX_LOCK_PI, 0, &expired,
                                     NULL, 0);
            const int32_t disarm_errno = errno;
            /* R3a-2/B7：ghost disarm 的 ret/errno 发布到回执（可证伪诊断）。 */
            atomic_store(&actor->shared.ghost_disarm_result,
                         (int32_t) disarm_result);
            atomic_store(&actor->shared.ghost_disarm_errno, disarm_errno);
        }
        glk43499_actor_waiter_finish(actor);
    }
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
        /* Step B2（legacy ROUTE_OK 语义，同 multicast 注释）。 */
        atomic_store(&actor->shared.route_enter_ns, glk43499_actor_now_ns());
        glk43499_carrier_pselect_trigger(&actor->carrier, &actor->shared,
                                         &actor->carrier_outcome);
        atomic_store(&actor->shared.route_exit_ns, glk43499_actor_now_ns());
        if (actor->carrier_outcome.observed_write != 0) {
            atomic_store(&actor->shared.window_opened, 1);
        }
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
        /* Step B2（legacy ROUTE_OK 语义，同 multicast 注释）。 */
        atomic_store(&actor->shared.route_enter_ns, glk43499_actor_now_ns());
        glk43499_carrier_tcp_trigger(&actor->carrier, &actor->shared,
                                     &actor->carrier_outcome);
        atomic_store(&actor->shared.route_exit_ns, glk43499_actor_now_ns());
        if (actor->carrier_outcome.observed_write != 0) {
            atomic_store(&actor->shared.window_opened, 1);
        }
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
            if (result == 0) {
                struct glk43499_carrier_multicast *multicast =
                    &actor->carrier.value.multicast_sendmsg;
                multicast->arm_hold_ns =
                    spec->value.multicast_sendmsg.arm_hold_ns;
                multicast->arm_sequence =
                    spec->value.multicast_sendmsg.arm_sequence;
                multicast->disarm_spin_limit = actor->disarm_spin_limit;
                /* D3：ghost disarm 参数来自请求（0/失败=不做 disarm）。 */
                multicast->ghost_pid = spec->value.multicast_sendmsg.ghost_pid;
                multicast->futex_waiter_flag =
                    spec->value.multicast_sendmsg.futex_waiter_flag;
                multicast->disarm_required =
                    spec->value.multicast_sendmsg.disarm_required;
                return GLK43499_CALL_OK;
            }
            *system_error = result < 0 ? -result : result;
            return GLK43499_CALL_SYSTEM_ERROR;
        }
        case GLK43499_PRIMITIVE_PSELECT: {
            const int32_t result = glk43499_carrier_pselect_prepare(
                &actor->carrier, &spec->value.pselect);
            if (result == 0) {
                actor->carrier.value.pselect.per_attempt_delay_ns =
                    spec->value.pselect.per_attempt_delay_ns;
                actor->carrier.value.pselect.disarm_spin_limit =
                    actor->disarm_spin_limit;
                return GLK43499_CALL_OK;
            }
            *system_error = result < 0 ? -result : result;
            return GLK43499_CALL_SYSTEM_ERROR;
        }
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE: {
            /* R3b：发送/映射两个填充字节由 tcp spec 唯一携带（send_fill_byte /
             * mapping_fill_byte），在 carrier prepare 使用前即可读；不再从 Page 的
             * landing.fill_byte 借用（A 决策：每个物理使用者一个请求字段）。 */
            const int32_t result = glk43499_carrier_tcp_prepare(
                &actor->carrier, &spec->value.tcp_zerocopy_receive);
            if (result == 0) {
                actor->carrier.value.tcp_zerocopy_receive.disarm_spin_limit =
                    actor->disarm_spin_limit;
                actor->carrier.value.tcp_zerocopy_receive.poll_us =
                    actor->poll_us;
                return GLK43499_CALL_OK;
            }
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
 * @note 只做非零/范围/物理上限/跨字段不变量校验；不填默认值。
 * @note R2b：原固定上限（1h/1e6/999999999/2^26/nice -20..19）改为
 *       请求值之间或系统头派生的自洽校验（PRIO_MIN/PRIO_MAX、INT32_MAX、
 *       UINT32_MAX、deadline 加法不溢出）。
 */
static int32_t glk43499_actor_valid_setup(
    const struct glk43499_actor_setup *setup) {
    if (setup->thread_count != 3) return 0;
    /* G08：owner/waiter 允许 no-pin 哨兵（0xFFFFFFFF）；consumer 必须有效 CPU（保持 pin）。 */
    if ((!glk43499_internal_valid_cpu(setup->owner_cpu) &&
         !glk43499_internal_cpu_is_no_pin(setup->owner_cpu)) ||
        (!glk43499_internal_valid_cpu(setup->waiter_cpu) &&
         !glk43499_internal_cpu_is_no_pin(setup->waiter_cpu)) ||
        !glk43499_internal_valid_cpu(setup->consumer_cpu)) {
        return 0;
    }
    /* nice 域由系统头给出：PRIO_MIN..PRIO_MAX-1（-20..19）。 */
    const int32_t priorities[3] = {
        setup->owner_priority, setup->waiter_priority,
        setup->consumer_priority,
    };
    for (size_t index = 0; index < 3; ++index) {
        if (priorities[index] < PRIO_MIN || priorities[index] >= PRIO_MAX) {
            return 0;
        }
    }
    if (setup->consumer_max == 0 || setup->consumer_burst == 0 ||
        setup->consumer_burst > setup->consumer_max) {
        return 0;
    }
    /* 时间参数：全部非零且 deadline 加法不溢出（不内置 1 小时目标上限）。 */
    const uint64_t timeouts[4] = {
        setup->prepare_timeout_ns, setup->parked_timeout_ns,
        setup->execution_timeout_ns, setup->shutdown_timeout_ns,
    };
    const uint64_t now_ns = glk43499_internal_now_ns();
    for (size_t index = 0; index < 4; ++index) {
        if (timeouts[index] == 0 ||
            timeouts[index] > UINT64_MAX - now_ns) {
            return 0;
        }
    }
    /* 轮询/自旋/就绪参数必须落在请求的等待窗口内（跨字段，替代固定阈值）。 */
    uint64_t poll_ns = 0;
    if (!glk43499_internal_mul_u64(setup->poll_us, UINT64_C(1000),
                                   &poll_ns) ||
        poll_ns > setup->parked_timeout_ns ||
        poll_ns > setup->shutdown_timeout_ns) {
        return 0;
    }
    uint64_t settle_ns = 0;
    if (!glk43499_internal_mul_u64(setup->setup_settle_us, UINT64_C(1000),
                                   &settle_ns) ||
        settle_ns > setup->prepare_timeout_ns) {
        return 0;
    }
    if (setup->lock_pi_spin_ns == 0 ||
        setup->lock_pi_spin_ns > setup->parked_timeout_ns) {
        return 0;
    }
    /* CMP_REQUEUE_PI val3 显式收窄：i64 域 0..INT32_MAX。 */
    if (setup->requeue_expected < 0 ||
        setup->requeue_expected > (int64_t) INT32_MAX) {
        return 0;
    }
    /* disarm 自旋次数：非零且不超过 u32 计数域（循环变量为 u64，不溢出）。 */
    if (setup->disarm_spin_limit == 0 ||
        setup->disarm_spin_limit > UINT32_MAX) {
        return 0;
    }
    return 1;
}

/**
 * @brief 校验 PrimitiveSpec 的 ABI 与选中 branch 以外的 union 尾零。
 * @param primitive 调用期借用；可为 NULL。
 * @return 1 接受；0 拒绝（BAD_ABI）。
 * @note 未选 union 分支覆盖的尾字节必须保持全零（非 reserved 槽）。
 */
static int32_t glk43499_actor_valid_primitive(
    const struct glk43499_primitive_spec *primitive) {
    if (primitive == NULL ||
        !glk43499_internal_valid_header(&primitive->abi,
                                        (uint32_t) sizeof(*primitive))) {
        return 0;
    }
    size_t selected_size = 0;
    switch (primitive->kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG:
            selected_size = sizeof(primitive->value.multicast_sendmsg);
            break;
        case GLK43499_PRIMITIVE_PSELECT:
            selected_size = sizeof(primitive->value.pselect);
            break;
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE:
            selected_size = sizeof(primitive->value.tcp_zerocopy_receive);
            break;
        default:
            return 0;
    }
    /* ABI v4 无 reserved 槽；未选 union 分支覆盖的尾字节必须保持全零。 */
    const uint8_t *union_bytes = (const uint8_t *) &primitive->value;
    for (size_t index = selected_size; index < sizeof(primitive->value);
         ++index) {
        if (union_bytes[index] != 0) return 0;
    }
    return 1;
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
    atomic_init(&actor->shared.waiter_in_futex, 0);
    atomic_init(&actor->shared.waiter_wait_ret, 0);
    atomic_init(&actor->shared.waiter_wait_errno, 0);
    atomic_init(&actor->shared.waiter_dispatched, 0);
    atomic_init(&actor->shared.target_value_at_requeue, 0);
    atomic_init(&actor->shared.waiter_futex_enter_ns, 0);
    atomic_init(&actor->shared.waiter_futex_exit_ns, 0);
    atomic_init(&actor->shared.settle_done_ns, 0);
    atomic_init(&actor->shared.requeue_call_ns, 0);
    atomic_init(&actor->shared.owner_tid, 0);
    atomic_init(&actor->shared.carrier_started, 0);
    atomic_init(&actor->shared.carrier_frames_sent, 0);
    atomic_init(&actor->shared.carrier_step, 0);
    atomic_init(&actor->shared.carrier_last_errno, 0);
    atomic_init(&actor->shared.route_enter_ns, 0);
    atomic_init(&actor->shared.route_exit_ns, 0);
    atomic_init(&actor->shared.waiter_parked_timeout_ns, 0);
    atomic_init(&actor->shared.waiter_op, 0);
    atomic_init(&actor->shared.waiter_val, 0);
    atomic_init(&actor->shared.waiter_val3, 0);
    atomic_init(&actor->shared.waiter_uaddr2_tid, 0);
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
    atomic_init(&actor->shared.ghost_disarm_result, 0);
    atomic_init(&actor->shared.ghost_disarm_errno, 0);
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
    actor->consumer_max = setup->consumer_max;
    actor->consumer_burst = setup->consumer_burst;
    actor->lock_pi_spin_ns = setup->lock_pi_spin_ns;
    actor->poll_us = setup->poll_us;
    actor->setup_settle_us = setup->setup_settle_us;
    actor->requeue_expected = setup->requeue_expected;
    actor->disarm_spin_limit = setup->disarm_spin_limit;
    actor->thread_count = setup->thread_count;
    actor->primitive_kind = primitive->kind;
    actor->state = GLK43499_ACTOR_PREPARED;
    actor->carrier.kind = GLK43499_PRIMITIVE_INVALID;

    /* R3b：Page 的 landing.fill_byte 不参与 Carrier 填充（tcp 双 fill 在 spec）。 */
    int32_t system_error = 0;
    const int32_t prepare_status =
        glk43499_actor_carrier_prepare(actor, primitive, &system_error);
    if (prepare_status != GLK43499_CALL_OK) {
        /* R3a-2/P11：Carrier prepare 可能已建立部分资源且清理不干净（retained）。
         * 先尝试 release：只有真实干净释放才 free(actor)；否则保留对象到进程退出
         * 并在回执报 dirty/retained —— 禁止把保留态随 free(actor) 静默丢弃。 */
        const int32_t released = glk43499_actor_carrier_release(actor);
        if (released != 0) {
            free(actor);
            glk43499_internal_set_call(&result->call, prepare_status,
                                       GLK43499_EFFECT_NOT_APPLIED,
                                       GLK43499_RESOURCE_INVALID, system_error,
                                       0);
            return GLK43499_INVALID_HANDLE;
        }
        result->receipt.page_consumed = 0; /* 提交点在 Carrier/三线程就绪之后 */
        result->receipt.carrier_prepared = 0;
        glk43499_internal_set_call(&result->call, prepare_status,
                                   GLK43499_EFFECT_DIRTY,
                                   GLK43499_RESOURCE_QUARANTINED, system_error,
                                   1);
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
    /* B11：pthread_create 返回错误码（bionic 不保证同步写 errno），
     * 错误码必须取返回值；不得读 errno。 */
    int32_t create_status = pthread_create(
        &actor->consumer_thread, NULL, glk43499_actor_consumer_run, actor);
    if (create_status != 0) {
        create_error = create_status;
    } else {
        actor->consumer_started = 1;
        create_status = pthread_create(&actor->owner_thread, NULL,
                                       glk43499_actor_owner_run, actor);
        if (create_status != 0) {
            create_error = create_status;
        } else {
            actor->owner_started = 1;
            create_status = pthread_create(&actor->waiter_thread, NULL,
                                           waiter_entry, actor);
            if (create_status != 0) {
                create_error = create_status;
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
            (void) usleep((useconds_t) actor->poll_us);
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
    result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_UNKNOWN;
    result->diag_requeue_ret = 0;
    result->diag_requeue_errno = 0;
    result->diag_fail_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_UNKNOWN;
    result->diag_publish_count = 0;
    result->diag_requeue_calls = 0;
    result->diag_version = UINT32_C(0x2026102805);
    result->diag_waiter_waiting = 0;
    result->diag_owner_started = 0;
    result->diag_requeue_expected = 0;
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

    /* task-104：阶段诊断（PI 窗口内仅写回执字段，不 IO/不日志）。 */
    result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_SETTLE;
    /* TODO(task-131): F1 旧顺序（settle 先于 ready 等待）；本批完成后删除。
    (void) usleep((useconds_t) impl->setup_settle_us);
    result->diag_ts_settle_done_ns = glk43499_actor_now_ns();
    atomic_store(&impl->shared.settle_done_ns, result->diag_ts_settle_done_ns);
    */
    /* task-108：对齐 legacy2 race.cpp:374-375 的有界预等（不改变失败/超时语义：
     * 到时未 ready 仍按原路径继续，仅记录 evidence）。 */
    const uint64_t ready_deadline =
        glk43499_internal_deadline(impl->execution_timeout_ns);
    while ((atomic_load(&impl->shared.waiter_waiting) == 0 ||
            atomic_load(&impl->shared.owner_started) == 0) &&
           !glk43499_internal_expired(ready_deadline)) {
        (void) usleep((useconds_t) impl->poll_us);
    }
    /* task-131 F1：ready 之后再 settle，逐行对齐 legacy2 race.cpp:374-378
     * （先等 waiter_waiting∩owner_started，再 usleep(setup_settle_us)）。 */
    (void) usleep((useconds_t) impl->setup_settle_us);
    result->diag_ts_settle_done_ns = glk43499_actor_now_ns();
    atomic_store(&impl->shared.settle_done_ns, result->diag_ts_settle_done_ns);
    /* 打开窗口：先发布 execute_requested，再 requeue。 */
    result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_PUBLISH;
    atomic_store(&impl->shared.execute_requested, 1);
    ++result->diag_publish_count;
    errno = 0;
    result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_REQUEUE;
    ++result->diag_requeue_calls;
    result->diag_waiter_waiting =
        (uint32_t) atomic_load(&impl->shared.waiter_waiting);
    result->diag_owner_started =
        (uint32_t) atomic_load(&impl->shared.owner_started);
    result->diag_requeue_expected = impl->requeue_expected;
    result->diag_target_value_at_requeue =
        (uint32_t) __atomic_load_n(&impl->shared.target_futex, __ATOMIC_ACQUIRE);
    result->diag_ts_requeue_ns = glk43499_actor_now_ns();
    atomic_store(&impl->shared.requeue_call_ns, result->diag_ts_requeue_ns);
    result->diag_waiter_in_futex =
        (uint32_t) atomic_load(&impl->shared.waiter_in_futex);
    result->diag_waiter_wait_ret =
        (int32_t) atomic_load(&impl->shared.waiter_wait_ret);
    result->diag_waiter_wait_errno =
        (int32_t) atomic_load(&impl->shared.waiter_wait_errno);
    result->diag_waiter_dispatched =
        (uint32_t) atomic_load(&impl->shared.waiter_dispatched);
    result->diag_ts_waiter_enter_ns =
        (uint64_t) atomic_load(&impl->shared.waiter_futex_enter_ns);
    result->diag_ts_waiter_exit_ns =
        (uint64_t) atomic_load(&impl->shared.waiter_futex_exit_ns);
    result->diag_parked_timeout_ns =
        (uint64_t) atomic_load(&impl->shared.waiter_parked_timeout_ns);
    result->diag_setup_settle_us = (uint64_t) impl->setup_settle_us;
    result->diag_poll_us = (uint64_t) impl->poll_us;
    result->diag_prepare_timeout_ns = impl->prepare_timeout_ns;
    result->diag_ts_sec = 0;
    result->diag_ts_nsec = 0;
    result->diag_futex_op = (uint32_t) atomic_load(&impl->shared.waiter_op);
    result->diag_futex_val = (uint32_t) atomic_load(&impl->shared.waiter_val);
    result->diag_futex_uaddr2 = (uint32_t) atomic_load(&impl->shared.waiter_uaddr2_tid);
    result->diag_futex_val3 = (uint32_t) atomic_load(&impl->shared.waiter_val3);
    result->diag_owner_tid = (int32_t) atomic_load(&impl->shared.owner_tid);
    const long requeue_ret = glk43499_actor_futex(
        &impl->shared.wait_futex, FUTEX_CMP_REQUEUE_PI, 1,
        (const struct timespec *) (uintptr_t) 1,
        &impl->shared.target_futex, (uint32_t) impl->requeue_expected);
    result->diag_requeue_ret = (int32_t) requeue_ret;
    result->diag_requeue_errno = requeue_ret < 0 ? errno : 0;
    result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_WAIT_WINDOW;

    const uint64_t deadline =
        glk43499_internal_deadline(impl->execution_timeout_ns);
    int32_t window_done = 0;
    for (;;) {
        if (atomic_load(&impl->shared.window_done) != 0) {
            result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_WINDOW_DONE;
            window_done = 1;
            break;
        }
        if (glk43499_internal_expired(deadline)) break;
        (void) usleep((useconds_t) impl->poll_us);
    }

    if (!window_done) {
        /* task-107：timeout 分支无条件记录失败阶段（不被 cleanup 覆写）。 */
        result->diag_fail_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_WAIT_WINDOW;
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
    result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_DRAIN;
    int32_t drained = glk43499_actor_drain_consumer(impl,
                                                    impl->execution_timeout_ns);
    if (drained == 0) {
        if (result->diag_fail_stage == (uint32_t) GLK43499_ACTOR_DIAG_STAGE_UNKNOWN) {
            result->diag_fail_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_DRAIN;
        }
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
    result->diag_stage = (uint32_t) GLK43499_ACTOR_DIAG_STAGE_DISARM;
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
    /* R3a-2：Carrier 中性物理结果与失败定位（last_step/last_errno）进回执；
     * 与 calls/successes 一起构成可证伪诊断，不参与成功判据（成功判据在下方）。 */
    result->receipt.carrier_operations = impl->carrier_outcome.operations;
    result->receipt.carrier_error = impl->carrier_outcome.failed;
    result->receipt.carrier_last_step = impl->carrier_outcome.last_step;
    result->receipt.carrier_last_errno = impl->carrier_outcome.last_errno;
    result->receipt.carrier_punch_failed =
        impl->carrier.kind == GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE
            ? (int32_t) atomic_load(
                  &impl->carrier.value.tcp_zerocopy_receive.punch_failed)
            : 0;
    /* R3a-2/B7：multicast ghost disarm 触发结果（未要求时保持 0/NONE）。 */
    result->receipt.ghost_disarm_result =
        (int32_t) atomic_load(&impl->shared.ghost_disarm_result);
    result->receipt.ghost_disarm_errno =
        (int32_t) atomic_load(&impl->shared.ghost_disarm_errno);
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

/* ========================================================================== */
/* task-111：FUTEX_WAIT_REQUEUE_PI 机制自测（debug-only；不写内核）            */
/* ========================================================================== */

struct glk43499_futex_selftest_shared {
    uint32_t wait_word;
    uint32_t target_word;
    atomic_int owner_locked;
    atomic_int waiter_entered;
    int32_t owner_ret;
    int32_t owner_errno;
    int32_t owner_tid;
    int32_t waiter_ret;
    int32_t waiter_errno;
    int64_t waiter_elapsed_ns;
    int32_t requeue_ret;
    int32_t requeue_errno;
    int32_t variant;
};

static void *glk43499_futex_selftest_owner(void *arg) {
    struct glk43499_futex_selftest_shared *shared =
        (struct glk43499_futex_selftest_shared *) arg;
    errno = 0;
    const long locked = glk43499_actor_futex(
        &shared->target_word, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    shared->owner_ret = (int32_t) locked;
    shared->owner_errno = errno;
    shared->owner_tid = (int32_t) gettid();
    atomic_store(&shared->owner_locked, 1);
    (void) usleep(300000);
    (void) glk43499_actor_futex(&shared->target_word, FUTEX_UNLOCK_PI, 0,
                                NULL, NULL, 0);
    return NULL;
}

static void *glk43499_futex_selftest_waiter(void *arg) {
    struct glk43499_futex_selftest_shared *shared =
        (struct glk43499_futex_selftest_shared *) arg;
    struct timespec started;
    struct timespec finished;
    struct timespec timeout;
    (void) clock_gettime(CLOCK_MONOTONIC, &started);
    /* 逐行复刻 legacy2/core/race.cpp:171-189：绝对 CLOCK_MONOTONIC deadline。 */
    (void) clock_gettime(CLOCK_MONOTONIC, &timeout);
    timeout.tv_sec += 1;
    timeout.tv_nsec += 0;
    if (timeout.tv_nsec >= 1000000000L) {
        timeout.tv_sec++;
        timeout.tv_nsec -= 1000000000L;
    }
    const int32_t op = FUTEX_WAIT_REQUEUE_PI;
    atomic_store(&shared->waiter_entered, 1);
    errno = 0;
    const long waited = glk43499_actor_futex(
        &shared->wait_word, op, 0, &timeout, &shared->target_word, 0);
    shared->waiter_ret = (int32_t) waited;
    shared->waiter_errno = errno;
    (void) clock_gettime(CLOCK_MONOTONIC, &finished);
    shared->waiter_elapsed_ns =
        (int64_t) ((finished.tv_sec - started.tv_sec) * INT64_C(1000000000) +
                   (finished.tv_nsec - started.tv_nsec));
    return NULL;
}

int32_t glk43499_actor_futex_selftest(int32_t variant, int32_t *out) {
    if (out == NULL || variant < 0 || variant > 2) return -1;
    struct glk43499_futex_selftest_shared shared;
    (void) memset(&shared, 0, sizeof(shared));
    shared.variant = variant;
    atomic_init(&shared.owner_locked, 0);
    atomic_init(&shared.waiter_entered, 0);
    pthread_t owner_thread;
    pthread_t waiter_thread;
    if (pthread_create(&owner_thread, NULL, glk43499_futex_selftest_owner,
                       &shared) != 0) {
        return -1;
    }
    while (atomic_load(&shared.owner_locked) == 0) (void) usleep(100);
    if (pthread_create(&waiter_thread, NULL, glk43499_futex_selftest_waiter,
                       &shared) != 0) {
        (void) glk43499_actor_futex(&shared.target_word, FUTEX_UNLOCK_PI, 0,
                                    NULL, NULL, 0);
        (void) pthread_join(owner_thread, NULL);
        return -1;
    }
    while (atomic_load(&shared.waiter_entered) == 0) (void) usleep(100);
    (void) usleep(50000);
    errno = 0;
    /* 复刻 legacy2/core/race.cpp:380：val=1、timeout=(void*)1、val3=0。 */
    const int32_t val3 = 0;
    const long requeued = glk43499_actor_futex(
        &shared.wait_word, FUTEX_CMP_REQUEUE_PI, 1,
        (const struct timespec *) (uintptr_t) 1, &shared.target_word, val3);
    shared.requeue_ret = (int32_t) requeued;
    shared.requeue_errno = errno;
    (void) pthread_join(waiter_thread, NULL);
    (void) pthread_join(owner_thread, NULL);
    out[0] = shared.variant;
    out[1] = shared.owner_ret;
    out[2] = shared.owner_errno;
    out[3] = shared.owner_tid;
    out[4] = shared.waiter_ret;
    out[5] = shared.waiter_errno;
    out[6] = (int32_t) (shared.waiter_elapsed_ns / INT64_C(1000));
    out[7] = shared.requeue_ret;
    out[8] = shared.requeue_errno;
    out[9] = shared.waiter_ret == 0 ? 1 : 0;
    return 0;
}

/** @brief task-113：carrier 崩溃前快照（只读 atomic；不 IO/不日志）。 */
int32_t glk43499_actor_debug_snapshot(glk43499_handle actor, int64_t *out) {
    if (actor == GLK43499_INVALID_HANDLE || out == NULL) return -1;
    struct glk43499_actor_impl *impl =
        (struct glk43499_actor_impl *) glk43499_internal_handle_ptr(actor);
    if (impl == NULL) return -1;
    out[0] = (int64_t) impl->state;
    out[1] = (int64_t) atomic_load(&impl->shared.waiter_waiting);
    out[2] = (int64_t) atomic_load(&impl->shared.window_opened);
    out[3] = (int64_t) atomic_load(&impl->shared.window_done);
    out[4] = (int64_t) atomic_load(&impl->shared.carrier_started);
    out[5] = (int64_t) atomic_load(&impl->shared.carrier_frames_sent);
    out[6] = (int64_t) atomic_load(&impl->shared.carrier_step);
    out[7] = (int64_t) atomic_load(&impl->shared.carrier_last_errno);
    out[8] = (int64_t) atomic_load(&impl->shared.route_enter_ns);
    out[9] = (int64_t) atomic_load(&impl->shared.route_exit_ns);
    return 0;
}
