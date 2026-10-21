package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.WriteRequest

enum class Cve43499RouteId(val token: String) {
    MulticastWaiter("multicast_waiter"),
    SelectStack("select_stack"),
    TcpZerocopy("tcp_zerocopy"),
}

/** Per-route production port; params are route-owned bytes, never a generic union. */
interface Cve43499RoutePort : AutoCloseable {
    val route: Cve43499RouteId
    fun attempt(
        request: WriteRequest,
        page: ByteArray,
        inputs: PayloadPageInputs,
        params: ByteArray = ByteArray(0),
    ): RouteResult
    override fun close()
}

class Cve43499MulticastPort(private val delegate: KernelRoute) : Cve43499RoutePort {
    override val route: Cve43499RouteId = Cve43499RouteId.MulticastWaiter
    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs, params: ByteArray): RouteResult {
        if (params.isNotEmpty()) return RouteResult.Refused("multicast-params-unexpected")
        return delegate.attempt(request, page, inputs)
    }
    override fun close() = Unit
}

/** Named refusal for routes whose native implementation is not present. */
class Cve43499UnavailablePort(override val route: Cve43499RouteId, private val reason: String) : Cve43499RoutePort {
    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs, params: ByteArray): RouteResult =
        RouteResult.Refused("route-unavailable:" + route.token + ":" + reason)
    override fun close() = Unit
}

/** Explicit registry: unregistered routes are refused, never fallback to multicast. */
class Cve43499RouteRegistry(private val factories: Map<Cve43499RouteId, () -> Cve43499RoutePort>) {
    fun open(route: Cve43499RouteId): RouteOpenResult {
        val factory = factories[route] ?: return RouteOpenResult.Unavailable(route)
        return try {
            val port = factory()
            if (port.route != route) {
                port.close()
                RouteOpenResult.Mismatch(route, port.route)
            } else {
                RouteOpenResult.Opened(port)
            }
        } catch (problem: Throwable) {
            RouteOpenResult.Failed(route, problem.message ?: problem.javaClass.simpleName)
        }
    }

    fun registered(): Set<Cve43499RouteId> = factories.keys

    companion object {
        /** All three ports; callers provide the multicast delegate and route-owned params. */
        fun standard(
            multicast: KernelRoute,
            selectParams: com.ghostlock.app.jni.cve43499.NativeSelectStackParams,
            tcpFake: LongArray = LongArray(0),
            available: Set<Cve43499RouteId> = setOf(
                Cve43499RouteId.MulticastWaiter, Cve43499RouteId.SelectStack, Cve43499RouteId.TcpZerocopy,
            ),
        ): Cve43499RouteRegistry {
            val factories = mutableMapOf<Cve43499RouteId, () -> Cve43499RoutePort>()
            if (Cve43499RouteId.MulticastWaiter in available) {
                factories[Cve43499RouteId.MulticastWaiter] = { Cve43499MulticastPort(multicast) }
            }
            if (Cve43499RouteId.SelectStack in available) {
                factories[Cve43499RouteId.SelectStack] = { Cve43499SelectStackPort(selectParams) }
            }
            if (Cve43499RouteId.TcpZerocopy in available) {
                factories[Cve43499RouteId.TcpZerocopy] = { Cve43499TcpZerocopyPort(tcpFake) }
            }
            return Cve43499RouteRegistry(factories)
        }
    }
}

sealed interface RouteOpenResult {
    data class Opened(val port: Cve43499RoutePort) : RouteOpenResult
    data class Unavailable(val route: Cve43499RouteId) : RouteOpenResult
    data class Mismatch(val requested: Cve43499RouteId, val actual: Cve43499RouteId) : RouteOpenResult
    data class Failed(val route: Cve43499RouteId, val reason: String) : RouteOpenResult
}

data class Cve43499Plan(
    val route: Cve43499RouteId,
    val stepPlan: com.ghostlock.app.engine.ktstep.KtStepPlan,
    val routeParams: ByteArray = ByteArray(0),
)
