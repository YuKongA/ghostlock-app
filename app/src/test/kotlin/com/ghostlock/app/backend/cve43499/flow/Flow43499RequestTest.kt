package com.ghostlock.app.backend.cve43499.flow

import com.ghostlock.app.core.flow.FlowRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Flow43499RequestTest {

    @Test
    fun `request carries session prepare and discover`() {
        val route = Flow43499Route.TcpZerocopy(
            zeroCopyMapBytes = 0x4000L,
            postReceiveHoldIterations = 8L,
            payloadDelta = -16,
            chunkBias = 32L,
            fakeTaskOff = 64L,
            credCopyOff = 96L,
        )
        val request = Flow43499Request(
            session = Flow43499Request.Session(
                init = completeInit(route),
                layout = Flow43499KernelLayout(slotFakeTask = 0x40L, pointerBytes = 8),
                credentials = Flow43499CredentialLayout(copySize = 0x100L, refCount = 4),
                targets = Flow43499TargetOffsets(
                    taskCred = Flow43499TargetField(
                        offset = 0x10L,
                        value = 0x1L,
                        present = true,
                        width = 4,
                    ),
                ),
            ),
            prepare = Flow43499Request.Prepare(collisions = 3L, mainCpu = 0),
            discover = Flow43499Request.Discover(possibleCpus = 8L),
            expectedUid = 0L,
            finishAction = Flow43499FinishAction.PARK,
        )

        assertTrue(FlowRequest::class.java.isAssignableFrom(request.javaClass))
        assertEquals(0x4000L, (request.session.init.route as Flow43499Route.TcpZerocopy).zeroCopyMapBytes)
        assertEquals(0x40L, request.session.layout.slotFakeTask)
        assertEquals(8, request.session.layout.pointerBytes)
        assertEquals(0x100L, request.session.credentials.copySize)
        assertEquals(0x10L, request.session.targets.taskCred.offset)
        assertEquals(3L, request.prepare.collisions)
        assertEquals(8L, request.discover.possibleCpus)
        assertEquals(Flow43499FinishAction.PARK, request.finishAction)
    }

    @Test
    fun `route variants keep native and legacy fields`() {
        val tcp = Flow43499Route.TcpZerocopy(
            zeroCopyMapBytes = 1L,
            postReceiveHoldIterations = 2L,
            payloadDelta = -3,
            chunkBias = 4L,
            fakeTaskOff = 5L,
            credCopyOff = 6L,
        )
        val select = Flow43499Route.SelectStack(
            fdCount = 320L,
            waiterShift = -2,
            enterDelayUs = 7L,
            timeoutUs = 8L,
            compactWaiter = true,
        )
        val multicast = Flow43499Route.MulticastWaiter(
            armRounds = 9L,
            stampWaiterShift = 10L,
            waiterOff = -11,
        )

        assertEquals(1L, tcp.wireId)
        assertEquals(1, tcp.presentMask)
        assertEquals("tcp_zerocopy", tcp.token)
        assertEquals(-3, tcp.payloadDelta)
        assertEquals(2L, select.wireId)
        assertEquals(2, select.presentMask)
        assertEquals(-2, select.waiterShift)
        assertEquals(320L, select.fdCount)
        assertEquals(3L, multicast.wireId)
        assertEquals(4, multicast.presentMask)
        assertEquals(-11, multicast.waiterOff)
    }

    @Test
    fun `present mask expands in wire order`() {
        assertEquals(Triple(false, true, false), wirePresence(Flow43499Route.TcpZerocopy()))
        assertEquals(Triple(false, false, true), wirePresence(Flow43499Route.SelectStack()))
        assertEquals(Triple(true, false, false), wirePresence(Flow43499Route.MulticastWaiter()))
    }

    @Test
    fun `route is an exhaustively sealed three-way choice`() {
        val routes = listOf(
            Flow43499Route.TcpZerocopy(),
            Flow43499Route.SelectStack(),
            Flow43499Route.MulticastWaiter(),
        )

        assertEquals(
            listOf("tcp_zerocopy", "select_stack", "multicast_waiter"),
            routes.map(::routeToken),
        )
    }

    @Test
    fun `finish action has no handoff`() {
        assertEquals(
            listOf(Flow43499FinishAction.RETIRE, Flow43499FinishAction.PARK),
            Flow43499FinishAction.entries,
        )
    }

    private fun routeToken(route: Flow43499Route): String = when (route) {
        is Flow43499Route.TcpZerocopy -> route.token
        is Flow43499Route.SelectStack -> route.token
        is Flow43499Route.MulticastWaiter -> route.token
    }

    private fun wirePresence(route: Flow43499Route): Triple<Boolean, Boolean, Boolean> =
        when (route) {
            is Flow43499Route.TcpZerocopy -> Triple(false, true, false)
            is Flow43499Route.SelectStack -> Triple(false, false, true)
            is Flow43499Route.MulticastWaiter -> Triple(true, false, false)
        }

    private fun completeInit(route: Flow43499Route): Flow43499Request.Init =
        Flow43499Request.Init(
            route = route,
            stamp = Flow43499Request.Stamp(
                waiterOffset = 96L,
                taskOffset = 48L,
                lockOffset = 56L,
                bufferSize = 264L,
            ),
            limits = Flow43499Request.Limits(
                maxAttempts = 128L,
                armSequence = 16L,
                armHold = 20_000L,
            ),
            timing = Flow43499Request.Timing(
                parkMs = 50L,
                settleUs = 100L,
                postRequeueSettleUs = 10L,
                postAdjustSettleUs = 20L,
                consumerDelayUs = 30L,
                fallbackLockUs = 300L,
            ),
            consumer = Flow43499Request.Consumer(
                burstCalls = 4L,
                maxCalls = 200L,
                niceModulus = 7,
                policy = 2,
            ),
            fakeTask = 0xAAL,
            fakeLock = 0xBBL,
        )
}
