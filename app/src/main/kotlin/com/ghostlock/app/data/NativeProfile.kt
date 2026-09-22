package com.ghostlock.app.data

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Typed mirror of the native `struct kernel_offsets` (GLK1 v4: fixed common
 * slots + a per-route section). Route-specific parameters live in [routeConfig],
 * not in the common document. The common slot order must match
 * `src/core/profile/binary.cpp` `kCommonFields`; route keys must match its
 * `kXxxFields`.
 */
internal data class NativeProfileDocument(
    val release: String,
    val routeKind: Int,
    val kernelMajor: Long,
    val recommendShizuku: Long,
    val fallbackRoute: Int,
    val taskStruct: TaskStructOffsets,
    val cred: CredTemplate,
    val kernelOffset: KernelOffsetTable,
    val kernelPhysLoad: Long,
    val compactWaiter: Long,
    val kernelsnitchCollisions: Long,
    val mmStructSz: Long,
    val execution: ExecutionTuning,
    val safeMode: Long,
    /** Route-specific configuration; never part of the shared schema. */
    val routeConfig: RouteConfig,
) {
    fun toBinary(): ByteArray {
        val releaseBytes = release.toByteArray(Charsets.UTF_8)
        require(releaseBytes.size <= 0xffff) { "release is too long" }
        val common = flattenCommon()
        val route = routeEntries()
        var size = HeaderSize + releaseBytes.size + common.size * 8 + 1
        for ((key, _) in route) size += 1 + key.toByteArray(Charsets.UTF_8).size + 8
        val buffer = ByteBuffer
            .allocate(size)
            .order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(Magic)
        buffer.putShort(Version)
        buffer.put(routeKind.toByte())
        buffer.put(kernelMajor.toByte())
        buffer.put(recommendShizuku.toByte())
        buffer.put(fallbackRoute.toByte())
        buffer.putShort(releaseBytes.size.toShort())
        buffer.put(releaseBytes)
        common.forEach(buffer::putLong)
        buffer.put(route.size.toByte())
        for ((key, value) in route) {
            val kb = key.toByteArray(Charsets.UTF_8)
            buffer.put(kb.size.toByte())
            buffer.put(kb)
            buffer.putLong(value)
        }
        return buffer.array()
    }

    /** Route-independent slots (order shared with native kCommonFields). */
    private fun flattenCommon(): LongArray {
        val task = taskStruct
        val credential = cred
        val offsets = kernelOffset
        val exec = execution
        return longArrayOf(
            task.prio, task.normalPrio, task.schedTaskGroup, task.piLock,
            task.piWaiters, task.piTopTask, task.piBlockedOn, task.pid, task.tgid,
            task.atomicFlags, task.realCred, task.cred, task.comm, task.tasks,
            task.seccomp,
            credential.copySize, credential.usageOffset, credential.usageValue,
            credential.capsOffset, credential.capsCount, credential.capsValue,
            credential.refCount,
            credential.ref0Offset, credential.ref1Offset, credential.ref2Offset,
            credential.ref3Offset, credential.ref0Image, credential.ref1Image,
            credential.ref2Image, credential.ref3Image,
            offsets.initTask, offsets.initCred, offsets.emptyZeroPage,
            offsets.rootTaskGroup, offsets.selinuxEnforcing, offsets.selinuxBlobSizes,
            offsets.securityHookHeads, offsets.slideNfulnlLogger, offsets.slideLoggers01,
            offsets.slideBootId,
            kernelPhysLoad, compactWaiter, kernelsnitchCollisions, mmStructSz,
            exec.recommendedMainCpu, exec.recommendedConsumerCpu,
            exec.heapPrepareMaxAttempts, exec.heapPrepareTimeoutMs,
            exec.heapKernelsnitchTimeoutMs, exec.raceRouteWaitMs,
            exec.raceSetupSettleUs, exec.raceStatePollIntervalUs,
            exec.w1Attempts, exec.w1SettleUs, exec.w1ScratchRepairAttempts,
            exec.w2Attempts, exec.w2SettleUs, exec.w3ChainRounds,
            exec.w3Attempts, exec.w3SettleUs,
            exec.handoffPreDispatchSettleMs, exec.handoffModulePollAttempts,
            exec.handoffModulePollIntervalMs, exec.handoffEnforcePollAttempts,
            exec.handoffEnforcePollIntervalMs,
            safeMode,
        )
    }

    /** Route-specific entries emitted from [routeConfig] (keys shared with the
     * native route table). */
    private fun routeEntries(): List<Pair<String, Long>> = when (val cfg = routeConfig) {
        is TcpConfig -> listOf(
            "tcp_attempts" to cfg.attempts,
            "tcp_arm_sequence" to cfg.armSequence,
            "tcp_post_receive_hold_iterations" to cfg.postReceiveHoldIterations,
        )

        is SelectConfig -> listOf(
            "pselect_waiter_shift" to cfg.waiterShift,
            "select_enter_delay_us" to cfg.enterDelayUs,
            "select_timeout_us" to cfg.timeoutUs,
            "select_consumer_max_calls" to cfg.consumerMaxCalls,
            "select_consumer_burst_calls" to cfg.consumerBurstCalls,
        )

        is MulticastConfig -> listOf(
            "mcast_waiter_off" to cfg.geometry.waiterOff,
            "mcast_buffer_size" to cfg.geometry.bufferSize,
            "mcast_task_offset" to cfg.geometry.taskOffset,
            "mcast_lock_offset" to cfg.geometry.lockOffset,
            "mcast_fake_lock_offset" to cfg.geometry.fakeLockOffset,
            "mcast_fake_task_offset" to cfg.geometry.fakeTaskOffset,
            "mcast_lock_slots_offset" to cfg.geometry.lockSlotsOffset,
            "mcast_lock_slot_count" to cfg.geometry.lockSlotCount,
            "mcast_lock_slot_stride" to cfg.geometry.lockSlotStride,
            "off_mcast_fake_bss" to cfg.fakeBssImageOffset,
            "multicast_resident" to cfg.resident,
            "multicast_ready_timeout_ms" to cfg.readyTimeoutMs,
            "multicast_post_requeue_settle_us" to cfg.postRequeueSettleUs,
            "multicast_post_adjust_settle_us" to cfg.postAdjustSettleUs,
        )

        NoRouteConfig -> emptyList()
    }

    companion object {
        const val Magic = 0x314B4C47
        const val Version: Short = 4
        private const val HeaderSize = 12
        private const val CommonFieldCount = 66

        fun routeKind(route: String?): Int = RouteKind.fromToken(route)?.wire ?: 0

        fun fromBinary(bytes: ByteArray): NativeProfileDocument? {
            if (bytes.size < HeaderSize) return null
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            if (buffer.int != Magic) return null
            if (buffer.short != Version) return null
            val routeKind = buffer.get().toInt() and 0xff
            val kernelMajor = buffer.get().toLong() and 0xff
            val recommendShizuku = buffer.get().toLong() and 0xff
            val fallbackRoute = buffer.get().toInt() and 0xff
            val releaseLength = buffer.short.toInt() and 0xffff
            if (buffer.remaining() < releaseLength + CommonFieldCount * 8 + 1) return null
            val releaseBytes = ByteArray(releaseLength)
            buffer.get(releaseBytes)
            val common = LongArray(CommonFieldCount) { buffer.long }
            var routeConfig: RouteConfig = emptyRouteConfig(routeKind)
            val count = buffer.get().toInt() and 0xff
            repeat(count) {
                if (buffer.remaining() < 1) return null
                val keyLength = buffer.get().toInt() and 0xff
                if (buffer.remaining() < keyLength + 8) return null
                val keyBytes = ByteArray(keyLength)
                buffer.get(keyBytes)
                routeConfig = applyRouteEntry(routeConfig, String(keyBytes, Charsets.UTF_8), buffer.long)
            }
            return fromCommon(
                release = String(releaseBytes, Charsets.UTF_8),
                routeKind = routeKind,
                kernelMajor = kernelMajor,
                recommendShizuku = recommendShizuku,
                fallbackRoute = fallbackRoute,
                f = common,
                routeConfig = routeConfig,
            )
        }

        private fun emptyRouteConfig(routeKind: Int): RouteConfig = when (RouteKind.fromWire(routeKind)) {
            RouteKind.TCP_ZEROCOPY -> TcpConfig(0L, 0L, 0L)
            RouteKind.SELECT_STACK -> SelectConfig(0L, 0L, 0L, 0L, 0L)
            RouteKind.MULTICAST_WAITER -> MulticastConfig(
                geometry = MulticastGeometry(0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L),
                fakeBssImageOffset = 0L, resident = 0L,
                readyTimeoutMs = 0L, postRequeueSettleUs = 0L, postAdjustSettleUs = 0L,
            )

            else -> NoRouteConfig
        }

        /* Common slot indices mirror flattenCommon() one-to-one. */
        private fun fromCommon(
            release: String,
            routeKind: Int,
            kernelMajor: Long,
            recommendShizuku: Long,
            fallbackRoute: Int,
            f: LongArray,
            routeConfig: RouteConfig,
        ): NativeProfileDocument = NativeProfileDocument(
            release = release,
            routeKind = routeKind,
            kernelMajor = kernelMajor,
            recommendShizuku = recommendShizuku,
            fallbackRoute = fallbackRoute,
            taskStruct = TaskStructOffsets(
                prio = f[0], normalPrio = f[1], schedTaskGroup = f[2],
                piLock = f[3], piWaiters = f[4], piTopTask = f[5],
                piBlockedOn = f[6], pid = f[7], tgid = f[8],
                atomicFlags = f[9], realCred = f[10], cred = f[11],
                comm = f[12], tasks = f[13], seccomp = f[14],
            ),
            cred = CredTemplate(
                copySize = f[15], usageOffset = f[16], usageValue = f[17],
                capsOffset = f[18], capsCount = f[19], capsValue = f[20],
                refCount = f[21], ref0Offset = f[22], ref1Offset = f[23],
                ref2Offset = f[24], ref3Offset = f[25], ref0Image = f[26],
                ref1Image = f[27], ref2Image = f[28], ref3Image = f[29],
            ),
            kernelOffset = KernelOffsetTable(
                initTask = f[30], initCred = f[31], emptyZeroPage = f[32],
                rootTaskGroup = f[33], selinuxEnforcing = f[34],
                selinuxBlobSizes = f[35], securityHookHeads = f[36],
                slideNfulnlLogger = f[37], slideLoggers01 = f[38], slideBootId = f[39],
            ),
            kernelPhysLoad = f[40],
            compactWaiter = f[41],
            kernelsnitchCollisions = f[42],
            mmStructSz = f[43],
            execution = ExecutionTuning(
                recommendedMainCpu = f[44], recommendedConsumerCpu = f[45],
                heapPrepareMaxAttempts = f[46], heapPrepareTimeoutMs = f[47],
                heapKernelsnitchTimeoutMs = f[48], raceRouteWaitMs = f[49],
                raceSetupSettleUs = f[50], raceStatePollIntervalUs = f[51],
                w1Attempts = f[52], w1SettleUs = f[53],
                w1ScratchRepairAttempts = f[54], w2Attempts = f[55], w2SettleUs = f[56],
                w3ChainRounds = f[57], w3Attempts = f[58], w3SettleUs = f[59],
                handoffPreDispatchSettleMs = f[60], handoffModulePollAttempts = f[61],
                handoffModulePollIntervalMs = f[62], handoffEnforcePollAttempts = f[63],
                handoffEnforcePollIntervalMs = f[64],
            ),
            safeMode = f[65],
            routeConfig = routeConfig,
        )

        private fun applyRouteEntry(
            config: RouteConfig,
            key: String,
            value: Long,
        ): RouteConfig = when (config) {
            is TcpConfig -> when (key) {
                "tcp_attempts" -> config.copy(attempts = value)
                "tcp_arm_sequence" -> config.copy(armSequence = value)
                "tcp_post_receive_hold_iterations" -> config.copy(postReceiveHoldIterations = value)
                else -> config
            }

            is SelectConfig -> when (key) {
                "pselect_waiter_shift" -> config.copy(waiterShift = value)
                "select_enter_delay_us" -> config.copy(enterDelayUs = value)
                "select_timeout_us" -> config.copy(timeoutUs = value)
                "select_consumer_max_calls" -> config.copy(consumerMaxCalls = value)
                "select_consumer_burst_calls" -> config.copy(consumerBurstCalls = value)
                else -> config
            }

            is MulticastConfig -> when (key) {
                "mcast_waiter_off" -> config.copy(geometry = config.geometry.copy(waiterOff = value))
                "mcast_buffer_size" -> config.copy(geometry = config.geometry.copy(bufferSize = value))
                "mcast_task_offset" -> config.copy(geometry = config.geometry.copy(taskOffset = value))
                "mcast_lock_offset" -> config.copy(geometry = config.geometry.copy(lockOffset = value))
                "mcast_fake_lock_offset" ->
                    config.copy(geometry = config.geometry.copy(fakeLockOffset = value))
                "mcast_fake_task_offset" ->
                    config.copy(geometry = config.geometry.copy(fakeTaskOffset = value))
                "mcast_lock_slots_offset" ->
                    config.copy(geometry = config.geometry.copy(lockSlotsOffset = value))
                "mcast_lock_slot_count" ->
                    config.copy(geometry = config.geometry.copy(lockSlotCount = value))
                "mcast_lock_slot_stride" ->
                    config.copy(geometry = config.geometry.copy(lockSlotStride = value))
                "off_mcast_fake_bss" -> config.copy(fakeBssImageOffset = value)
                "multicast_resident" -> config.copy(resident = value)
                "multicast_ready_timeout_ms" -> config.copy(readyTimeoutMs = value)
                "multicast_post_requeue_settle_us" -> config.copy(postRequeueSettleUs = value)
                "multicast_post_adjust_settle_us" -> config.copy(postAdjustSettleUs = value)
                else -> config
            }

            NoRouteConfig -> config
        }

        /** Builds the document from resolved profile values by dotted path. */
        fun from(
            release: String,
            route: String?,
            fallbackTo: String?,
            value: (String) -> Long?,
        ): NativeProfileDocument {
            fun v(path: String): Long = value(path) ?: 0L
            val routeConfig: RouteConfig = when (RouteKind.fromToken(route)) {
                RouteKind.TCP_ZEROCOPY -> TcpConfig(
                    attempts = v("execution.routes.tcp_zerocopy.attempts"),
                    armSequence = v("execution.routes.tcp_zerocopy.arm_sequence"),
                    postReceiveHoldIterations =
                        v("execution.routes.tcp_zerocopy.post_receive_hold_iterations"),
                )

                RouteKind.SELECT_STACK -> SelectConfig(
                    waiterShift = v("pselect_waiter_shift"),
                    enterDelayUs = v("execution.routes.select_stack.enter_delay_us"),
                    timeoutUs = v("execution.routes.select_stack.timeout_us"),
                    consumerMaxCalls = v("execution.routes.select_stack.consumer_max_calls"),
                    consumerBurstCalls = v("execution.routes.select_stack.consumer_burst_calls"),
                )

                RouteKind.MULTICAST_WAITER -> MulticastConfig(
                    geometry = MulticastGeometry(
                        waiterOff = v("mcast.waiter_off"),
                        bufferSize = v("mcast.buffer_size"),
                        taskOffset = v("mcast.task_offset"),
                        lockOffset = v("mcast.lock_offset"),
                        fakeLockOffset = v("mcast.fake_lock_offset"),
                        fakeTaskOffset = v("mcast.fake_task_offset"),
                        lockSlotsOffset = v("mcast.lock_slots_offset"),
                        lockSlotCount = v("mcast.lock_slot_count"),
                        lockSlotStride = v("mcast.lock_slot_stride"),
                    ),
                    fakeBssImageOffset = v("offset.mcast_fake_bss"),
                    resident = v("multicast_resident"),
                    readyTimeoutMs = v("execution.routes.multicast_waiter.ready_timeout_ms"),
                    postRequeueSettleUs =
                        v("execution.routes.multicast_waiter.post_requeue_settle_us"),
                    postAdjustSettleUs =
                        v("execution.routes.multicast_waiter.post_adjust_settle_us"),
                )

                else -> NoRouteConfig
            }
            return NativeProfileDocument(
                release = release,
                routeKind = routeKind(route),
                kernelMajor = v("kernel_major"),
                recommendShizuku = v("recommend_shizuku"),
                fallbackRoute = routeKind(fallbackTo),
                taskStruct = TaskStructOffsets(
                    prio = v("task_struct.prio"),
                    normalPrio = v("task_struct.normal_prio"),
                    schedTaskGroup = v("task_struct.sched_task_group"),
                    piLock = v("task_struct.pi_lock"),
                    piWaiters = v("task_struct.pi_waiters"),
                    piTopTask = v("task_struct.pi_top_task"),
                    piBlockedOn = v("task_struct.pi_blocked_on"),
                    pid = v("task_struct.pid"),
                    tgid = v("task_struct.tgid"),
                    atomicFlags = v("task_struct.atomic_flags"),
                    realCred = v("task_struct.real_cred"),
                    cred = v("task_struct.cred"),
                    comm = v("task_struct.comm"),
                    tasks = v("task_struct.tasks"),
                    seccomp = v("task_struct.seccomp"),
                ),
                cred = CredTemplate(
                    copySize = v("cred.copy_size"),
                    usageOffset = v("cred.usage_offset"),
                    usageValue = v("cred.usage_value"),
                    capsOffset = v("cred.caps_offset"),
                    capsCount = v("cred.caps_count"),
                    capsValue = v("cred.caps_value"),
                    refCount = v("cred.ref_count"),
                    ref0Offset = v("cred.ref0_offset"),
                    ref1Offset = v("cred.ref1_offset"),
                    ref2Offset = v("cred.ref2_offset"),
                    ref3Offset = v("cred.ref3_offset"),
                    ref0Image = v("cred.ref0_image"),
                    ref1Image = v("cred.ref1_image"),
                    ref2Image = v("cred.ref2_image"),
                    ref3Image = v("cred.ref3_image"),
                ),
                kernelOffset = KernelOffsetTable(
                    initTask = v("offset.init_task"),
                    initCred = v("offset.init_cred"),
                    emptyZeroPage = v("offset.empty_zero_page"),
                    rootTaskGroup = v("offset.root_task_group"),
                    selinuxEnforcing = v("offset.selinux_enforcing"),
                    selinuxBlobSizes = v("offset.selinux_blob_sizes"),
                    securityHookHeads = v("offset.security_hook_heads"),
                    slideNfulnlLogger = v("offset.slide_nfulnl_logger"),
                    slideLoggers01 = v("offset.slide_loggers_0_1"),
                    slideBootId = v("offset.slide_boot_id"),
                ),
                kernelPhysLoad = v("kernel_phys_load"),
                compactWaiter = v("compact_waiter"),
                kernelsnitchCollisions = v("kernelsnitch.collisions"),
                mmStructSz = v("kernelsnitch.mm_struct_sz"),
                execution = ExecutionTuning(
                    recommendedMainCpu = v("execution.recommended_cpus.main"),
                    recommendedConsumerCpu = v("execution.recommended_cpus.consumer"),
                    heapPrepareMaxAttempts = v("execution.heap.prepare_max_attempts"),
                    heapPrepareTimeoutMs = v("execution.heap.prepare_timeout_ms"),
                    heapKernelsnitchTimeoutMs = v("execution.heap.kernelsnitch_timeout_ms"),
                    raceRouteWaitMs = v("execution.race.route_wait_ms"),
                    raceSetupSettleUs = v("execution.race.setup_settle_us"),
                    raceStatePollIntervalUs = v("execution.race.state_poll_interval_us"),
                    w1Attempts = v("execution.stages.w1_attempts"),
                    w1SettleUs = v("execution.stages.w1_settle_us"),
                    w1ScratchRepairAttempts = v("execution.stages.w1_scratch_repair_attempts"),
                    w2Attempts = v("execution.stages.w2_attempts"),
                    w2SettleUs = v("execution.stages.w2_settle_us"),
                    w3ChainRounds = v("execution.stages.w3_chain_rounds"),
                    w3Attempts = v("execution.stages.w3_attempts"),
                    w3SettleUs = v("execution.stages.w3_settle_us"),
                    handoffPreDispatchSettleMs = v("execution.handoff.pre_dispatch_settle_ms"),
                    handoffModulePollAttempts = v("execution.handoff.module_poll_attempts"),
                    handoffModulePollIntervalMs = v("execution.handoff.module_poll_interval_ms"),
                    handoffEnforcePollAttempts = v("execution.handoff.enforce_poll_attempts"),
                    handoffEnforcePollIntervalMs = v("execution.handoff.enforce_poll_interval_ms"),
                ),
                safeMode = 0,
                routeConfig = routeConfig,
            )
        }
    }
}

internal data class TaskStructOffsets(
    val prio: Long,
    val normalPrio: Long,
    val schedTaskGroup: Long,
    val piLock: Long,
    val piWaiters: Long,
    val piTopTask: Long,
    val piBlockedOn: Long,
    val pid: Long,
    val tgid: Long,
    val atomicFlags: Long,
    val realCred: Long,
    val cred: Long,
    val comm: Long,
    val tasks: Long,
    val seccomp: Long,
)

internal data class CredTemplate(
    val copySize: Long,
    val usageOffset: Long,
    val usageValue: Long,
    val capsOffset: Long,
    val capsCount: Long,
    val capsValue: Long,
    val refCount: Long,
    val ref0Offset: Long,
    val ref1Offset: Long,
    val ref2Offset: Long,
    val ref3Offset: Long,
    val ref0Image: Long,
    val ref1Image: Long,
    val ref2Image: Long,
    val ref3Image: Long,
)

internal data class KernelOffsetTable(
    val initTask: Long,
    val initCred: Long,
    val emptyZeroPage: Long,
    val rootTaskGroup: Long,
    val selinuxEnforcing: Long,
    val selinuxBlobSizes: Long,
    val securityHookHeads: Long,
    val slideNfulnlLogger: Long,
    val slideLoggers01: Long,
    val slideBootId: Long,
)

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

internal data class ExecutionTuning(
    val recommendedMainCpu: Long,
    val recommendedConsumerCpu: Long,
    val heapPrepareMaxAttempts: Long,
    val heapPrepareTimeoutMs: Long,
    val heapKernelsnitchTimeoutMs: Long,
    val raceRouteWaitMs: Long,
    val raceSetupSettleUs: Long,
    val raceStatePollIntervalUs: Long,
    val w1Attempts: Long,
    val w1SettleUs: Long,
    val w1ScratchRepairAttempts: Long,
    val w2Attempts: Long,
    val w2SettleUs: Long,
    val w3ChainRounds: Long,
    val w3Attempts: Long,
    val w3SettleUs: Long,
    val handoffPreDispatchSettleMs: Long,
    val handoffModulePollAttempts: Long,
    val handoffModulePollIntervalMs: Long,
    val handoffEnforcePollAttempts: Long,
    val handoffEnforcePollIntervalMs: Long,
)

/** Route-specific configuration; one subtype per route. */
internal sealed interface RouteConfig

internal object NoRouteConfig : RouteConfig

internal data class TcpConfig(
    val attempts: Long,
    val armSequence: Long,
    val postReceiveHoldIterations: Long,
) : RouteConfig

internal data class SelectConfig(
    val waiterShift: Long,
    val enterDelayUs: Long,
    val timeoutUs: Long,
    val consumerMaxCalls: Long,
    val consumerBurstCalls: Long,
) : RouteConfig

internal data class MulticastConfig(
    val geometry: MulticastGeometry,
    val fakeBssImageOffset: Long,
    val resident: Long,
    val readyTimeoutMs: Long,
    val postRequeueSettleUs: Long,
    val postAdjustSettleUs: Long,
) : RouteConfig
