/**
 * @file ks_bridge.c
 * @brief `ks_bridge.h` 的实现：把 general 的 KernelSnitch 包成 opaque 句柄。
 *
 * 只能用 **C** 编译（`glgeneral/kernelsnitch/utils.h` 的 `SYSCHK` 使用 GNU
 * `typeof`，C++ 不认）。构建时需 `-D__ARM -I <repo>/app/src/main/cpp/glgeneral`。
 *
 * @note 算法本体不在此处：`kernelsnitch_setup / find_collisions / bruteforce /
 *       cleanup` 全部来自 `glgeneral/kernelsnitch/kernelsnitch.h`（header-only）。
 *       本文件只做类型擦除与状态查询，不含任何攻击逻辑。
 * @note 阶段：P-A-3 过渡物；general 导出同一组 C API 后本文件删除。
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stddef.h>
#include <stdint.h>

#include "kernelsnitch/kernelsnitch.h"

void *glk_ks_dev_create(size_t mm_struct_sz, size_t mm_slab_order,
                        size_t thread_cnt, size_t collision_cnt, size_t verbose,
                        int32_t pin_cpu) {
    return kernelsnitch_setup(mm_struct_sz, mm_slab_order, thread_cnt,
                              collision_cnt, verbose, (int)pin_cpu);
}

void glk_ks_dev_find_collisions(void *ks) {
    if (!ks) return;
    kernelsnitch_find_collisions((struct kernelsnitch_shared_state *)ks);
}

int32_t glk_ks_dev_found_collisions(void *ks) {
    if (!ks) return 0;
    return kernelsnitch_found_collisions((struct kernelsnitch_shared_state *)ks)
               ? 1
               : 0;
}

uintptr_t glk_ks_dev_leak(void *ks) {
    struct kernelsnitch_shared_state *state =
        (struct kernelsnitch_shared_state *)ks;
    if (!state) return (uintptr_t)-1;
    if (!kernelsnitch_found_collisions(state)) return (uintptr_t)-1;
    kernelsnitch_bruteforce(state);
    return (uintptr_t)state->mm_struct;
}

size_t glk_ks_dev_scan_done(void *ks) {
    if (!ks) return 0;
    return ((struct kernelsnitch_shared_state *)ks)->scan_done;
}

size_t glk_ks_dev_scan_total(void *ks) {
    if (!ks) return 0;
    return ((struct kernelsnitch_shared_state *)ks)->total_futexes;
}

size_t glk_ks_dev_futex_sz(void) { return (size_t)FUTEX_SZ; }

uintptr_t glk_ks_dev_destroy(void *ks) {
    if (!ks) return (uintptr_t)-1;
    /* cleanup 的 ASSERT_pr 在 "碰撞未找齐" 状态下只打一行警告，照常 munmap。 */
    return (uintptr_t)kernelsnitch_cleanup(
        (struct kernelsnitch_shared_state *)ks);
}
