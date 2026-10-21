package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.NativePrepareResult
import org.junit.Assert.assertEquals
import org.junit.Test

class Cve43499FakePageTest {
    @Test
    fun derivesSelectLayoutFromPrepareBase() {
        val facts = NativePrepareResult.Ok(
            ran = true, status = 0, refusal = 0, atStep = 0, base = 0x100000L,
            wordsBase = 0L, payloadBase = 0L, shapingFd = 1, shapingPeerFd = 2,
            opened = 0, closed = 0, error = 0, proofCount = 0, snitchFound = 0,
            snitchAddress = 0, snitchVerdict = 0, proofs = LongArray(0),
            words = LongArray(0), payloadOffset = 0,
        )
        val page = Cve43499FakePage.fromPrepared(facts)
        assertEquals(0x100000L - 0xE80L, page[0])
        assertEquals(page[0] + 0xE80L, page[1])
        assertEquals(page[0] + 0x1280L, page[3])
        assertEquals(page[0] + 0xF80L, page[7])
    }
}
