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

/**
 * Narrows a transport `Long` into the unsigned 32-bit range native stores every
 * duration/attempt field in. A hand-edited HOCON/JSON value that is negative or
 * overflows the slot clamps instead of wrapping into an enormous wait.
 */
internal fun Long.toConfigUInt(): UInt = coerceIn(0L, UInt.MAX_VALUE.toLong()).toUInt()

/** Same as [toConfigUInt] for the 64-bit unsigned profile slots. */
internal fun Long.toConfigULong(): ULong = coerceAtLeast(0L).toULong()
