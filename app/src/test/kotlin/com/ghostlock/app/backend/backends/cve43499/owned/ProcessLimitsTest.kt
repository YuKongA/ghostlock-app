package com.ghostlock.app.backend.backends.cve43499.owned

import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499LimitsResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * G06 `ProcessLimits` 单测（task-63）：成功 before/after、NOT_ACCEPTED/INCOMPLETE、
 * per-limit errno、setrlimit 静默未抬升（回读硬校验）、JNI 抛异常 fail-closed。
 *
 * @note 纯 JVM；Native getrlimit/setrlimit 真行为属设备 UNVERIFIED（设备离线）。
 */
class ProcessLimitsTest {

    @Test
    fun `raised outcome exposes before and after values`() {
        val native = FakeLimitsNative { result ->
            result.callStatus = 0
            result.nofileSoftBefore = 1024
            result.nofileHard = 1_048_576
            result.nofileSoftAfter = 1_048_576
            result.nprocSoftBefore = 512
            result.nprocHard = 1_048_576
            result.nprocSoftAfter = 1_048_576
            0
        }
        val outcome = ProcessLimits(native).raise()
        assertTrue(outcome is ProcessLimitsOutcome.Raised)
        val raised = outcome as ProcessLimitsOutcome.Raised
        assertEquals(1024L, raised.nofileSoftBefore)
        assertEquals(1_048_576L, raised.nofileSoftAfter)
        assertEquals(512L, raised.nprocSoftBefore)
        assertEquals(1_048_576L, raised.nprocSoftAfter)
        assertEquals(
            "raised",
            ProcessLimits.evidence(raised)["processLimits"],
        )
        assertEquals(
            "1048576",
            ProcessLimits.evidence(raised)["limitsNofileAfter"],
        )
    }

    @Test
    fun `not accepted and incomplete map to named reasons`() {
        assertReason(Native43499Jni.RESULT_NOT_ACCEPTED_INT, "process-limits:not-accepted")
        assertReason(Native43499Jni.RESULT_INCOMPLETE_INT, "process-limits:incomplete")
    }

    @Test
    fun `per limit errno and status map to named reasons`() {
        assertEquals(
            "process-limits:nofile:1",
            failedReason { result -> result.nofileStatus = 1; 0 },
        )
        assertEquals(
            "process-limits:nproc:13",
            failedReason { result -> result.nprocStatus = 13; 0 },
        )
        assertEquals(
            "process-limits:status:8",
            failedReason { 8 },
        )
    }

    @Test
    fun `silent setrlimit no-op fails closed on after re-read`() {
        /* soft 未抬到 hard（after < hard）⇒ fail-closed，不得把 no-op 当成功。 */
        assertEquals(
            "process-limits:nofile-not-raised",
            failedReason { result ->
                result.nofileSoftAfter = 1024
                result.nofileHard = 2048
                result.nprocSoftAfter = 2048
                result.nprocHard = 2048
                0
            },
        )
        assertEquals(
            "process-limits:nproc-not-raised",
            failedReason { result ->
                result.nofileSoftAfter = 2048
                result.nofileHard = 2048
                result.nprocSoftAfter = 512
                result.nprocHard = 2048
                0
            },
        )
    }

    @Test
    fun `jni throw is caught and fail closed`() {
        val failing = FakeLimitsNative { throw IllegalStateException("boom") }
        val outcome = ProcessLimits(failing).raise()
        assertTrue(outcome is ProcessLimitsOutcome.Failed)
        assertEquals(
            "process-limits:jni:IllegalStateException",
            (outcome as ProcessLimitsOutcome.Failed).reason,
        )
    }

    /**
     * @param status fake 返回状态。 @param reason 期望失败 reason。
     */
    private fun assertReason(status: Int, reason: String) {
        val native = FakeLimitsNative { result ->
            result.nofileSoftBefore = 1
            result.nofileHard = 2
            result.nofileSoftAfter = 2
            result.nprocSoftBefore = 1
            result.nprocHard = 2
            result.nprocSoftAfter = 2
            status
        }
        val outcome = ProcessLimits(native).raise()
        assertTrue(outcome is ProcessLimitsOutcome.Failed)
        assertEquals(reason, (outcome as ProcessLimitsOutcome.Failed).reason)
    }

    /**
     * @param configure fake 对回执的写回/返回状态。 @return 失败 reason。
     */
    private fun failedReason(
        configure: (Native43499LimitsResult) -> Int,
    ): String {
        val outcome = ProcessLimits(FakeLimitsNative(configure)).raise()
        assertTrue("expected failure but was $outcome", outcome is ProcessLimitsOutcome.Failed)
        return (outcome as ProcessLimitsOutcome.Failed).reason
    }

    /** @param block fake 行为。 */
    private class FakeLimitsNative(
        private val block: (Native43499LimitsResult) -> Int,
    ) : ProcessLimitsNative {
        override fun raise(result: Native43499LimitsResult): Int = block(result)
    }
}
