/**
 * @file page_internal.h
 * @brief Page 对象的私有布局与 cross-TU 助手（Page ↔ Actor 所有权交接）。
 *
 * @note 本头只服务 `page.c` 与 `actor.c`；不是公共 ABI。
 * @note 所有权：`struct glk43499_page_impl` 是 Page token 指向的唯一对象，
 *       独占 landing 提交时建立的 reclaim socketpair 闭包（Page 的全部物理资源）。
 *       `landPage` 成功消费 Placement 并产生一个 Page；`closePage` 只终结尚未
 *       转移给 Actor 的 Page；`prepareActor` 成功后 Page 归 Actor，由 Actor 释放。
 * @note 阶段：G4L-2/G4L-3。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PAGE_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_PAGE_INTERNAL_H

#include "core_internal.h"
#include "kernel_internal.h"
#include "page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 一个已落页 Page：候选页基址事实 + reclaim 闭包。 */
struct glk43499_page_impl {
    int32_t reclaim_read;
    int32_t reclaim_write;
    uint64_t base;
    uint64_t page_size;
    uint64_t allocation_size;
    uint64_t required_alignment;
    uint64_t content_generation;
    /* 0=LANDED；1=CLOSING（close 已接受）；2=CONSUMED（已转移给 Actor）。 */
    int32_t state;
    /* 1 表示 reclaim 闭包无法证明完整释放：对象保留到进程退出（不 free、不 close）。 */
    int32_t retained;
};

/**
 * @brief 把 Page token 还原为私有对象。
 * @param handle 非零 token；调用方已确认非零。
 * @return 对象指针；0 返回 NULL。
 * @note 不校验存活性：线性 owner 契约把 raw 误用责任交给调用方。
 */
static inline struct glk43499_page_impl *glk43499_page_from_handle(
    glk43499_handle handle) {
    return (struct glk43499_page_impl *) glk43499_internal_handle_ptr(handle);
}

/**
 * @brief 释放 Page 的 reclaim 闭包并释放对象。
 * @param page 非空对象；已由 Actor 唯一拥有。
 * @return 1 完整释放（对象已 free）；0 表示无法证明完整释放（对象 retained，不 free）。
 * @note Actor 终结顺序要求：先 close Carrier，再调用本函数释放 Page。
 * @note 幂等保护：state==2 时不重复关闭；正常路径不会重复调用。
 * @note 兼容 Actor（actor.c 不在本批写范围）：提供 void 入口
 *       [glk43499_page_release]，失败时同样保留对象身份、不 free。
 */
int32_t glk43499_page_release_checked(struct glk43499_page_impl *page);

/**
 * @brief 释放 Page 的 reclaim 闭包（Actor 终结用的 void 入口）。
 * @param page 非空对象；已由 Actor 唯一拥有。
 * @note 与 [glk43499_page_release_checked] 同形；失败时对象保留身份（不 free、不 close）。
 * @note 本入口保留给只需要「尽力释放」的调用方；Actor 终结改用
 *       [glk43499_page_release_checked] 并把失败抬升为 retained（task-17）。
 */
void glk43499_page_release(struct glk43499_page_impl *page);

/**
 * @brief 读取 Page 的 content_generation 事实。
 * @param page 非空对象。
 * @return landing 时 Kotlin 给出的 content_generation。
 */
static inline uint64_t glk43499_page_content_generation(
    const struct glk43499_page_impl *page) {
    return page->content_generation;
}

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PAGE_INTERNAL_H */
