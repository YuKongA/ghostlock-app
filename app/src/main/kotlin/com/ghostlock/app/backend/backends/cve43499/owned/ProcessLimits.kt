package com.ghostlock.app.backend.backends.cve43499.owned

import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499LimitsResult

/**
 * RLIMIT 抬升结局（G06；只表达机械调用发生了什么）。
 *
 * @note 调用方在 W1 前 fail-closed 拒绝；成功时把 before/after 写进 Flow evidence。
 * @note 阶段：P1a parity（task-63）。
 */
internal sealed interface ProcessLimitsOutcome {
    /**
     * soft 已抬到 hard（两资源均成功）。
     *
     * @property nofileSoftBefore/After NOFILE soft 前/后值。
     * @property nofileHard NOFILE hard 值（未改）。
     * @property nprocSoftBefore/After NPROC soft 前/后值。
     * @property nprocHard NPROC hard 值（未改）。
     */
    data class Raised(
        val nofileSoftBefore: Long,
        val nofileHard: Long,
        val nofileSoftAfter: Long,
        val nprocSoftBefore: Long,
        val nprocHard: Long,
        val nprocSoftAfter: Long,
    ) : ProcessLimitsOutcome

    /** 具名失败（`process-limits:*`；调用方 fail-closed）。 */
    data class Failed(val reason: String) : ProcessLimitsOutcome
}

/**
 * RLIMIT 机制 seam（生产=JNI；宿主单测注入 fake）。
 *
 * @note 返回 `Native43499Jni` int 协议值（0/-2/INT32_MIN/负 errno）；实现不得抛异常。
 */
internal fun interface ProcessLimitsNative {
    /**
     * @param result 机制回执（before/after/errno）。
     * @return JNI 入口状态值。
     */
    fun raise(result: Native43499LimitsResult): Int
}

/** 生产实现：`Native43499Jni.raiseProcessLimits`（libghostlock43499 已加载）。 */
internal object JniProcessLimitsNative : ProcessLimitsNative {
    override fun raise(result: Native43499LimitsResult): Int =
        Native43499Jni.raiseProcessLimits(result)
}

/**
 * G06：把本进程 NOFILE/NPROC 的 soft limit 抬到 hard（legacy `set_limit`）。
 *
 * 语义（legacy/kernelsnitch/utils.h:158-168）：`getrlimit→rlim_cur=rlim_max→
 * setrlimit`，NOFILE 与 NPROC 都做；不修改 hard。失败即 fail-closed（具名 reason），
 * 不吞错、不继续 W1。
 *
 * @param native 机制 seam（生产=JNI；宿主测试注入 fake）。
 * @note 本类不缓存结果；每次 [raise] 都做一次 getrlimit/setrlimit/回读。
 * @note 阶段：P1a parity（task-63）；真机行为 UNVERIFIED（设备离线）。
 */
internal class ProcessLimits(
    private val native: ProcessLimitsNative = JniProcessLimitsNative,
) {
    /**
     * 执行一次 RLIMIT 抬升。
     *
     * @return [ProcessLimitsOutcome.Raised] 或具名 [ProcessLimitsOutcome.Failed]。
     */
    fun raise(): ProcessLimitsOutcome {
        val result = Native43499LimitsResult()
        val status = try {
            native.raise(result)
        } catch (problem: Throwable) {
            return ProcessLimitsOutcome.Failed(
                "process-limits:jni:" + (problem.javaClass.simpleName),
            )
        }
        return when {
            status == Native43499Jni.RESULT_NOT_ACCEPTED_INT ->
                ProcessLimitsOutcome.Failed("process-limits:not-accepted")
            status == Native43499Jni.RESULT_INCOMPLETE_INT ->
                ProcessLimitsOutcome.Failed("process-limits:incomplete")
            result.nofileStatus != 0 ->
                ProcessLimitsOutcome.Failed("process-limits:nofile:" + result.nofileStatus)
            result.nprocStatus != 0 ->
                ProcessLimitsOutcome.Failed("process-limits:nproc:" + result.nprocStatus)
            status != 0 ->
                ProcessLimitsOutcome.Failed("process-limits:status:" + status)
            /* soft 必须已抬到 hard（回读值可证伪；防止 setrlimit 静默 no-op）。 */
            result.nofileSoftAfter < result.nofileHard ->
                ProcessLimitsOutcome.Failed("process-limits:nofile-not-raised")
            result.nprocSoftAfter < result.nprocHard ->
                ProcessLimitsOutcome.Failed("process-limits:nproc-not-raised")
            else -> ProcessLimitsOutcome.Raised(
                nofileSoftBefore = result.nofileSoftBefore,
                nofileHard = result.nofileHard,
                nofileSoftAfter = result.nofileSoftAfter,
                nprocSoftBefore = result.nprocSoftBefore,
                nprocHard = result.nprocHard,
                nprocSoftAfter = result.nprocSoftAfter,
            )
        }
    }

    companion object {
        /**
         * @param raised 成功结局。 @return Flow evidence（稳定 key）。
         */
        fun evidence(raised: ProcessLimitsOutcome.Raised): Map<String, String> = linkedMapOf(
            "processLimits" to "raised",
            "limitsNofileBefore" to raised.nofileSoftBefore.toString(),
            "limitsNofileHard" to raised.nofileHard.toString(),
            "limitsNofileAfter" to raised.nofileSoftAfter.toString(),
            "limitsNprocBefore" to raised.nprocSoftBefore.toString(),
            "limitsNprocHard" to raised.nprocHard.toString(),
            "limitsNprocAfter" to raised.nprocSoftAfter.toString(),
        )
    }
}
