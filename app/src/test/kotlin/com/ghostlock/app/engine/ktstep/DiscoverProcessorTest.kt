package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DiscoverProcessorTest {
    private val slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80)
    private val processor = DiscoverProcessor(0xC000L, slots)

    private fun result(
        found: Long = 0xFFFFFFC000200000uL.toLong(),
        wordsBase: Long = 0x00007F0000000000L,
        ran: Boolean = true,
        verdict: Int = 1,
    ) = NativeDiscoverResult.Ok(
        ran = ran,
        verdict = verdict,
        refusal = 0,
        found = found,
        mmStruct = 0L,
        probes = 1,
        rounds = 1,
        pokes = 1,
        error = 0,
        wordsBase = wordsBase,
        wordsBytes = 0x10000,
    )

    @Test
    fun aDiscoveredKernelAddressYieldsCanonicalSlots() {
        val wordsBase = 0x00007F0000000000L
        val userPage = wordsBase + 0xC000L
        val found = 0xFFFFFFC000200000uL.toLong()
        val decision = processor.process(result(found, wordsBase), userPage)

        assertTrue(decision is DiscoverProcessResult.Ready)
        val addresses = (decision as DiscoverProcessResult.Ready).addresses
        assertEquals(found, addresses.pageBase)
        assertEquals(found + 0x1280L, addresses.fakeTask)
        assertEquals(found + 0x0E80L, addresses.fakeLock)
        assertEquals(found + 0x1180L, addresses.waiterWord)
    }

    @Test
    fun unrunOrNonOkDiscoverIsRefused() {
        val userPage = 0x00007F000000C000L
        assertTrue(processor.process(result(ran = false), userPage) is DiscoverProcessResult.Refused)
        assertTrue(processor.process(result(verdict = 2), userPage) is DiscoverProcessResult.Refused)
        assertTrue(processor.process(result(found = 0), userPage) is DiscoverProcessResult.Refused)
        assertTrue(processor.process(result(wordsBase = 0), userPage) is DiscoverProcessResult.Refused)
    }

    @Test
    fun nonCanonicalPageIsRefused() {
        val decision = processor.process(result(found = 0x1000L, wordsBase = 0x1000L), 0x1000L)
        assertTrue(decision is DiscoverProcessResult.Refused)
    }
}
