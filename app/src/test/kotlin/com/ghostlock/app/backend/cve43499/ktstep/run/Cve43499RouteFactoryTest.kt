package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.RouteResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499RouteFactoryTest {
    @Test
    fun factoryBuildsTheConfiguredImplementation() {
        val delegate = KernelRoute { _, _, _ -> RouteResult.Landed() }
        val registry = Cve43499RouteFactory.registry(
            listOf(
                Cve43499RouteConfig.Multicast(delegate),
                Cve43499RouteConfig.Tcp(LongArray(10)),
            ),
        )
        val multicast = registry.open(Cve43499RouteId.MulticastWaiter)
        assertTrue(multicast is RouteOpenResult.Opened)
        assertEquals(Cve43499RouteId.MulticastWaiter, (multicast as RouteOpenResult.Opened).port.route)

        val tcp = registry.open(Cve43499RouteId.TcpZerocopy)
        assertTrue(tcp is RouteOpenResult.Opened)
        assertEquals(Cve43499RouteId.TcpZerocopy, (tcp as RouteOpenResult.Opened).port.route)
        assertEquals(setOf(Cve43499RouteId.MulticastWaiter, Cve43499RouteId.TcpZerocopy), registry.registered())
    }

    @Test(expected = IllegalArgumentException::class)
    fun duplicateRouteConfigIsRejected() {
        val delegate = KernelRoute { _, _, _ -> RouteResult.Landed() }
        Cve43499RouteFactory.registry(
            listOf(
                Cve43499RouteConfig.Multicast(delegate),
                Cve43499RouteConfig.Multicast(delegate),
            ),
        )
    }
}
