/**
 * @file cpu_pin_internal.h
 * @brief G08 Actor CPU pin 哨兵（P1b-1；内部 helper，不是 ABI）。
 *
 * 设备/legacy 事实（legacy `threads.cpp:135-138`）：首次真机门禁只 pin consumer；
 * owner/waiter 不 pin。为**不改 ABI**，复用既有 `owner_cpu/waiter_cpu` u32 字段：
 * 值 = **u32 全 1** 表示 **no-pin sentinel**（Kotlin `-1` 经 `get_int` 转 u32 同值）；
 * consumer 字段必须是有效 CPU（禁止哨兵，保持 legacy pin）。
 *
 * @note 纯判定函数、无系统调用；宿主单测 `tools/tests/cpu_pin_test.c`。
 * @note 阶段：P1b-1（task-64）；真机行为 UNVERIFIED（设备离线）。
 */
#ifndef GHOSTLOCK_GL43499_CORE_CPU_PIN_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_CPU_PIN_INTERNAL_H

#include <stdint.h>

/**
 * @brief no-pin 哨兵（u32 全 1；Kotlin -1 的 u32 表示）。
 *
 * @note 与既有字段同宽，不新增/不重排 actor_setup 任何字段 ⇒ ABI 不变。
 */
/* 用取反表达，避免把全 1 写成一个会被 address-space 扫描器误判的地址字面量。 */
#define GLK43499_INTERNAL_NO_PIN_CPU ((uint32_t) ~UINT32_C(0))

/**
 * @brief 判定 CPU 值是否为 no-pin 哨兵。
 *
 * @param cpu 请求里的 CPU 域。
 * @return 1=哨兵（不 pin）；0=普通 CPU。
 */
static inline int32_t glk43499_internal_cpu_is_no_pin(uint32_t cpu) {
    return cpu == GLK43499_INTERNAL_NO_PIN_CPU ? 1 : 0;
}

/**
 * @brief 判定该 worker 是否需要 pin（consumer 恒 pin；owner/waiter 可为哨兵）。
 *
 * @param cpu 请求里的 CPU 域。
 * @return 1=需要 sched_setaffinity；0=跳过（哨兵）。
 */
static inline int32_t glk43499_internal_cpu_pin_required(uint32_t cpu) {
    return cpu == GLK43499_INTERNAL_NO_PIN_CPU ? 0 : 1;
}

#endif /* GHOSTLOCK_GL43499_CORE_CPU_PIN_INTERNAL_H */
