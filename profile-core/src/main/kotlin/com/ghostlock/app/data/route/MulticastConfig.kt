package com.ghostlock.app.data.route

data class MulticastConfig(
    val geometry: MulticastGeometry,
) : RouteConfig {
    override fun entries(): List<Pair<String, ULong>> = buildList {
        geometry.waiterOff?.let { add("waiter_off" to it.toLong().toULong()) }
        geometry.bufferSize?.let { add("buffer_size" to it.toULong()) }
        geometry.taskOffset?.let { add("task_offset" to it.toULong()) }
        geometry.lockOffset?.let { add("lock_offset" to it.toULong()) }
    }

    override fun apply(key: String, value: ULong): RouteConfig = when (key) {
        "waiter_off" -> copy(geometry = geometry.copy(waiterOff = value.toLong().toInt()))
        "buffer_size" -> copy(geometry = geometry.copy(bufferSize = value.toUInt()))
        "task_offset" -> copy(geometry = geometry.copy(taskOffset = value.toUInt()))
        "lock_offset" -> copy(geometry = geometry.copy(lockOffset = value.toUInt()))
        else -> this
    }

    companion object {
        val EMPTY = MulticastConfig(
            geometry = MulticastGeometry(null, null, null, null),
        )

        fun from(value: (String) -> Long?): MulticastConfig = MulticastConfig(
            geometry = MulticastGeometry(
                waiterOff = value("mcast.waiter_off")?.toInt(),
                bufferSize = value("mcast.buffer_size")?.toUInt(),
                taskOffset = value("mcast.task_offset")?.toUInt(),
                lockOffset = value("mcast.lock_offset")?.toUInt(),
            ),
        )
    }
}

/** Mirrors native `RouteGeometry`'s multicast members (all `std::optional`). */
data class MulticastGeometry(
    val waiterOff: Int?,
    val bufferSize: UInt?,
    val taskOffset: UInt?,
    val lockOffset: UInt?,
)
