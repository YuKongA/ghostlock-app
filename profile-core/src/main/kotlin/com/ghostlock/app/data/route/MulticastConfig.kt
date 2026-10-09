package com.ghostlock.app.data.route

/**
 * `route.multicast_waiter` section. Geometry fields mirror native's optional
 * members; the poison/walk repetition mirrors native's plain members, where a
 * zero value means "keep the compiled route default" and an absent key does the
 * same, so they are written only when the profile provides them.
 *
 * Tuning source (task-142, mirroring [TcpConfig]/[SelectConfig]): the poison/walk
 * repetition reads the preset path `execution.routes.multicast_waiter.*` first
 * (carrier first; the key names are 1:1 with the wire fields) and only then falls
 * back to the legacy `mcast.*` spelling. Geometry is unchanged (profile route
 * branch).
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
            //TODO(task-142, execution-multicast-waiter 使能)：载体优先（预设
            //  execution.routes.multicast_waiter.*，键名与 wire 字段 1:1）、原 mcast.* 路径
            //  作回退。沿革：旧实现只读 mcast.*（经 ProfileResolver.nativeValue 映射到
            //  route.<route>.*），故 task-141 补的预设当时无消费者；接线后由
            //  MulticastPresetCarriageTest（值到 wire）与字节差量（+37 B/档）取证。
            attempts = (value("execution.routes.multicast_waiter.attempts")
                ?: value("mcast.attempts"))?.toUByte(),
            armSequence = (value("execution.routes.multicast_waiter.arm_sequence")
                ?: value("mcast.arm_sequence"))?.toUByte(),
            armHold = (value("execution.routes.multicast_waiter.arm_hold")
                ?: value("mcast.arm_hold"))?.toUShort(),
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
