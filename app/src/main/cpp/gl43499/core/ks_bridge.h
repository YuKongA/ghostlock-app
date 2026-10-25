/**
 * @file ks_bridge.h
 * @brief 43499 侧访问 KernelSnitch 的唯一入口（实现见 `ks_bridge.c`）。
 *
 * KernelSnitch 的算法与线程模型只保留在 `glgeneral/kernelsnitch/` 一份；本头
 * 只暴露 opaque 句柄，避免 43499 的 C++ TU 直接 include 那份 C 头
 * （`utils.h` 的 `SYSCHK` 展开出 GNU `typeof`，C++ 编译不了）。
 *
 * @note 阶段：P-A-3 过渡物。`CMakeLists.txt` 的「backend 不得带第二份」在这里
 *       被临时放宽（同一次构建编入同一份源码）；偿还 = general 导出同一组 C API
 *       后删除本桥与 `ks_bridge.c`。
 * @note 句柄只在同一进程内有效；`fork` 之后的子进程可以用（mmap 共享）。
 */
#ifndef GHOSTLOCK_CORE_KS_BRIDGE_H
#define GHOSTLOCK_CORE_KS_BRIDGE_H

#include <cstddef>
#include <cstdint>

extern "C" {
/**
 * @brief 创建一次 KernelSnitch 会话状态。
 *
 * @param mm_struct_sz  sizeof(mm_struct)，爆破步长。
 * @param mm_slab_order mm_struct slab order。
 * @param thread_cnt    爆破线程数（必须 > 0）。
 * @param collision_cnt 需要的哈希碰撞数（>= 2）。
 * @param verbose       是否输出扫描细节（0 = 安静）。
 * @param pin_cpu       绑定的 CPU；负数表示不绑定。
 * @return 不透明句柄；参数非法或初始化失败返回 nullptr。
 */
void *glk_ks_dev_create(std::size_t mm_struct_sz, std::size_t mm_slab_order,
                        std::size_t thread_cnt, std::size_t collision_cnt,
                        std::size_t verbose, std::int32_t pin_cpu);

/**
 * @brief 执行碰撞扫描（阻塞）。建议在 `fork` 出来的 leak child 里调用。
 *
 * @param ks `glk_ks_dev_create` 的返回值。
 */
void glk_ks_dev_find_collisions(void *ks);

/**
 * @brief 是否已找齐所需碰撞。
 *
 * @param ks 句柄。
 * @return 1 找齐，0 未找齐或句柄无效。
 */
std::int32_t glk_ks_dev_found_collisions(void *ks);

/**
 * @brief 爆破 mm_struct 地址（阻塞）。
 *
 * @param ks 句柄；必须已经 `find_collisions` 且找齐。
 * @return 泄漏到的地址；未找到或前提不满足返回 `(uintptr_t)-1`。
 */
std::uintptr_t glk_ks_dev_leak(void *ks);

/** @brief 扫描进度（已处理条目数），仅用于日志节拍；句柄无效返回 0。 */
std::size_t glk_ks_dev_scan_done(void *ks);

/** @brief 扫描总量（条目数），仅用于日志节拍；句柄无效返回 0。 */
std::size_t glk_ks_dev_scan_total(void *ks);

/** @brief KernelSnitch 的 futex 扫描区间大小（日志用；与算法同源）。 */
std::size_t glk_ks_dev_futex_sz(void);

/**
 * @brief 释放状态并返回本次找到的地址。
 *
 * @param ks 句柄；可为 nullptr（返回 `(uintptr_t)-1`）。
 * @return 找到的 mm_struct 或 `(uintptr_t)-1`。
 * @note 必须恰好调用一次；之后句柄失效。
 */
std::uintptr_t glk_ks_dev_destroy(void *ks);
}

#endif // GHOSTLOCK_CORE_KS_BRIDGE_H
