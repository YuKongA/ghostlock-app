package com.ghostlock.app.backend.backends.cve43499.owned

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * G01 `FailStopController` 单测（task-64）。
 *
 * 覆盖：evidence/logger 先于 terminator；只触发一次；disabled=只记录不终止；
 * reason 冻结格式；evidence 字段名。
 *
 * @note 全部 fake，不真杀 JVM；生产默认 terminator 只做静态存在性断言。
 */
class FailStopControllerTest {

    @Test
    fun `evidence and logger are flushed before terminate and only once`() {
        val events = mutableListOf<String>()
        var evidence: Map<String, String> = emptyMap()
        var stopReason: String? = null
        val controller = FailStopController(
            enabled = true,
            terminator = { reason ->
                events += "terminate"
                stopReason = reason
            },
            evidenceSink = { map ->
                events += "evidence"
                evidence = map
            },
            logger = { events += "log" },
        )
        assertTrue(controller.trigger("write.w1", "cve43499:write.w1:actor-dirty"))
        assertEquals(listOf("evidence", "log", "terminate"), events)
        assertEquals("fail-stop:write.w1:cve43499:write.w1:actor-dirty", stopReason)
        assertEquals("true", evidence["failStopTriggered"])
        assertEquals(
            "fail-stop:write.w1:cve43499:write.w1:actor-dirty",
            evidence["failStopReason"],
        )
        assertEquals("write.w1", evidence["failStopStage"])
        assertEquals("true", evidence["failStopPrecededByRetained"])
        assertTrue(controller.isTriggered())
        /* 第二次/并发重入必须 no-op（只触发一次）。 */
        assertFalse(controller.trigger("write.w2", "second"))
        assertEquals(3, events.size)
    }

    @Test
    fun `disabled controller records nothing and does not terminate`() {
        var terminated = 0
        var sinkCalls = 0
        val controller = FailStopController(
            enabled = false,
            terminator = { terminated++ },
            evidenceSink = { sinkCalls++ },
        )
        assertFalse(controller.trigger("write.w1", "dirty"))
        assertEquals(0, terminated)
        assertEquals(0, sinkCalls)
        assertFalse(controller.isTriggered())
        assertFalse(controller.isEnabled())
    }

    @Test
    fun `controller is enabled by default terminator is not invoked by construction`() {
        /* 只构造 controller（默认生产 terminator 是 lambda，不调用 ⇒ 单测不真杀）。 */
        val controller = FailStopController(enabled = true)
        assertTrue(controller.isEnabled())
        assertFalse(controller.isTriggered())
    }
}
