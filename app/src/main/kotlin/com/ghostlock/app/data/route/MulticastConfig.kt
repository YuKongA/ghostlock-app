package com.ghostlock.app.data.route

internal data class MulticastConfig(
    val geometry: MulticastGeometry,
    val fakeBssImageOffset: Long,
    val resident: Long,
    val readyTimeoutMs: Long,
    val postRequeueSettleUs: Long,
    val postAdjustSettleUs: Long,
) : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = listOf(
        "mcast_waiter_off" to geometry.waiterOff,
        "mcast_buffer_size" to geometry.bufferSize,
        "mcast_task_offset" to geometry.taskOffset,
        "mcast_lock_offset" to geometry.lockOffset,
        "mcast_fake_lock_offset" to geometry.fakeLockOffset,
        "mcast_fake_task_offset" to geometry.fakeTaskOffset,
        "mcast_lock_slots_offset" to geometry.lockSlotsOffset,
        "mcast_lock_slot_count" to geometry.lockSlotCount,
        "mcast_lock_slot_stride" to geometry.lockSlotStride,
        "off_mcast_fake_bss" to fakeBssImageOffset,
        "multicast_resident" to resident,
        "multicast_ready_timeout_ms" to readyTimeoutMs,
        "multicast_post_requeue_settle_us" to postRequeueSettleUs,
        "multicast_post_adjust_settle_us" to postAdjustSettleUs,
    )

    override fun apply(key: String, value: Long): RouteConfig = when (key) {
        "mcast_waiter_off" -> copy(geometry = geometry.copy(waiterOff = value))
        "mcast_buffer_size" -> copy(geometry = geometry.copy(bufferSize = value))
        "mcast_task_offset" -> copy(geometry = geometry.copy(taskOffset = value))
        "mcast_lock_offset" -> copy(geometry = geometry.copy(lockOffset = value))
        "mcast_fake_lock_offset" -> copy(geometry = geometry.copy(fakeLockOffset = value))
        "mcast_fake_task_offset" -> copy(geometry = geometry.copy(fakeTaskOffset = value))
        "mcast_lock_slots_offset" -> copy(geometry = geometry.copy(lockSlotsOffset = value))
        "mcast_lock_slot_count" -> copy(geometry = geometry.copy(lockSlotCount = value))
        "mcast_lock_slot_stride" -> copy(geometry = geometry.copy(lockSlotStride = value))
        "off_mcast_fake_bss" -> copy(fakeBssImageOffset = value)
        "multicast_resident" -> copy(resident = value)
        "multicast_ready_timeout_ms" -> copy(readyTimeoutMs = value)
        "multicast_post_requeue_settle_us" -> copy(postRequeueSettleUs = value)
        "multicast_post_adjust_settle_us" -> copy(postAdjustSettleUs = value)
        else -> this
    }

    companion object {
        val EMPTY = MulticastConfig(
            geometry = MulticastGeometry(0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L),
            fakeBssImageOffset = 0L,
            resident = 0L,
            readyTimeoutMs = 0L,
            postRequeueSettleUs = 0L,
            postAdjustSettleUs = 0L,
        )

        fun from(value: (String) -> Long): MulticastConfig = MulticastConfig(
            geometry = MulticastGeometry(
                waiterOff = value("mcast.waiter_off"),
                bufferSize = value("mcast.buffer_size"),
                taskOffset = value("mcast.task_offset"),
                lockOffset = value("mcast.lock_offset"),
                fakeLockOffset = value("mcast.fake_lock_offset"),
                fakeTaskOffset = value("mcast.fake_task_offset"),
                lockSlotsOffset = value("mcast.lock_slots_offset"),
                lockSlotCount = value("mcast.lock_slot_count"),
                lockSlotStride = value("mcast.lock_slot_stride"),
            ),
            fakeBssImageOffset = value("offset.mcast_fake_bss"),
            resident = value("multicast_resident"),
            readyTimeoutMs = value("execution.routes.multicast_waiter.ready_timeout_ms"),
            postRequeueSettleUs = value("execution.routes.multicast_waiter.post_requeue_settle_us"),
            postAdjustSettleUs = value("execution.routes.multicast_waiter.post_adjust_settle_us"),
        )
    }
}

internal data class MulticastGeometry(
    val waiterOff: Long,
    val bufferSize: Long,
    val taskOffset: Long,
    val lockOffset: Long,
    val fakeLockOffset: Long,
    val fakeTaskOffset: Long,
    val lockSlotsOffset: Long,
    val lockSlotCount: Long,
    val lockSlotStride: Long,
)
