package com.ghostlock.app.core.flow

import com.ghostlock.app.backend.cve43284.flow.Flow43284Impl
import com.ghostlock.app.backend.cve43284.flow.Flow43284Request
import com.ghostlock.app.backend.cve43499.flow.Flow43499Impl
import com.ghostlock.app.backend.cve43499.flow.Flow43499KernelLayout
import com.ghostlock.app.backend.cve43499.flow.Flow43499Request
import com.ghostlock.app.backend.cve43499.flow.Flow43499Route
import com.ghostlock.app.backend.lua.FlowLuaImpl
import com.ghostlock.app.backend.lua.FlowLuaRequest
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FlowRequestTest {

    @Test
    fun `each backend binds its own request type`() {
        val lua: Flow<FlowLuaRequest> = FlowLuaImpl()
        val cve43499: Flow<Flow43499Request> = Flow43499Impl()
        val cve43284: Flow<Flow43284Request> = Flow43284Impl()

        assertEquals(FlowLuaImpl::class, lua.backendClass)
        assertEquals(Flow43499Impl::class, cve43499.backendClass)
        assertEquals(Flow43284Impl::class, cve43284.backendClass)
        assertTrue(FlowRequest::class.java.isAssignableFrom(FlowLuaRequest::class.java))
        assertTrue(FlowRequest::class.java.isAssignableFrom(Flow43499Request::class.java))
        assertTrue(FlowRequest::class.java.isAssignableFrom(Flow43284Request::class.java))
    }

    @Test
    fun `lua request exposes its own fields`() {
        val payload = byteArrayOf(1, 2, 3)
        val request = FlowLuaRequest(
            pathId = "entry",
            payload = payload,
            values = mapOf("route" to "select"),
        )

        assertEquals("entry", request.pathId)
        assertArrayEquals(payload, request.payload)
        assertEquals(mapOf("route" to "select"), request.values)
    }

    @Test
    fun `sequential flow accepts its own request`() = runBlocking {
        val result = Flow43499Impl().run(validRequest())

        assertTrue(result is FlowResult.Failed)
        assertEquals("cve43499-flow-not-wired", (result as FlowResult.Failed).reason)
    }

    @Test
    fun `lua flow accepts a script carried by its request`() = runBlocking {
        val result = FlowLuaImpl().run(
            FlowLuaRequest(values = mapOf("script" to "return 1")),
        )

        assertTrue(result is FlowResult.Ok)
    }

    @Test
    fun `lua flow fails closed without a script`() = runBlocking {
        val result = FlowLuaImpl().run(FlowLuaRequest())

        assertTrue(result is FlowResult.Failed)
        assertEquals("lua-script-missing", (result as FlowResult.Failed).reason)
    }

    private fun validRequest(): Flow43499Request = Flow43499Request(
        session = Flow43499Request.Session(
            init = Flow43499Request.Init(
                route = Flow43499Route.TcpZerocopy(),
                stamp = Flow43499Request.Stamp(
                    waiterOffset = 1L,
                    taskOffset = 1L,
                    lockOffset = 1L,
                    bufferSize = 1L,
                ),
                limits = Flow43499Request.Limits(
                    maxAttempts = 1L,
                    armSequence = 1L,
                    armHold = 1L,
                ),
                timing = Flow43499Request.Timing(parkMs = 1L),
                consumer = Flow43499Request.Consumer(
                    burstCalls = 1L,
                    maxCalls = 1L,
                    niceModulus = 1,
                ),
                fakeTask = 1L,
                fakeLock = 1L,
            ),
            layout = Flow43499KernelLayout(pointerBytes = 8),
        ),
        prepare = Flow43499Request.Prepare(
            threads = 1L,
            pageSize = 1L,
            regionSize = 1L,
            objectSize = 1L,
            mmStructSize = 1L,
        ),
        discover = Flow43499Request.Discover(
            rangeStart = 1L,
            patienceRounds = 1L,
        ),
    )
}
