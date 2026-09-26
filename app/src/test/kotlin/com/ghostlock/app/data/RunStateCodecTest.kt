package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RunStateCodecTest {
    @Test
    fun `initial state marks every step not started`() {
        val text = RunStateCodec.initial(1L)
        for (step in RunStateCodec.Steps) {
            assertTrue("$step missing from $text", text.contains("\"$step\": \"not_start\""))
        }
        assertNull(RunStateCodec.parseStuckStep(text))
    }

    @Test
    fun `first in progress step wins`() {
        val steps = RunStateCodec.Steps.associateWith { RunStateCodec.Completed }.toMutableMap()
        steps["w2a"] = RunStateCodec.InProgress
        steps["w3a"] = RunStateCodec.InProgress
        assertEquals("w2a", RunStateCodec.parseStuckStep(RunStateCodec.encode(steps, 2L)))
    }

    @Test
    fun `w3 in progress round trips`() {
        val steps = RunStateCodec.Steps.associateWith { RunStateCodec.Completed }.toMutableMap()
        steps["w3b"] = RunStateCodec.InProgress
        assertEquals("w3b", RunStateCodec.parseStuckStep(RunStateCodec.encode(steps, 3L)))
    }

    @Test
    fun `completed document has no stuck step`() {
        val steps = RunStateCodec.Steps.associateWith { RunStateCodec.Completed }
        assertNull(RunStateCodec.parseStuckStep(RunStateCodec.encode(steps, 4L)))
    }

    @Test
    fun `missing or invalid documents yield null`() {
        assertNull(RunStateCodec.parseStuckStep(""))
        assertNull(RunStateCodec.parseStuckStep("not a config"))
        assertNull(RunStateCodec.parseStuckStep("{\"steps\": {}}"))
    }
}
