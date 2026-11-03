package com.ghostlock.app.backend.backends.cve43499.perf

import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499PerfCollectResult
import com.ghostlock.app.jni.Native43499PerfOpenRequest
import com.ghostlock.app.jni.Native43499PerfOpenResult

/**
 * perf 采样原语的 Native seam（R3e-2）。
 *
 * 生产实现是 [Cve43499JniPerfNative]（直连 `Native43499Jni` 的 3 个无状态入口）；
 * 宿主单测注入 fake 以覆盖 pass 聚合/失败路径，不触 Native。
 *
 * @note 阶段：R3e-2；Native 侧只做 syscall 机制 + 整块 raw 拷贝，不解析样本。
 */
internal interface Cve43499PerfNative {
    /**
     * 打开一次 perf 采样事件。
     *
     * @param request 15 个显式物理参数。
     * @param result 回执（callStatus 最后写）。
     * @return 新 token；0 或 JNI 三态哨兵（-2 not accepted / -1 incomplete）。
     */
    fun open(
        request: Native43499PerfOpenRequest,
        result: Native43499PerfOpenResult,
    ): Long

    /**
     * 停止采样并把环数据区整块拷贝到 [out]（token 不消费）。
     *
     * @param handle open 返回的 token。
     * @param out 输出缓冲；容量必须 >= dataSize。
     * @param result 环事实回执。
     * @return call_status 或 JNI 三态哨兵。
     */
    fun collect(
        handle: Long,
        out: ByteArray,
        result: Native43499PerfCollectResult,
    ): Int

    /**
     * 终结采样会话并消费 token（无状态句柄；close 唯一性由 owner 保证）。
     *
     * @param handle open 返回的 token。
     * @param result 终结回执。
     * @return call_status 或 JNI 三态哨兵。
     */
    fun close(handle: Long, result: Native43499CloseResult): Int
}

/** 生产 seam：直接转调 [Native43499Jni] 的 perf 三入口（无逻辑）。 */
internal object Cve43499JniPerfNative : Cve43499PerfNative {
    override fun open(
        request: Native43499PerfOpenRequest,
        result: Native43499PerfOpenResult,
    ): Long = Native43499Jni.openPerfSampler(request, result)

    override fun collect(
        handle: Long,
        out: ByteArray,
        result: Native43499PerfCollectResult,
    ): Int = Native43499Jni.collectPerfSamples(handle, out, result)

    override fun close(handle: Long, result: Native43499CloseResult): Int =
        Native43499Jni.closePerfSampler(handle, result)
}
