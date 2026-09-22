package com.ghostlock.app.data

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Typed mirror of the native `struct kernel_offsets` (binary layout v1).
 * The field order of [toBinary] must match `src/core/profile_binary.cpp`.
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
    val multicast: MulticastGeometry,
    val kernelPhysLoad: Long,
    val pselectWaiterShift: Long,
    val compactWaiter: Long,
    val kernelsnitchCollisions: Long,
    val mmStructSz: Long,
    val execution: ExecutionTuning,
) {
    fun toBinary(): ByteArray {
        val releaseBytes = release.toByteArray(Charsets.UTF_8)
        require(releaseBytes.size <= 0xffff) { "release is too long" }
        val fields = flatten()
        val buffer = ByteBuffer
            .allocate(HeaderSize + releaseBytes.size + fields.size * 8)
            .order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(Magic)
        buffer.putShort(Version)
        buffer.put(routeKind.toByte())
        buffer.put(kernelMajor.toByte())
        buffer.put(recommendShizuku.toByte())
        buffer.put(fallbackRoute.toByte())
        buffer.putShort(releaseBytes.size.toShort())
        buffer.put(releaseBytes)
        fields.forEach(buffer::putLong)
        return buffer.array()
    }

    private fun flatten(): LongArray {
        val task = taskStruct
        val credential = cred
        val offsets = kernelOffset
        val mcast = multicast
        val exec = execution
        return longArrayOf(
            /* task_struct */
            task.prio, task.normalPrio, task.schedTaskGroup, task.piLock,
            task.piWaiters, task.piTopTask, task.piBlockedOn, task.pid, task.tgid,
            task.atomicFlags, task.realCred, task.cred, task.comm, task.tasks,
            task.seccomp,
            /* cred */
            credential.copySize, credential.usageOffset, credential.usageValue,
            credential.capsOffset, credential.capsCount, credential.capsValue,
            credential.refCount,
            credential.ref0Offset, credential.ref1Offset, credential.ref2Offset,
            credential.ref3Offset, credential.ref0Image, credential.ref1Image,
            credential.ref2Image, credential.ref3Image,
            /* offset */
            offsets.initTask, offsets.initCred, offsets.emptyZeroPage,
            offsets.mcastFakeBss, offsets.rootTaskGroup, offsets.selinuxEnforcing,
            offsets.selinuxBlobSizes, offsets.securityHookHeads,
            offsets.slideNfulnlLogger, offsets.slideLoggers01, offsets.slideBootId,
            /* mcast */
            mcast.waiterOff, mcast.bufferSize, mcast.taskOffset, mcast.lockOffset,
            mcast.fakeLockOffset, mcast.fakeTaskOffset, mcast.lockSlotsOffset,
            mcast.lockSlotCount, mcast.lockSlotStride,
            /* misc */
            kernelPhysLoad, pselectWaiterShift, compactWaiter,
            kernelsnitchCollisions, mmStructSz,
            /* execution */
            exec.recommendedMainCpu, exec.recommendedConsumerCpu,
            exec.heapPrepareMaxAttempts, exec.heapPrepareTimeoutMs,
            exec.heapKernelsnitchTimeoutMs, exec.raceRouteWaitMs,
            exec.raceSetupSettleUs, exec.raceStatePollIntervalUs,
            exec.w1Attempts, exec.w1SettleUs, exec.w1ScratchRepairAttempts,
            exec.w2Attempts, exec.w2SettleUs, exec.w3ChainRounds,
            exec.w3Attempts, exec.w3SettleUs, exec.tcpAttempts,
            exec.tcpArmSequence, exec.tcpPostReceiveHoldIterations,
            exec.selectEnterDelayUs, exec.selectTimeoutUs,
            exec.selectConsumerMaxCalls, exec.selectConsumerBurstCalls,
            exec.multicastReadyTimeoutMs, exec.multicastPostRequeueSettleUs,
            exec.multicastPostAdjustSettleUs, exec.handoffPreDispatchSettleMs,
            exec.handoffModulePollAttempts, exec.handoffModulePollIntervalMs,
            exec.handoffEnforcePollAttempts, exec.handoffEnforcePollIntervalMs,
        )
    }

    companion object {
        const val Magic = 0x314B4C47
        const val Version: Short = 2
        private const val HeaderSize = 12
        private const val FieldCount = 86

        fun routeKind(route: String?): Int = when (route) {
            "tcp_zerocopy" -> 1
            "select_stack" -> 2
            "multicast_waiter" -> 3
            else -> 0
        }

        /** Decodes the GLK1 v2 byte layout; null on bad magic/version/size. */
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
            if (buffer.remaining() != releaseLength + FieldCount * 8) return null
            val releaseBytes = ByteArray(releaseLength)
            buffer.get(releaseBytes)
            val fields = LongArray(FieldCount) { buffer.long }
            return fromFields(
                release = String(releaseBytes, Charsets.UTF_8),
                routeKind = routeKind,
                kernelMajor = kernelMajor,
                recommendShizuku = recommendShizuku,
                fallbackRoute = fallbackRoute,
                fields = fields,
            )
        }

        /* Field indices mirror flatten() one-to-one. */
        private fun fromFields(
            release: String,
            routeKind: Int,
            kernelMajor: Long,
            recommendShizuku: Long,
            fallbackRoute: Int,
            fields: LongArray,
        ): NativeProfileDocument = NativeProfileDocument(
            release = release,
            routeKind = routeKind,
            kernelMajor = kernelMajor,
            recommendShizuku = recommendShizuku,
            fallbackRoute = fallbackRoute,
            taskStruct = TaskStructOffsets(
                prio = fields[0], normalPrio = fields[1], schedTaskGroup = fields[2],
                piLock = fields[3], piWaiters = fields[4], piTopTask = fields[5],
                piBlockedOn = fields[6], pid = fields[7], tgid = fields[8],
                atomicFlags = fields[9], realCred = fields[10], cred = fields[11],
                comm = fields[12], tasks = fields[13], seccomp = fields[14],
            ),
            cred = CredTemplate(
                copySize = fields[15], usageOffset = fields[16], usageValue = fields[17],
                capsOffset = fields[18], capsCount = fields[19], capsValue = fields[20],
                refCount = fields[21], ref0Offset = fields[22], ref1Offset = fields[23],
                ref2Offset = fields[24], ref3Offset = fields[25], ref0Image = fields[26],
                ref1Image = fields[27], ref2Image = fields[28], ref3Image = fields[29],
            ),
            kernelOffset = KernelOffsetTable(
                initTask = fields[30], initCred = fields[31], emptyZeroPage = fields[32],
                mcastFakeBss = fields[33], rootTaskGroup = fields[34],
                selinuxEnforcing = fields[35], selinuxBlobSizes = fields[36],
                securityHookHeads = fields[37], slideNfulnlLogger = fields[38],
                slideLoggers01 = fields[39], slideBootId = fields[40],
            ),
            multicast = MulticastGeometry(
                waiterOff = fields[41], bufferSize = fields[42], taskOffset = fields[43],
                lockOffset = fields[44], fakeLockOffset = fields[45],
                fakeTaskOffset = fields[46], lockSlotsOffset = fields[47],
                lockSlotCount = fields[48], lockSlotStride = fields[49],
            ),
            kernelPhysLoad = fields[50],
            pselectWaiterShift = fields[51],
            compactWaiter = fields[52],
            kernelsnitchCollisions = fields[53],
            mmStructSz = fields[54],
            execution = ExecutionTuning(
                recommendedMainCpu = fields[55],
                recommendedConsumerCpu = fields[56],
                heapPrepareMaxAttempts = fields[57],
                heapPrepareTimeoutMs = fields[58],
                heapKernelsnitchTimeoutMs = fields[59],
                raceRouteWaitMs = fields[60],
                raceSetupSettleUs = fields[61],
                raceStatePollIntervalUs = fields[62],
                w1Attempts = fields[63],
                w1SettleUs = fields[64],
                w1ScratchRepairAttempts = fields[65],
                w2Attempts = fields[66],
                w2SettleUs = fields[67],
                w3ChainRounds = fields[68],
                w3Attempts = fields[69],
                w3SettleUs = fields[70],
                tcpAttempts = fields[71],
                tcpArmSequence = fields[72],
                tcpPostReceiveHoldIterations = fields[73],
                selectEnterDelayUs = fields[74],
                selectTimeoutUs = fields[75],
                selectConsumerMaxCalls = fields[76],
                selectConsumerBurstCalls = fields[77],
                multicastReadyTimeoutMs = fields[78],
                multicastPostRequeueSettleUs = fields[79],
                multicastPostAdjustSettleUs = fields[80],
                handoffPreDispatchSettleMs = fields[81],
                handoffModulePollAttempts = fields[82],
                handoffModulePollIntervalMs = fields[83],
                handoffEnforcePollAttempts = fields[84],
                handoffEnforcePollIntervalMs = fields[85],
            ),
        )

        /** Builds the document from resolved profile values by dotted path. */
        fun from(
            release: String,
            route: String?,
            fallbackTo: String?,
            value: (String) -> Long?,
        ): NativeProfileDocument {
            fun v(path: String): Long = value(path) ?: 0L
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
                    mcastFakeBss = v("offset.mcast_fake_bss"),
                    rootTaskGroup = v("offset.root_task_group"),
                    selinuxEnforcing = v("offset.selinux_enforcing"),
                    selinuxBlobSizes = v("offset.selinux_blob_sizes"),
                    securityHookHeads = v("offset.security_hook_heads"),
                    slideNfulnlLogger = v("offset.slide_nfulnl_logger"),
                    slideLoggers01 = v("offset.slide_loggers_0_1"),
                    slideBootId = v("offset.slide_boot_id"),
                ),
                multicast = MulticastGeometry(
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
                kernelPhysLoad = v("kernel_phys_load"),
                pselectWaiterShift = v("pselect_waiter_shift"),
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
                    tcpAttempts = v("execution.routes.tcp_zerocopy.attempts"),
                    tcpArmSequence = v("execution.routes.tcp_zerocopy.arm_sequence"),
                    tcpPostReceiveHoldIterations =
                        v("execution.routes.tcp_zerocopy.post_receive_hold_iterations"),
                    selectEnterDelayUs = v("execution.routes.select_stack.enter_delay_us"),
                    selectTimeoutUs = v("execution.routes.select_stack.timeout_us"),
                    selectConsumerMaxCalls = v("execution.routes.select_stack.consumer_max_calls"),
                    selectConsumerBurstCalls =
                        v("execution.routes.select_stack.consumer_burst_calls"),
                    multicastReadyTimeoutMs =
                        v("execution.routes.multicast_waiter.ready_timeout_ms"),
                    multicastPostRequeueSettleUs =
                        v("execution.routes.multicast_waiter.post_requeue_settle_us"),
                    multicastPostAdjustSettleUs =
                        v("execution.routes.multicast_waiter.post_adjust_settle_us"),
                    handoffPreDispatchSettleMs = v("execution.handoff.pre_dispatch_settle_ms"),
                    handoffModulePollAttempts = v("execution.handoff.module_poll_attempts"),
                    handoffModulePollIntervalMs = v("execution.handoff.module_poll_interval_ms"),
                    handoffEnforcePollAttempts = v("execution.handoff.enforce_poll_attempts"),
                    handoffEnforcePollIntervalMs = v("execution.handoff.enforce_poll_interval_ms"),
                ),
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
    val mcastFakeBss: Long,
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
    val tcpAttempts: Long,
    val tcpArmSequence: Long,
    val tcpPostReceiveHoldIterations: Long,
    val selectEnterDelayUs: Long,
    val selectTimeoutUs: Long,
    val selectConsumerMaxCalls: Long,
    val selectConsumerBurstCalls: Long,
    val multicastReadyTimeoutMs: Long,
    val multicastPostRequeueSettleUs: Long,
    val multicastPostAdjustSettleUs: Long,
    val handoffPreDispatchSettleMs: Long,
    val handoffModulePollAttempts: Long,
    val handoffModulePollIntervalMs: Long,
    val handoffEnforcePollAttempts: Long,
    val handoffEnforcePollIntervalMs: Long,
)
