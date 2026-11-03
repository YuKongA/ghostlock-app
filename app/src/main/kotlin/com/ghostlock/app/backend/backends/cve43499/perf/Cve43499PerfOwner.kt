package com.ghostlock.app.backend.backends.cve43499.perf

import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499PerfCollectResult

/**
 * perf token 的 Kotlin 唯一 owner（H3-a 的正确性证据）。
 *
 * Native 是无状态句柄：不检查 OPEN/COLLECTED/CLOSED，也不阻止重复 collect 或
 * 重复 close。本类在 Kotlin 侧持有 token 并强制 owner 契约：
 * - [close] 恰好一次；第二次 close 不再触 JNI；
 * - close 后 token 清零，[collect] 在触 JNI 前即被拒绝；
 * - [collect] 允许重复调用（机械重读；语义由调用方的 pass 循环决定）。
 *
 * @param native Native seam。
 * @param handle 已由 `openPerfSampler` 返回的非零 token。
 * @note 阶段：R3e-2；不注册 Cleaner、无全局状态。
 */
internal class Cve43499PerfOwner private constructor(
    private val native: Cve43499PerfNative,
    handle: Long,
) : AutoCloseable {
    private var token: Long = handle

    /** @return 当前仍持有的 token；close 后为 0（contract test 可见）。 */
    internal fun tokenForTest(): Long = token

    /**
     * 停止采样并整块拷贝环数据（token 不消费）。
     *
     * @param out 输出缓冲；容量必须 >= dataSize。
     * @param result 环事实回执。
     * @return call_status 或 JNI 三态哨兵。
     * @throws IllegalStateException 已 close（owner 契约：不得再触 JNI）。
     */
    fun collect(out: ByteArray, result: Native43499PerfCollectResult): Int {
        check(token != 0L) { "perf owner: collect after close" }
        return native.collect(token, out, result)
    }

    /**
     * 终结采样会话并消费 token（恰好一次）。
     *
     * @param result 终结回执；由调用方提供以读取 retained 事实。
     * @return call_status 或 JNI 三态哨兵；重复 close 返回 [OWNER_ALREADY_CLOSED]。
     */
    fun close(result: Native43499CloseResult): Int {
        if (token == 0L) return OWNER_ALREADY_CLOSED
        val status = native.close(token, result)
        /* B8：只有 Native 已接受（非 NOT_ACCEPTED）才视为消费 token；-2 保留 token。 */
        if (status != Native43499Jni.RESULT_NOT_ACCEPTED_INT) token = 0L
        return status
    }

    /** 便捷 close（丢弃回执；生产路径用带回执重载，便于记录 retained）。 */
    override fun close() {
        close(Native43499CloseResult())
    }

    internal companion object {
        /** owner 已关闭后的重复 close 返回值（不触 JNI 的本地哨兵）。 */
        const val OWNER_ALREADY_CLOSED: Int = -3

        /**
         * 包装 open 返回的非零 token。
         *
         * @param native Native seam。
         * @param handle `openPerfSampler` 返回的非零 token。
         * @return owner 实例。
         * @throws IllegalArgumentException handle=0（调用方必须先判失败）。
         */
        fun wrap(native: Cve43499PerfNative, handle: Long): Cve43499PerfOwner {
            require(handle != 0L) { "perf owner: invalid handle" }
            return Cve43499PerfOwner(native, handle)
        }
    }
}
