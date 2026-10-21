package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499RoutePortTest {
    private class FakeRoute : KernelRoute {
        var calls = 0
        override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs): RouteResult {
            calls += 1
            return RouteResult.Landed()
        }
    }

    private fun inputs() = PayloadPageInputs(
        PayloadSlots(0,0,0,0,0), LockFields(0,0,0,0),
        WaiterFields(0,0,0,0,0,0,0,0,0,0,0,0), TaskFields(0,0,0,0,0,0,0,0),
        PayloadValues(0,0,0), 0, 0, 0, 0, 0, 0, 8,
    )

    @Test
    fun multicastPortOwnsNoParamsAndForwards() {
        val fake = FakeRoute()
        val port = Cve43499MulticastPort(fake)
        assertEquals(Cve43499RouteId.MulticastWaiter, port.route)
        assertTrue(port.attempt(WriteRequest(1, WriteMode.Zero, true), ByteArray(1), inputs()) is RouteResult.Landed)
        assertEquals(1, fake.calls)
        assertTrue(port.attempt(WriteRequest(1, WriteMode.Zero, true), ByteArray(1), inputs(), byteArrayOf(1)) is RouteResult.Refused)
    }

    @Test
    fun standardRegistryOpensAllThreeRoutes() {
        val select = com.ghostlock.app.jni.cve43499.NativeSelectStackParams(
            target = 0, value = 0, fdCount = 64, attempts = 1, selectWaitUs = 1,
            niceModulus = 0, waiterShift = 0, compact = true, fakeWaiterPrio = 0,
            fakeTask = 0, fakeLock = 0, wordTask = 0, wordLock = 0, wordTreePc = 0,
            wordTreeRight = 0, wordTreeLeft = 0, wordPiPc = 0, wordPiRight = 0,
            wordPiLeft = 0, wordWakePrio = 0, wordDeadline = 0, wordWwCtx = 0,
            waitWord = 0, chainWord = 0, targetWord = 0, selected = intArrayOf(0),
        )
        val registry = Cve43499RouteRegistry.standard(FakeRoute(), select)
        assertTrue(registry.open(Cve43499RouteId.MulticastWaiter) is RouteOpenResult.Opened)
        assertTrue(registry.open(Cve43499RouteId.SelectStack) is RouteOpenResult.Opened)
        assertTrue(registry.open(Cve43499RouteId.TcpZerocopy) is RouteOpenResult.Opened)
    }

    @Test
    fun availabilityOnlyOpensSupportedRoutes() {
        val select = com.ghostlock.app.jni.cve43499.NativeSelectStackParams(
            target = 0, value = 0, fdCount = 64, attempts = 1, selectWaitUs = 1,
            niceModulus = 0, waiterShift = 0, compact = true, fakeWaiterPrio = 0,
            fakeTask = 0, fakeLock = 0, wordTask = 0, wordLock = 0, wordTreePc = 0,
            wordTreeRight = 0, wordTreeLeft = 0, wordPiPc = 0, wordPiRight = 0,
            wordPiLeft = 0, wordWakePrio = 0, wordDeadline = 0, wordWwCtx = 0,
            waitWord = 0, chainWord = 0, targetWord = 0, selected = intArrayOf(0),
        )
        val registry = Cve43499RouteRegistry.standard(
            FakeRoute(), select, available = setOf(Cve43499RouteId.MulticastWaiter),
        )
        assertTrue(registry.open(Cve43499RouteId.MulticastWaiter) is RouteOpenResult.Opened)
        assertTrue(registry.open(Cve43499RouteId.SelectStack) is RouteOpenResult.Unavailable)
        assertTrue(registry.open(Cve43499RouteId.TcpZerocopy) is RouteOpenResult.Unavailable)
    }

    @Test
    fun registryRefusesUnregisteredAndMismatchedRoutes() {
        val fake = FakeRoute()
        val registry = Cve43499RouteRegistry(
            mapOf(Cve43499RouteId.MulticastWaiter to { Cve43499MulticastPort(fake) }),
        )
        assertTrue(registry.open(Cve43499RouteId.MulticastWaiter) is RouteOpenResult.Opened)
        assertTrue(registry.open(Cve43499RouteId.SelectStack) is RouteOpenResult.Unavailable)
        assertTrue(registry.open(Cve43499RouteId.TcpZerocopy) is RouteOpenResult.Unavailable)

        val wrong = Cve43499RouteRegistry(
            mapOf(Cve43499RouteId.MulticastWaiter to { Cve43499UnavailablePort(Cve43499RouteId.SelectStack, "x") }),
        )
        assertTrue(wrong.open(Cve43499RouteId.MulticastWaiter) is RouteOpenResult.Mismatch)
    }

    @Test
    fun unavailableRoutesNameThemselves() {
        for (id in listOf(Cve43499RouteId.SelectStack, Cve43499RouteId.TcpZerocopy)) {
            val result = Cve43499UnavailablePort(id, "not-ported").attempt(
                WriteRequest(1, WriteMode.Zero, true), ByteArray(1), inputs(),
            )
            assertTrue(result is RouteResult.Refused)
            assertTrue((result as RouteResult.Refused).reason.contains(id.token))
        }
    }
}
