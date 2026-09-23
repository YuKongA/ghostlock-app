package com.ghostlock.app.data.route

internal data class MulticastConfig(
    val geometry: MulticastGeometry,
    val fakeBssImageOffset: ULong,
    val resident: UInt,
    val readyTimeoutMs: UInt,
    val postRequeueSettleUs: UInt,
    val postAdjustSettleUs: UInt,
) : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = listOf(
        "mcast_waiter_off" to geometry.waiterOff,
        "mcast_buffer_size" to geometry.bufferSize.toLong(),
        "mcast_task_offset" to geometry.taskOffset.toLong(),
        "mcast_lock_offset" to geometry.lockOffset.toLong(),
        "mcast_fake_lock_offset" to geometry.fakeLockOffset.toLong(),
        "mcast_fake_task_offset" to geometry.fakeTaskOffset.toLong(),
        "mcast_lock_slots_offset" to geometry.lockSlotsOffset.toLong(),
        "mcast_lock_slot_count" to geometry.lockSlotCount.toLong(),
        "mcast_lock_slot_stride" to geometry.lockSlotStride.toLong(),
        "off_mcast_fake_bss" to fakeBssImageOffset.toLong(),
        "multicast_resident" to resident.toLong(),
        "multicast_ready_timeout_ms" to readyTimeoutMs.toLong(),
        "multicast_post_requeue_settle_us" to postRequeueSettleUs.toLong(),
        "multicast_post_adjust_settle_us" to postAdjustSettleUs.toLong(),
    )

    override fun apply(key: String, value: Long): RouteConfig = when (key) {
        "mcast_waiter_off" -> copy(geometry = geometry.copy(waiterOff = value))
        "mcast_buffer_size" -> copy(geometry = geometry.copy(bufferSize = value.toConfigUInt()))
        "mcast_task_offset" -> copy(geometry = geometry.copy(taskOffset = value.toConfigUInt()))
        "mcast_lock_offset" -> copy(geometry = geometry.copy(lockOffset = value.toConfigUInt()))
        "mcast_fake_lock_offset" ->
            copy(geometry = geometry.copy(fakeLockOffset = value.toConfigUInt()))
        "mcast_fake_task_offset" ->
            copy(geometry = geometry.copy(fakeTaskOffset = value.toConfigUInt()))
        "mcast_lock_slots_offset" ->
            copy(geometry = geometry.copy(lockSlotsOffset = value.toConfigUInt()))
        "mcast_lock_slot_count" ->
            copy(geometry = geometry.copy(lockSlotCount = value.toConfigUInt()))
        "mcast_lock_slot_stride" ->
            copy(geometry = geometry.copy(lockSlotStride = value.toConfigUInt()))
        "off_mcast_fake_bss" -> copy(fakeBssImageOffset = value.toConfigULong())
        "multicast_resident" -> copy(resident = value.toConfigUInt())
        "multicast_ready_timeout_ms" -> copy(readyTimeoutMs = value.toConfigUInt())
        "multicast_post_requeue_settle_us" -> copy(postRequeueSettleUs = value.toConfigUInt())
        "multicast_post_adjust_settle_us" -> copy(postAdjustSettleUs = value.toConfigUInt())
        else -> this
    }

    companion object {
        val EMPTY = MulticastConfig(
            geometry = MulticastGeometry(0L, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u),
            fakeBssImageOffset = 0uL,
            resident = 0u,
            readyTimeoutMs = 0u,
            postRequeueSettleUs = 0u,
            postAdjustSettleUs = 0u,
        )

        fun from(value: (String) -> Long): MulticastConfig = MulticastConfig(
            geometry = MulticastGeometry(
                waiterOff = value("mcast.waiter_off"),
                bufferSize = value("mcast.buffer_size").toConfigUInt(),
                taskOffset = value("mcast.task_offset").toConfigUInt(),
                lockOffset = value("mcast.lock_offset").toConfigUInt(),
                fakeLockOffset = value("mcast.fake_lock_offset").toConfigUInt(),
                fakeTaskOffset = value("mcast.fake_task_offset").toConfigUInt(),
                lockSlotsOffset = value("mcast.lock_slots_offset").toConfigUInt(),
                lockSlotCount = value("mcast.lock_slot_count").toConfigUInt(),
                lockSlotStride = value("mcast.lock_slot_stride").toConfigUInt(),
            ),
            fakeBssImageOffset = value("offset.mcast_fake_bss").toConfigULong(),
            resident = value("multicast_resident").toConfigUInt(),
            readyTimeoutMs =
                value("execution.routes.multicast_waiter.ready_timeout_ms").toConfigUInt(),
            postRequeueSettleUs =
                value("execution.routes.multicast_waiter.post_requeue_settle_us").toConfigUInt(),
            postAdjustSettleUs =
                value("execution.routes.multicast_waiter.post_adjust_settle_us").toConfigUInt(),
        )
    }
}

internal data class MulticastGeometry(
    val waiterOff: Long,
    val bufferSize: UInt,
    val taskOffset: UInt,
    val lockOffset: UInt,
    val fakeLockOffset: UInt,
    val fakeTaskOffset: UInt,
    val lockSlotsOffset: UInt,
    val lockSlotCount: UInt,
    val lockSlotStride: UInt,
)
