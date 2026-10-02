package com.ghostlock.app.data.route

/**
 * `route.multicast_waiter` section. Geometry fields mirror native's optional
 * members; the poison/walk repetition mirrors native's plain members, where a
 * zero value means "keep the compiled route default" and an absent key does the
 * same, so they are written only when the profile provides them.
 */
data class MulticastConfig(
    val geometry: MulticastGeometry,
    val attempts: UByte?,
    val armSequence: UByte?,
    val armHold: UShort?,
) : RouteConfig {
    override fun entries(): List<Pair<String, ULong>> = buildList {
        geometry.waiterOff?.let { add("waiter_off" to it.toLong().toULong()) }
        geometry.bufferSize?.let { add("buffer_size" to it.toULong()) }
        geometry.taskOffset?.let { add("task_offset" to it.toULong()) }
        geometry.lockOffset?.let { add("lock_offset" to it.toULong()) }
        attempts?.let { add("attempts" to it.toULong()) }
        armSequence?.let { add("arm_sequence" to it.toULong()) }
        armHold?.let { add("arm_hold" to it.toULong()) }
    }

    override fun apply(key: String, value: ULong): RouteConfig = when (key) {
        "waiter_off" -> copy(geometry = geometry.copy(waiterOff = value.toLong().toInt()))
        "buffer_size" -> copy(geometry = geometry.copy(bufferSize = value.toUInt()))
        "task_offset" -> copy(geometry = geometry.copy(taskOffset = value.toUInt()))
        "lock_offset" -> copy(geometry = geometry.copy(lockOffset = value.toUInt()))
        "attempts" -> copy(attempts = value.toUByte())
        "arm_sequence" -> copy(armSequence = value.toUByte())
        "arm_hold" -> copy(armHold = value.toUShort())
        else -> this
    }

    companion object {
        val EMPTY = MulticastConfig(
            geometry = MulticastGeometry(null, null, null, null),
            attempts = null,
            armSequence = null,
            armHold = null,
        )

        fun from(value: (String) -> Long?): MulticastConfig = MulticastConfig(
            geometry = MulticastGeometry(
                waiterOff = value("mcast.waiter_off")?.toInt(),
                bufferSize = value("mcast.buffer_size")?.toUInt(),
                taskOffset = value("mcast.task_offset")?.toUInt(),
                lockOffset = value("mcast.lock_offset")?.toUInt(),
            ),
            attempts = value("mcast.attempts")?.toUByte(),
            armSequence = value("mcast.arm_sequence")?.toUByte(),
            armHold = value("mcast.arm_hold")?.toUShort(),
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
