package com.ghostlock.app.data.route

/**
 * Route-specific configuration. One subtype per route, each owning the wire
 * key names of its v2 route section. Never part of the shared document.
 */
internal sealed interface RouteConfig {
    fun entries(): List<Pair<String, Long>>

    fun apply(key: String, value: Long): RouteConfig
}

internal object NoRouteConfig : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = emptyList()

    override fun apply(key: String, value: Long): RouteConfig = this
}
