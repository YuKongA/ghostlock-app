package com.ghostlock.app.data

import com.ghostlock.app.data.route.NoRouteConfig
import com.ghostlock.app.data.route.RouteConfig
import com.ghostlock.app.data.route.RouteKind
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Typed mirror of the native `struct kernel_offsets` (v2: fixed common
 * slots + a per-route section). Route-specific parameters live in [routeConfig],
 * not in the common document. The common slot order must match
 * `src/core/profile/binary.cpp` `kCommonFields`; route keys are owned by the
 * per-route [RouteConfig] subtype.
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
        val route = routeConfig.entries()
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
            exec.consumerMaxCalls, exec.consumerBurstCalls,
            safeMode,
        )
    }

    companion object {
        const val Magic = 0x0D000721
        const val Version: Short = 2
        private const val HeaderSize = 12
        private const val CommonFieldCount = 68

        fun routeKind(route: String?): Int = RouteKind.fromToken(route)?.wire ?: 0

        /** Byte offset of the trailing common `safe_mode` slot in a v2 document,
         * or null when the blob is too short. The route section follows the
         * common slots, so the old `size - 16` shortcut no longer applies. */
        fun safeModeOffset(document: ByteArray): Int? {
            if (document.size < HeaderSize) return null
            val releaseLength =
                (document[10].toInt() and 0xff) or ((document[11].toInt() and 0xff) shl 8)
            val offset = HeaderSize + releaseLength + (CommonFieldCount - 1) * 8
            return if (offset + 8 <= document.size) offset else null
        }

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
            var routeConfig: RouteConfig = RouteKind.fromWire(routeKind)?.emptyConfig() ?: NoRouteConfig
            val count = buffer.get().toInt() and 0xff
            repeat(count) {
                if (buffer.remaining() < 1) return null
                val keyLength = buffer.get().toInt() and 0xff
                if (buffer.remaining() < keyLength + 8) return null
                val keyBytes = ByteArray(keyLength)
                buffer.get(keyBytes)
                routeConfig = routeConfig.apply(String(keyBytes, Charsets.UTF_8), buffer.long)
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
                consumerMaxCalls = f[65],
                consumerBurstCalls = f[66],
            ),
            safeMode = f[67],
            routeConfig = routeConfig,
        )

        /** Builds the document from resolved profile values by dotted path. */
        fun from(
            release: String,
            route: String?,
            fallbackTo: String?,
            value: (String) -> Long?,
        ): NativeProfileDocument {
            fun v(path: String): Long = value(path) ?: 0L
            val routeConfig = RouteKind.fromToken(route)?.buildConfig(::v) ?: NoRouteConfig
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
                    consumerMaxCalls = v("execution.routes.select_stack.consumer_max_calls"),
                    consumerBurstCalls = v("execution.routes.select_stack.consumer_burst_calls"),
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
    val consumerMaxCalls: Long,
    val consumerBurstCalls: Long,
)
