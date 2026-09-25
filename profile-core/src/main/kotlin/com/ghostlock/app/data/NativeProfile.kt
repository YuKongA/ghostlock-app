package com.ghostlock.app.data

import com.ghostlock.app.data.route.NoRouteConfig
import com.ghostlock.app.data.route.RouteConfig
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.data.route.toConfigUInt
import com.ghostlock.app.data.route.toConfigULong
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Typed mirror of the native `struct kernel_offsets` (v2: fixed common
 * slots + a per-route section). Route-specific parameters live in [routeConfig],
 * not in the common document. The common slot order must match
 * `src/core/profile/binary.cpp` `kCommonFields`; route keys are owned by the
 * per-route [RouteConfig] subtype.
 *
 * Every field mirrors the native storage signedness: native `uint8_t`/`uint32_t`
 * slots are `UInt` and `uint64_t` slots are `ULong`, while the signed
 * `int32_t` slots ([com.ghostlock.app.data.route.SelectConfig.waiterShift],
 * [com.ghostlock.app.data.route.MulticastGeometry.waiterOff]) stay `Long`.
 */
data class NativeProfileDocument(
    val release: String,
    val routeKind: UInt,
    val kernelMajor: UInt,
    val recommendShizuku: UInt,
    val fallbackRoute: UInt,
    val taskStruct: TaskStructOffsets,
    val cred: CredTemplate,
    val kernelOffset: KernelOffsetTable,
    val kernelPhysLoad: ULong,
    val compactWaiter: UInt,
    val kernelsnitchCollisions: UInt,
    val mmStructSz: UInt,
    val execution: ExecutionTuning,
    val safeMode: UInt,
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
        buffer.putInt(Magic.toInt())
        buffer.putShort(Version.toShort())
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

    /**
     * v3 transport: `u32 magic + u16 version(3) + u16 frontend + u16 backend +
     * u16 middleware + u8 kernel_major + u8 fallback + u16 release_len + release
     * + 68×u64 core + u16 middleware_count + entries + u16 option_count +
     * entries`. `recommend_shizuku` is deliberately absent (App-only).
     */
    fun toBinaryV3(): ByteArray {
        val releaseBytes = release.toByteArray(Charsets.UTF_8)
        require(releaseBytes.size <= 0xffff) { "release is too long" }
        val core = flattenCommon()
        val middleware = routeConfig.entries()
        /* safe_mode travels only in the core common slot; writing it again in
         * options would let fromBinaryV3 override the patched core value. */
        val options = listOf(
            "selected_cpus.main" to execution.recommendedMainCpu.toLong(),
            "selected_cpus.consumer" to execution.recommendedConsumerCpu.toLong(),
            "race.route_done_timeout_ms" to execution.raceRouteDoneTimeoutMs.toLong(),
        )
        var size = HeaderSizeV3 + releaseBytes.size + core.size * 8 + 2
        for ((key, _) in middleware) size += 1 + key.toByteArray(Charsets.UTF_8).size + 8
        size += 2
        for ((key, _) in options) size += 1 + key.toByteArray(Charsets.UTF_8).size + 8

        val buffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(Magic.toInt())
        buffer.putShort(VersionV3.toShort())
        buffer.putShort(FrontendRootChild.toShort())
        buffer.putShort(BackendCve202643499.toShort())
        buffer.putShort(routeKind.toShort())
        buffer.put(kernelMajor.toByte())
        buffer.put(fallbackRoute.toByte())
        buffer.putShort(releaseBytes.size.toShort())
        buffer.put(releaseBytes)
        core.forEach(buffer::putLong)
        buffer.putShort(middleware.size.toShort())
        for ((key, value) in middleware) {
            val kb = key.toByteArray(Charsets.UTF_8)
            buffer.put(kb.size.toByte())
            buffer.put(kb)
            buffer.putLong(value)
        }
        buffer.putShort(options.size.toShort())
        for ((key, value) in options) {
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
            task.prio.toLong(), task.normalPrio.toLong(), task.schedTaskGroup.toLong(),
            task.piLock.toLong(), task.piWaiters.toLong(), task.piTopTask.toLong(),
            task.piBlockedOn.toLong(), task.pid.toLong(), task.tgid.toLong(),
            task.atomicFlags.toLong(), task.realCred.toLong(), task.cred.toLong(),
            task.comm.toLong(), task.tasks.toLong(), task.seccomp.toLong(),
            credential.copySize.toLong(), credential.usageOffset.toLong(),
            credential.usageValue.toLong(), credential.capsOffset.toLong(),
            credential.capsCount.toLong(), credential.capsValue.toLong(),
            credential.refCount.toLong(),
            credential.ref0Offset.toLong(), credential.ref1Offset.toLong(),
            credential.ref2Offset.toLong(), credential.ref3Offset.toLong(),
            credential.ref0Image.toLong(), credential.ref1Image.toLong(),
            credential.ref2Image.toLong(), credential.ref3Image.toLong(),
            offsets.initTask.toLong(), offsets.initCred.toLong(),
            offsets.emptyZeroPage.toLong(), offsets.rootTaskGroup.toLong(),
            offsets.selinuxEnforcing.toLong(), offsets.selinuxBlobSizes.toLong(),
            offsets.securityHookHeads.toLong(), offsets.slideNfulnlLogger.toLong(),
            offsets.slideLoggers01.toLong(), offsets.slideBootId.toLong(),
            kernelPhysLoad.toLong(), compactWaiter.toLong(),
            kernelsnitchCollisions.toLong(), mmStructSz.toLong(),
            exec.recommendedMainCpu.toLong(), exec.recommendedConsumerCpu.toLong(),
            exec.heapPrepareMaxAttempts.toLong(), exec.heapPrepareTimeoutMs.toLong(),
            exec.heapKernelsnitchTimeoutMs.toLong(), exec.raceRouteWaitMs.toLong(),
            exec.raceSetupSettleUs.toLong(), exec.raceStatePollIntervalUs.toLong(),
            exec.w1Attempts.toLong(), exec.w1SettleUs.toLong(),
            exec.w1ScratchRepairAttempts.toLong(), exec.w2Attempts.toLong(),
            exec.w2SettleUs.toLong(), exec.w3ChainRounds.toLong(),
            exec.w3Attempts.toLong(), exec.w3SettleUs.toLong(),
            exec.handoffPreDispatchSettleMs.toLong(), exec.handoffModulePollAttempts.toLong(),
            exec.handoffModulePollIntervalMs.toLong(), exec.handoffEnforcePollAttempts.toLong(),
            exec.handoffEnforcePollIntervalMs.toLong(),
            exec.consumerMaxCalls.toLong(), exec.consumerBurstCalls.toLong(),
            safeMode.toLong(),
        )
    }

    companion object {
        const val Magic = 0x0D000721u

        /** Legacy transport, still decoded. */
        const val Version: UShort = 2u

        /** Current writer version (core + middleware + options sections). */
        const val VersionV3: UShort = 3u
        private const val FrontendRootChild: UShort = 1u
        private const val FrontendUmhForward: UShort = 2u
        private const val BackendCve202643499: UShort = 1u
        private const val BackendCve20264560: UShort = 2u
        private const val HeaderSize = 12
        private const val HeaderSizeV3 = 16
        private const val CommonFieldCount = 68

        fun routeKind(route: String?): UInt = RouteKind.fromToken(route)?.wire ?: 0u

        /** Byte offset of the trailing common `safe_mode` slot in a v2 document,
         * or null when the blob is too short. The route section follows the
         * common slots, so the old `size - 16` shortcut no longer applies. */
        fun safeModeOffset(document: ByteArray): Int? {
            if (document.size < HeaderSize) return null
            val version = (document[4].toInt() and 0xff) or ((document[5].toInt() and 0xff) shl 8)
            val header = when (version) {
                2 -> HeaderSize
                3 -> HeaderSizeV3
                else -> return null
            }
            val releaseOffset = if (version == 2) 10 else 14
            if (document.size < releaseOffset + 2) return null
            val releaseLength =
                (document[releaseOffset].toInt() and 0xff) or
                    ((document[releaseOffset + 1].toInt() and 0xff) shl 8)
            val offset = header + releaseLength + (CommonFieldCount - 1) * 8
            return if (offset + 8 <= document.size) offset else null
        }

        fun fromBinary(bytes: ByteArray): NativeProfileDocument? {
            if (bytes.size < HeaderSize) return null
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            if (buffer.int.toUInt() != Magic) return null
            return when (buffer.short.toUShort()) {
                Version -> fromBinaryV2(bytes)
                VersionV3 -> fromBinaryV3(bytes)
                else -> null
            }
        }

        private fun fromBinaryV2(bytes: ByteArray): NativeProfileDocument? {
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            buffer.int // magic
            buffer.short // version
            val routeKind = (buffer.get().toInt() and 0xff).toUInt()
            val kernelMajor = (buffer.get().toInt() and 0xff).toUInt()
            val recommendShizuku = (buffer.get().toInt() and 0xff).toUInt()
            val fallbackRoute = (buffer.get().toInt() and 0xff).toUInt()
            val releaseLength = buffer.short.toInt() and 0xffff
            if (buffer.remaining() < releaseLength + CommonFieldCount * 8 + 1) return null
            val releaseBytes = ByteArray(releaseLength)
            buffer.get(releaseBytes)
            val common = LongArray(CommonFieldCount) { buffer.long }
            var routeConfig: RouteConfig =
                RouteKind.fromWire(routeKind)?.emptyConfig() ?: NoRouteConfig
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

        private fun fromBinaryV3(bytes: ByteArray): NativeProfileDocument? {
            if (bytes.size < HeaderSizeV3) return null
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            buffer.int // magic
            buffer.short // version
            val frontend = buffer.short.toInt() and 0xffff
            val backend = buffer.short.toInt() and 0xffff
            val middleware = buffer.short.toInt() and 0xffff
            /* Unknown ids are rejected here; known-but-unavailable ids (UMH,
             * cve_2026_64560) decode and are rejected by the orchestrator. */
            val frontendKnown = frontend == FrontendRootChild.toInt() ||
                frontend == FrontendUmhForward.toInt()
            val backendKnown = backend == BackendCve202643499.toInt() ||
                backend == BackendCve20264560.toInt()
            if (!frontendKnown || !backendKnown) return null
            if (RouteKind.fromWire(middleware.toUInt()) == null) return null
            val routeKind = middleware.toUInt()
            val kernelMajor = (buffer.get().toInt() and 0xff).toUInt()
            val fallbackRoute = (buffer.get().toInt() and 0xff).toUInt()
            val releaseLength = buffer.short.toInt() and 0xffff
            if (buffer.remaining() < releaseLength + CommonFieldCount * 8 + 2) return null
            val releaseBytes = ByteArray(releaseLength)
            buffer.get(releaseBytes)
            val common = LongArray(CommonFieldCount) { buffer.long }
            var routeConfig: RouteConfig =
                RouteKind.fromWire(routeKind)?.emptyConfig() ?: NoRouteConfig
            val middlewareCount = buffer.short.toInt() and 0xffff
            repeat(middlewareCount) {
                if (buffer.remaining() < 1) return null
                val keyLength = buffer.get().toInt() and 0xff
                if (buffer.remaining() < keyLength + 8) return null
                val keyBytes = ByteArray(keyLength)
                buffer.get(keyBytes)
                routeConfig = routeConfig.apply(String(keyBytes, Charsets.UTF_8), buffer.long)
            }
            val optionCount = buffer.short.toInt() and 0xffff
            var raceRouteDoneTimeoutMs = 0u
            repeat(optionCount) {
                if (buffer.remaining() < 1) return null
                val keyLength = buffer.get().toInt() and 0xff
                if (buffer.remaining() < keyLength + 8) return null
                val keyBytes = ByteArray(keyLength)
                buffer.get(keyBytes)
                val value = buffer.long
                when (String(keyBytes, Charsets.UTF_8)) {
                    "safe_mode" -> common[67] = value
                    "selected_cpus.main" -> common[44] = value
                    "selected_cpus.consumer" -> common[45] = value
                    "race.route_done_timeout_ms" -> raceRouteDoneTimeoutMs = value.toConfigUInt()
                }
            }
            val document = fromCommon(
                release = String(releaseBytes, Charsets.UTF_8),
                routeKind = routeKind,
                kernelMajor = kernelMajor,
                recommendShizuku = 0u,
                fallbackRoute = fallbackRoute,
                f = common,
                routeConfig = routeConfig,
            )
            return document.copy(
                execution = document.execution.copy(
                    raceRouteDoneTimeoutMs = raceRouteDoneTimeoutMs,
                ),
            )
        }

        /* Common slot indices mirror flattenCommon() one-to-one. */
        private fun fromCommon(
            release: String,
            routeKind: UInt,
            kernelMajor: UInt,
            recommendShizuku: UInt,
            fallbackRoute: UInt,
            f: LongArray,
            routeConfig: RouteConfig,
        ): NativeProfileDocument = NativeProfileDocument(
            release = release,
            routeKind = routeKind,
            kernelMajor = kernelMajor,
            recommendShizuku = recommendShizuku,
            fallbackRoute = fallbackRoute,
            taskStruct = TaskStructOffsets(
                prio = f[0].toUInt(), normalPrio = f[1].toUInt(), schedTaskGroup = f[2].toUInt(),
                piLock = f[3].toUInt(), piWaiters = f[4].toUInt(), piTopTask = f[5].toUInt(),
                piBlockedOn = f[6].toUInt(), pid = f[7].toUInt(), tgid = f[8].toUInt(),
                atomicFlags = f[9].toUInt(), realCred = f[10].toUInt(), cred = f[11].toUInt(),
                comm = f[12].toUInt(), tasks = f[13].toUInt(), seccomp = f[14].toUInt(),
            ),
            cred = CredTemplate(
                copySize = f[15].toUInt(), usageOffset = f[16].toUInt(),
                usageValue = f[17].toUInt(), capsOffset = f[18].toUInt(),
                capsCount = f[19].toUInt(), capsValue = f[20].toULong(),
                refCount = f[21].toUInt(), ref0Offset = f[22].toUInt(),
                ref1Offset = f[23].toUInt(), ref2Offset = f[24].toUInt(),
                ref3Offset = f[25].toUInt(), ref0Image = f[26].toULong(),
                ref1Image = f[27].toULong(), ref2Image = f[28].toULong(),
                ref3Image = f[29].toULong(),
            ),
            kernelOffset = KernelOffsetTable(
                initTask = f[30].toULong(), initCred = f[31].toULong(),
                emptyZeroPage = f[32].toULong(), rootTaskGroup = f[33].toULong(),
                selinuxEnforcing = f[34].toULong(), selinuxBlobSizes = f[35].toULong(),
                securityHookHeads = f[36].toULong(), slideNfulnlLogger = f[37].toULong(),
                slideLoggers01 = f[38].toULong(), slideBootId = f[39].toULong(),
            ),
            kernelPhysLoad = f[40].toULong(),
            compactWaiter = f[41].toUInt(),
            kernelsnitchCollisions = f[42].toUInt(),
            mmStructSz = f[43].toUInt(),
            execution = ExecutionTuning(
                recommendedMainCpu = f[44].toUInt(), recommendedConsumerCpu = f[45].toUInt(),
                heapPrepareMaxAttempts = f[46].toUInt(), heapPrepareTimeoutMs = f[47].toUInt(),
                heapKernelsnitchTimeoutMs = f[48].toUInt(), raceRouteWaitMs = f[49].toUInt(),
                raceRouteDoneTimeoutMs = 0u,
                raceSetupSettleUs = f[50].toUInt(), raceStatePollIntervalUs = f[51].toUInt(),
                w1Attempts = f[52].toUInt(), w1SettleUs = f[53].toUInt(),
                w1ScratchRepairAttempts = f[54].toUInt(), w2Attempts = f[55].toUInt(),
                w2SettleUs = f[56].toUInt(), w3ChainRounds = f[57].toUInt(),
                w3Attempts = f[58].toUInt(), w3SettleUs = f[59].toUInt(),
                handoffPreDispatchSettleMs = f[60].toUInt(),
                handoffModulePollAttempts = f[61].toUInt(),
                handoffModulePollIntervalMs = f[62].toUInt(),
                handoffEnforcePollAttempts = f[63].toUInt(),
                handoffEnforcePollIntervalMs = f[64].toUInt(),
                consumerMaxCalls = f[65].toUInt(),
                consumerBurstCalls = f[66].toUInt(),
            ),
            safeMode = f[67].toUInt(),
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
            fun vu(path: String): UInt = v(path).toConfigUInt()
            fun vul(path: String): ULong = v(path).toConfigULong()
            val routeConfig = RouteKind.fromToken(route)?.buildConfig(::v) ?: NoRouteConfig
            return NativeProfileDocument(
                release = release,
                routeKind = routeKind(route),
                kernelMajor = vu("kernel_major"),
                recommendShizuku = vu("recommend_shizuku"),
                fallbackRoute = routeKind(fallbackTo),
                taskStruct = TaskStructOffsets(
                    prio = vu("task_struct.prio"),
                    normalPrio = vu("task_struct.normal_prio"),
                    schedTaskGroup = vu("task_struct.sched_task_group"),
                    piLock = vu("task_struct.pi_lock"),
                    piWaiters = vu("task_struct.pi_waiters"),
                    piTopTask = vu("task_struct.pi_top_task"),
                    piBlockedOn = vu("task_struct.pi_blocked_on"),
                    pid = vu("task_struct.pid"),
                    tgid = vu("task_struct.tgid"),
                    atomicFlags = vu("task_struct.atomic_flags"),
                    realCred = vu("task_struct.real_cred"),
                    cred = vu("task_struct.cred"),
                    comm = vu("task_struct.comm"),
                    tasks = vu("task_struct.tasks"),
                    seccomp = vu("task_struct.seccomp"),
                ),
                cred = CredTemplate(
                    copySize = vu("cred.copy_size"),
                    usageOffset = vu("cred.usage_offset"),
                    usageValue = vu("cred.usage_value"),
                    capsOffset = vu("cred.caps_offset"),
                    capsCount = vu("cred.caps_count"),
                    capsValue = vul("cred.caps_value"),
                    refCount = vu("cred.ref_count"),
                    ref0Offset = vu("cred.ref0_offset"),
                    ref1Offset = vu("cred.ref1_offset"),
                    ref2Offset = vu("cred.ref2_offset"),
                    ref3Offset = vu("cred.ref3_offset"),
                    ref0Image = vul("cred.ref0_image"),
                    ref1Image = vul("cred.ref1_image"),
                    ref2Image = vul("cred.ref2_image"),
                    ref3Image = vul("cred.ref3_image"),
                ),
                kernelOffset = KernelOffsetTable(
                    initTask = vul("offset.init_task"),
                    initCred = vul("offset.init_cred"),
                    emptyZeroPage = vul("offset.empty_zero_page"),
                    rootTaskGroup = vul("offset.root_task_group"),
                    selinuxEnforcing = vul("offset.selinux_enforcing"),
                    selinuxBlobSizes = vul("offset.selinux_blob_sizes"),
                    securityHookHeads = vul("offset.security_hook_heads"),
                    slideNfulnlLogger = vul("offset.slide_nfulnl_logger"),
                    slideLoggers01 = vul("offset.slide_loggers_0_1"),
                    slideBootId = vul("offset.slide_boot_id"),
                ),
                kernelPhysLoad = vul("kernel_phys_load"),
                compactWaiter = vu("compact_waiter"),
                kernelsnitchCollisions = vu("kernelsnitch.collisions"),
                mmStructSz = vu("kernelsnitch.mm_struct_sz"),
                execution = ExecutionTuning(
                    recommendedMainCpu = vu("execution.recommended_cpus.main"),
                    recommendedConsumerCpu = vu("execution.recommended_cpus.consumer"),
                    heapPrepareMaxAttempts = vu("execution.heap.prepare_max_attempts"),
                    heapPrepareTimeoutMs = vu("execution.heap.prepare_timeout_ms"),
                    heapKernelsnitchTimeoutMs = vu("execution.heap.kernelsnitch_timeout_ms"),
                    raceRouteWaitMs = vu("execution.race.route_wait_ms"),
                    raceRouteDoneTimeoutMs = vu("execution.race.route_done_timeout_ms"),
                    raceSetupSettleUs = vu("execution.race.setup_settle_us"),
                    raceStatePollIntervalUs = vu("execution.race.state_poll_interval_us"),
                    w1Attempts = vu("execution.stages.w1_attempts"),
                    w1SettleUs = vu("execution.stages.w1_settle_us"),
                    w1ScratchRepairAttempts = vu("execution.stages.w1_scratch_repair_attempts"),
                    w2Attempts = vu("execution.stages.w2_attempts"),
                    w2SettleUs = vu("execution.stages.w2_settle_us"),
                    w3ChainRounds = vu("execution.stages.w3_chain_rounds"),
                    w3Attempts = vu("execution.stages.w3_attempts"),
                    w3SettleUs = vu("execution.stages.w3_settle_us"),
                    handoffPreDispatchSettleMs = vu("execution.handoff.pre_dispatch_settle_ms"),
                    handoffModulePollAttempts = vu("execution.handoff.module_poll_attempts"),
                    handoffModulePollIntervalMs = vu("execution.handoff.module_poll_interval_ms"),
                    handoffEnforcePollAttempts = vu("execution.handoff.enforce_poll_attempts"),
                    handoffEnforcePollIntervalMs = vu("execution.handoff.enforce_poll_interval_ms"),
                    consumerMaxCalls = vu("execution.routes.select_stack.consumer_max_calls"),
                    consumerBurstCalls = vu("execution.routes.select_stack.consumer_burst_calls"),
                ),
                safeMode = 0u,
                routeConfig = routeConfig,
            )
        }
    }
}

data class TaskStructOffsets(
    val prio: UInt,
    val normalPrio: UInt,
    val schedTaskGroup: UInt,
    val piLock: UInt,
    val piWaiters: UInt,
    val piTopTask: UInt,
    val piBlockedOn: UInt,
    val pid: UInt,
    val tgid: UInt,
    val atomicFlags: UInt,
    val realCred: UInt,
    val cred: UInt,
    val comm: UInt,
    val tasks: UInt,
    val seccomp: UInt,
)

data class CredTemplate(
    val copySize: UInt,
    val usageOffset: UInt,
    val usageValue: UInt,
    val capsOffset: UInt,
    val capsCount: UInt,
    val capsValue: ULong,
    val refCount: UInt,
    val ref0Offset: UInt,
    val ref1Offset: UInt,
    val ref2Offset: UInt,
    val ref3Offset: UInt,
    val ref0Image: ULong,
    val ref1Image: ULong,
    val ref2Image: ULong,
    val ref3Image: ULong,
)

data class KernelOffsetTable(
    val initTask: ULong,
    val initCred: ULong,
    val emptyZeroPage: ULong,
    val rootTaskGroup: ULong,
    val selinuxEnforcing: ULong,
    val selinuxBlobSizes: ULong,
    val securityHookHeads: ULong,
    val slideNfulnlLogger: ULong,
    val slideLoggers01: ULong,
    val slideBootId: ULong,
)

data class ExecutionTuning(
    val recommendedMainCpu: UInt,
    val recommendedConsumerCpu: UInt,
    val heapPrepareMaxAttempts: UInt,
    val heapPrepareTimeoutMs: UInt,
    val heapKernelsnitchTimeoutMs: UInt,
    val raceRouteWaitMs: UInt,
    val raceRouteDoneTimeoutMs: UInt,
    val raceSetupSettleUs: UInt,
    val raceStatePollIntervalUs: UInt,
    val w1Attempts: UInt,
    val w1SettleUs: UInt,
    val w1ScratchRepairAttempts: UInt,
    val w2Attempts: UInt,
    val w2SettleUs: UInt,
    val w3ChainRounds: UInt,
    val w3Attempts: UInt,
    val w3SettleUs: UInt,
    val handoffPreDispatchSettleMs: UInt,
    val handoffModulePollAttempts: UInt,
    val handoffModulePollIntervalMs: UInt,
    val handoffEnforcePollAttempts: UInt,
    val handoffEnforcePollIntervalMs: UInt,
    val consumerMaxCalls: UInt,
    val consumerBurstCalls: UInt,
)
