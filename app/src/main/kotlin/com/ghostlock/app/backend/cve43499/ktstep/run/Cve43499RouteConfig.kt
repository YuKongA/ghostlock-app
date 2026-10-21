package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.jni.cve43499.NativeSelectStackParams

/** Route-owned parameters; the route id decides which implementation the factory creates. */
sealed interface Cve43499RouteConfig {
    val route: Cve43499RouteId

    data class Multicast(val delegate: KernelRoute) : Cve43499RouteConfig {
        override val route: Cve43499RouteId = Cve43499RouteId.MulticastWaiter
    }

    data class Select(val params: NativeSelectStackParams) : Cve43499RouteConfig {
        override val route: Cve43499RouteId = Cve43499RouteId.SelectStack
    }

    data class Tcp(val fakePage: LongArray) : Cve43499RouteConfig {
        override val route: Cve43499RouteId = Cve43499RouteId.TcpZerocopy
    }
}

/** Single polymorphic construction point: config in, concrete port out. */
object Cve43499RouteFactory {
    fun create(config: Cve43499RouteConfig): Cve43499RoutePort = when (config) {
        is Cve43499RouteConfig.Multicast -> Cve43499MulticastPort(config.delegate)
        is Cve43499RouteConfig.Select -> Cve43499SelectStackPort(config.params)
        is Cve43499RouteConfig.Tcp -> Cve43499TcpZerocopyPort(config.fakePage)
    }

    /** Build a registry from per-route configs; duplicate ids are rejected, not overwritten. */
    fun registry(configs: List<Cve43499RouteConfig>): Cve43499RouteRegistry {
        val duplicates = configs.groupBy { it.route }.filterValues { it.size > 1 }.keys
        require(duplicates.isEmpty()) { "duplicate route config: " + duplicates.joinToString() }
        val factories = configs.associate { config ->
            config.route to { create(config) }
        }
        return Cve43499RouteRegistry(factories)
    }
}
