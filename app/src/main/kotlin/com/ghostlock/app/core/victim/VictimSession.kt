package com.ghostlock.app.core.victim

/**
 * 通用 victim 会话配置（backend 声明；不可变）。
 *
 * @property helperPath `nativeLibraryDir` 内 helper PIE 绝对路径。
 * @property helperArgv helper 自身 argv（含 argv[0]；使用方/调试配置给出）。
 * @property helperEnv helper 环境变量（`KEY=VALUE`；空=继承）。
 * @property capabilities 启用能力集（未启用命令具名拒绝）。
 * @property rawSizeBytes raw memfd 容量（字节；≥ [VictimProtocol.RAW_HEADER_BYTES]）。
 * @property controlTimeoutMs 单条控制帧写/读的基础超时（毫秒；>0）。
 * @property commandTimeoutMs 机制命令（perfSelfSample 等）的额外等待上限（毫秒；>0）。
 * @property closeTimeoutMs bye 后等待退出/回收的上限（毫秒；>0）。
 * @note 所有超时/容量都显式给出，无默认目标常量。
 * @note 阶段：批 A（task-52）。
 */
internal data class VictimSessionConfig(
    val helperPath: String,
    val helperArgv: List<String>,
    val helperEnv: List<String>,
    val capabilities: VictimCapabilities,
    val rawSizeBytes: Long,
    val controlTimeoutMs: Long = 2_000L,
    val commandTimeoutMs: Long = 10_000L,
    val closeTimeoutMs: Long = 2_000L,
) {
    /**
     * @return null=自洽；否则具名失败原因。
     */
    fun validate(): String? = when {
        helperPath.isBlank() -> VictimError.INVALID_ARGUMENT.token + ":helper-path"
        helperArgv.isEmpty() -> VictimError.INVALID_ARGUMENT.token + ":helper-argv"
        rawSizeBytes < VictimProtocol.RAW_HEADER_BYTES.toLong() ->
            VictimError.INVALID_ARGUMENT.token + ":raw-size"
        controlTimeoutMs <= 0L || commandTimeoutMs <= 0L || closeTimeoutMs <= 0L ->
            VictimError.INVALID_ARGUMENT.token + ":timeout"
        else -> null
    }
}

/**
 * 会话状态（只表达机械生命周期，不是业务状态机）。
 *
 * @note RUNNING 之外的状态只允许 close/detach；命令一律具名拒绝。
 */
internal enum class VictimSessionState { NEW, RUNNING, PARKED, DETACHED, CLOSED }

/**
 * perf self-sample 的 Kotlin 侧结果（raw 字节 + SHA + 机制事实）。
 *
 * @property rawBytes helper 回写的 ring raw 字节（原样；解析由使用方 Kotlin 完成）。
 * @property rawSha256 raw 字节的 SHA-256（小写十六进制；完整性判据）。
 * @property dataHead helper 观察到的环 data_head（绝对字节位置）。
 * @property helperErrno helper 机制 errno（0=无）。
 * @property truncated helper 因 raw 容量截断。
 */
internal data class VictimPerfSample(
    val rawBytes: ByteArray,
    val rawSha256: String,
    val dataHead: Long,
    val helperErrno: Int,
    val truncated: Boolean,
)

/**
 * execScript 的 Kotlin 侧结果（退出码 + 输出摘要）。
 *
 * @property exitCode 脚本退出码。
 * @property outputSha256 输出字节 SHA-256（无输出时为 SHA-256(空)）。
 * @property outputLength 输出字节数（实际从 raw 读回）。
 * @property truncated 输出因 raw 容量截断（截断时仍返回已读摘要，由调用方判定）。
 * @property timedOut helper 有界等待到期并 kill 了脚本。
 * @property helperErrno helper 机制 errno（0=无）。
 */
internal data class VictimExecResult(
    val exitCode: Int,
    val outputSha256: String,
    val outputLength: Long,
    val truncated: Boolean,
    val timedOut: Boolean,
    val helperErrno: Int,
)

/**
 * 通用 victim 会话：进程生命周期 + 控制协议 + raw 传输（backend-agnostic）。
 *
 * 职责边界：
 * - 通过 [VictimNative] 完成 posix_spawn/控制帧 I/O/raw 读取/waitpid/reap；
 * - 本类只做机械编排：能力门禁、帧编解码调用、超时/状态结算、raw 长度/SHA 校验；
 * - **不做**业务判定（uid==0 的意义、perf 解析/投票、选址、finish 判定）；
 * - 不 import 任何 backend 包，也不含 route/43499 常量。
 *
 * 生命周期（沿 task-45 rooted 语义）：
 * - `start()` ⇒ RUNNING；`park()`/`bye()` 发命令；
 * - rooted 使用方应调 [VictimProcess.detachRooted]：发 park 后只 detach（不 kill/reap）；
 * - 非 rooted 路径调 [VictimProcess.close] 或 [close]：bye→有界 wait→超时 kill→native close；
 * - 同一会话只终结一次（幂等）。
 *
 * @param native Native transport seam（批 B 由 JNI 实现；批 A 单测用 fake）。
 * @param config 显式配置；[VictimSessionConfig.validate] 必须为 null。
 * @param identityReader 身份读取器（可注入 /proc stat seam）。
 * @note 阶段：批 A（task-52）；不接 Flow/JNI 生产路径。
 */
internal class VictimSession(
    private val native: VictimNative,
    private val config: VictimSessionConfig,
    private val identityReader: VictimIdentityReader = VictimIdentityReader(),
) {
    private val lock: Any = Any()
    private var state: VictimSessionState = VictimSessionState.NEW
    private var handle: Long = 0L
    private var pid: Int = 0

    init {
        config.validate()?.let { reason -> throw IllegalArgumentException(reason) }
    }

    /** @return 当前机械状态（诊断/测试用）。 */
    internal val currentState: VictimSessionState get() = synchronized(lock) { state }

    /**
     * posix_spawn helper 并返回进程视图。
     *
     * @return 进程视图，或具名失败。
     * @note 需要 [VictimCapability.SPAWN_HELPER]；重复调用具名拒绝。
     */
    fun start(): VictimResult<VictimProcess> = synchronized(lock) {
        if (state != VictimSessionState.NEW) {
            return VictimResult.failed(VictimError.NOT_STARTED.token)
        }
        val capabilityFailure = requireCapability(VictimCapability.SPAWN_HELPER)
        if (capabilityFailure != null) return VictimResult.failed(capabilityFailure)
        val spawned = native.spawn(
            VictimSpawnRequest(
                helperPath = config.helperPath,
                argv = config.helperArgv,
                env = config.helperEnv,
                rawSizeBytes = config.rawSizeBytes,
            ),
        )
        return when (spawned) {
            is VictimResult.Failed -> spawned
            is VictimResult.Ok -> {
                val value = spawned.value
                if (value.handle == 0L || value.pid <= 0) {
                    VictimResult.failed(VictimError.SPAWN_FAILED.token)
                } else {
                    handle = value.handle
                    pid = value.pid
                    state = VictimSessionState.RUNNING
                    VictimResult.Ok(VictimProcess(this, value.pid))
                }
            }
        }
    }

    /**
     * 发 `probeUid` 并读回 uid。
     *
     * @param timeoutMs 读超时（毫秒；≤0 时用配置 controlTimeout）。
     * @return uid，或具名失败。
     */
    fun probeUid(timeoutMs: Long = config.controlTimeoutMs): VictimResult<Long> =
        synchronized(lock) {
            val precheck = requireRunning(VictimCapability.PROBE_UID)
            if (precheck != null) return VictimResult.failed(precheck)
            val reply = roundTrip(VictimProtocol.encodeProbeUid(), timeoutMs)
            when (reply) {
                is VictimResult.Failed -> reply
                is VictimResult.Ok -> VictimProtocol.decodeProbeUidReply(reply.value.data)
            }
        }

    /**
     * helper 内 perf self-sample（仅机制 + raw 回写）。
     *
     * @param request 采样参数（ring/attr/迭代/raw 窗口；全部显式）。
     * @param timeoutMs 读超时（毫秒；≤0 时用配置 commandTimeout）。
     * @return raw 字节 + SHA + 机制事实，或具名失败。
     */
    fun perfSelfSample(
        request: VictimProtocol.PerfSampleRequest,
        timeoutMs: Long = config.commandTimeoutMs,
    ): VictimResult<VictimPerfSample> = synchronized(lock) {
        val precheck = requireRunning(VictimCapability.PERF_SELF_SAMPLE)
        if (precheck != null) return VictimResult.failed(precheck)
        val window = validateRawWindow(request.rawOffset, request.rawCapacity)
        if (window != null) return VictimResult.failed(window)
        val reply = roundTrip(
            frame = runCatching {
                VictimProtocol.encodePerfSelfSample(request)
            }.getOrElse { problem ->
                return VictimResult.failed(
                    VictimError.INVALID_ARGUMENT.token + ":" +
                        (problem.message ?: "perf-args"),
                )
            },
            timeoutMs = timeoutMs,
        )
        when (reply) {
            is VictimResult.Failed -> reply
            is VictimResult.Ok -> {
                when (val decoded = VictimProtocol.decodePerfSelfSampleReply(reply.value.data)) {
                    is VictimResult.Failed -> decoded
                    is VictimResult.Ok -> readPerfRaw(request, decoded.value)
                }
            }
        }
    }

    /**
     * helper 内 posix_spawn `/system/bin/sh <script>` 并读回退出码/输出摘要。
     *
     * @param request 脚本/argv/env/超时。
     * @param receiveTimeoutMs 控制读超时（毫秒；≤0 时用 request.timeoutMs+controlTimeout）。
     * @return 执行回执，或具名失败。
     * @note 输出固定写在 raw 区起点（offset=0，容量=rawSize）；不与非 exec 命令并发。
     */
    fun execScript(
        request: VictimProtocol.ExecScriptRequest,
        receiveTimeoutMs: Long = -1L,
    ): VictimResult<VictimExecResult> = synchronized(lock) {
        val precheck = requireRunning(VictimCapability.EXEC_SCRIPT)
        if (precheck != null) return VictimResult.failed(precheck)
        val window = validateRawWindow(0L, config.rawSizeBytes)
        if (window != null) return VictimResult.failed(window)
        val frame = runCatching {
            VictimProtocol.encodeExecScript(request)
        }.getOrElse { problem ->
            return VictimResult.failed(
                VictimError.INVALID_ARGUMENT.token + ":" +
                    (problem.message ?: "exec-args"),
            )
        }
        val boundedReceive = if (receiveTimeoutMs > 0L) {
            receiveTimeoutMs
        } else {
            request.timeoutMs + config.controlTimeoutMs
        }
        val reply = roundTrip(frame, boundedReceive)
        when (reply) {
            is VictimResult.Failed -> reply
            is VictimResult.Ok -> {
                when (val decoded = VictimProtocol.decodeExecScriptReply(reply.value.data)) {
                    is VictimResult.Failed -> decoded
                    is VictimResult.Ok -> readExecRaw(decoded.value)
                }
            }
        }
    }

    /**
     * 发 `park`：rooted child 驻留（不杀，不再处理后续命令）。
     *
     * @param timeoutMs 读超时（毫秒；≤0 时用配置 controlTimeout）。
     * @return Ok 或具名失败。
     */
    fun park(timeoutMs: Long = config.controlTimeoutMs): VictimResult<Unit> =
        synchronized(lock) {
            val precheck = requireRunning(VictimCapability.PARK)
            if (precheck != null) return VictimResult.failed(precheck)
            when (val reply = roundTrip(VictimProtocol.encodePark(), timeoutMs)) {
                is VictimResult.Failed -> reply
                is VictimResult.Ok -> {
                    if (reply.value.status == VictimProtocol.Status.OK) {
                        state = VictimSessionState.PARKED
                        VictimResult.Ok(Unit)
                    } else {
                        VictimResult.failed(helperErrorReason(reply.value))
                    }
                }
            }
        }

    /**
     * 发 `bye`（helper 正常退出；不在此等待退出，由 [close] 有界回收）。
     *
     * @param timeoutMs 读超时（毫秒；≤0 时用配置 controlTimeout）。
     * @return Ok 或具名失败。
     */
    fun bye(timeoutMs: Long = config.controlTimeoutMs): VictimResult<Unit> =
        synchronized(lock) {
            val precheck = requireRunning(VictimCapability.BYE)
            if (precheck != null) return VictimResult.failed(precheck)
            when (val reply = roundTrip(VictimProtocol.encodeBye(), timeoutMs)) {
                is VictimResult.Failed -> reply
                is VictimResult.Ok -> {
                    if (reply.value.status == VictimProtocol.Status.OK) {
                        VictimResult.Ok(Unit)
                    } else {
                        VictimResult.failed(helperErrorReason(reply.value))
                    }
                }
            }
        }

    /**
     * rooted child 的 detach：发 park（best-effort）后只关闭父侧传输，**不 kill/reap**。
     *
     * @param timeoutMs park 读超时（毫秒；≤0 时用配置 controlTimeout）。
     * @return Ok（已 detach）/ Failed（park 未确认，但仍已 detach，绝不 kill）。
     * @note 沿 task-45「rooted victim 不得被杀」；rooted 使用方必须走本方法而非 close。
     */
    fun detachRooted(timeoutMs: Long = config.controlTimeoutMs): VictimResult<Unit> =
        synchronized(lock) {
            if (state == VictimSessionState.CLOSED || state == VictimSessionState.DETACHED) {
                return VictimResult.Ok(Unit)
            }
            if (state != VictimSessionState.RUNNING && state != VictimSessionState.PARKED) {
                return VictimResult.failed(
                    VictimError.NOT_STARTED.token + ":" + state.name.lowercase(),
                )
            }
            val capabilityFailure = requireCapability(VictimCapability.DETACH_ROOTED)
            if (capabilityFailure != null) return VictimResult.failed(capabilityFailure)
            var parkFailure: String? = null
            if (state == VictimSessionState.RUNNING) {
                when (val parked = roundTrip(VictimProtocol.encodePark(), timeoutMs)) {
                    is VictimResult.Failed -> parkFailure = parked.reason
                    is VictimResult.Ok ->
                        if (parked.value.status != VictimProtocol.Status.OK) {
                            parkFailure = helperErrorReason(parked.value)
                        }
                }
            }
            val token = handle
            handle = 0L
            state = VictimSessionState.DETACHED
            if (token != 0L) native.detach(token)
            return if (parkFailure == null) {
                VictimResult.Ok(Unit)
            } else {
                VictimResult.failed(parkFailure)
            }
        }

    /**
     * 正常关闭：best-effort `bye` → 有界 wait → 超时 kill → native close（幂等）。
     *
     * @note detached/closed 时为 no-op；rooted 驻留路径应使用 [detachRooted]。
     */
    fun close() {
        synchronized(lock) {
            if (state == VictimSessionState.CLOSED || state == VictimSessionState.DETACHED) {
                state = if (state == VictimSessionState.DETACHED) {
                    VictimSessionState.DETACHED
                } else {
                    VictimSessionState.CLOSED
                }
                return
            }
            val token = handle
            handle = 0L
            state = VictimSessionState.CLOSED
            if (token == 0L) return
            /* best-effort bye：失败不改变收口（后续 kill/reap 兜底）。 */
            if (config.capabilities.contains(VictimCapability.BYE)) {
                runCatching { roundTripLocked(token, VictimProtocol.encodeBye(), config.controlTimeoutMs) }
            }
            val exited = native.waitExit(token, config.closeTimeoutMs)
            if (exited is VictimResult.Failed) {
                runCatching { native.kill(token) }
            }
            native.close(token)
        }
    }

    /**
     * 读取 victim 身份证据（PID + uid + starttime）。
     *
     * @return 身份证据，或具名失败。
     */
    fun identity(): VictimResult<VictimIdentity> = synchronized(lock) {
        if (state != VictimSessionState.RUNNING && state != VictimSessionState.PARKED) {
            return VictimResult.failed(VictimError.NOT_STARTED.token)
        }
        when (val uid = probeUidLocked()) {
            is VictimResult.Failed -> uid
            is VictimResult.Ok -> runCatching {
                identityReader.read(pid, uid.value)
            }.fold(
                onSuccess = { identity -> VictimResult.Ok(identity) },
                onFailure = { problem ->
                    VictimResult.failed<VictimIdentity>(
                        VictimError.IDENTITY_FAILED.token + ":" +
                            (problem.message ?: "read"),
                    )
                },
            )
        }
    }

    /* ------------------------------------------------------------------ */
    /* 内部：命令往返、raw 校验、错误映射                                    */
    /* ------------------------------------------------------------------ */

    /* G04/A2 append-only：W3 self-probe（既有 finish/park/bye/detach 语义不变）。 */

    /**
     * 读 helper self raw `/proc/self/status`（≤4 KiB；解析在调用方）。
     *
     * @return raw 字节或具名失败。
     */
    fun probeStatus(): VictimResult<ByteArray> {
        val capabilityFailure = requireCapability(VictimCapability.PROBE_STATUS)
        if (capabilityFailure != null) return VictimResult.failed(capabilityFailure)
        return when (val reply = roundTrip(VictimProtocol.encodeProbeStatus(), config.controlTimeoutMs)) {
            is VictimResult.Failed -> reply
            is VictimResult.Ok -> VictimProtocol.decodeProbeStatusReply(reply.value.data)
        }
    }

    /**
     * 写 helper self comm marker。
     *
     * @param marker 15B 内 `[A-Za-z0-9._-]`。
     * @return 成功或具名失败。
     */
    fun probeCommSet(marker: ByteArray): VictimResult<Unit> {
        val capabilityFailure = requireCapability(VictimCapability.PROBE_COMM_SET)
        if (capabilityFailure != null) return VictimResult.failed(capabilityFailure)
        val frame = try {
            VictimProtocol.encodeProbeCommSet(marker)
        } catch (problem: IllegalArgumentException) {
            return VictimResult.failed(VictimError.INVALID_ARGUMENT.token + ":probe-comm-marker")
        }
        return when (val reply = roundTrip(frame, config.controlTimeoutMs)) {
            is VictimResult.Failed -> reply
            is VictimResult.Ok -> VictimResult.Ok(Unit)
        }
    }

    /**
     * 读 helper self comm（len/c0/bytes）。
     *
     * @return 观测结果或具名失败。
     */
    fun probeComm(): VictimResult<VictimProtocol.CommProbe> {
        val capabilityFailure = requireCapability(VictimCapability.PROBE_COMM)
        if (capabilityFailure != null) return VictimResult.failed(capabilityFailure)
        return when (val reply = roundTrip(VictimProtocol.encodeProbeComm(), config.controlTimeoutMs)) {
            is VictimResult.Failed -> reply
            is VictimResult.Ok -> VictimProtocol.decodeProbeCommReply(reply.value.data)
        }
    }

    /**
     * 命令往返（调用方已持有锁）。
     *
     * @param frame 完整控制线帧。
     * @param timeoutMs 读超时（毫秒）。
     * @return 解码后的 Reply，或具名失败。
     */
    private fun roundTrip(frame: ByteArray, timeoutMs: Long): VictimResult<VictimProtocol.Reply> {
        val token = handle
        if (token == 0L) return VictimResult.failed(VictimError.NOT_STARTED.token)
        return roundTripLocked(token, frame, timeoutMs)
    }

    /**
     * @param token 非零 handle。
     * @param frame 完整控制线帧。
     * @param timeoutMs 读超时（毫秒）。
     * @return 解码后的 Reply，或具名失败。
     */
    private fun roundTripLocked(
        token: Long,
        frame: ByteArray,
        timeoutMs: Long,
    ): VictimResult<VictimProtocol.Reply> {
        val effectiveTimeout = if (timeoutMs > 0L) timeoutMs else config.controlTimeoutMs
        when (val sent = native.send(token, frame, effectiveTimeout)) {
            is VictimResult.Failed -> return sent
            is VictimResult.Ok -> Unit
        }
        val received = native.receive(token, VictimProtocol.WIRE_MAX_BYTES, effectiveTimeout)
        val wire = when (received) {
            is VictimResult.Failed -> return received
            is VictimResult.Ok -> received.value
        }
        return when (val reply = VictimProtocol.decodeReply(wire)) {
            is VictimResult.Failed -> reply
            is VictimResult.Ok -> {
                if (reply.value.status == VictimProtocol.Status.ERROR) {
                    VictimResult.failed(helperErrorReason(reply.value))
                } else {
                    reply
                }
            }
        }
    }

    /** @return uid 或具名失败（identity 内部用；调用方已持有锁）。 */
    private fun probeUidLocked(): VictimResult<Long> {
        val capabilityFailure = requireCapability(VictimCapability.PROBE_UID)
        if (capabilityFailure != null) return VictimResult.failed(capabilityFailure)
        return when (val reply = roundTrip(VictimProtocol.encodeProbeUid(), config.controlTimeoutMs)) {
            is VictimResult.Failed -> reply
            is VictimResult.Ok -> VictimProtocol.decodeProbeUidReply(reply.value.data)
        }
    }

    /**
     * 校验 raw 窗口在 memfd 容量内。
     *
     * @param offset 起点。
     * @param capacity 容量。
     * @return null=合法；否则具名原因。
     */
    private fun validateRawWindow(offset: Long, capacity: Long): String? {
        if (offset < 0L || offset % VictimProtocol.RAW_HEADER_BYTES != 0L) {
            return VictimError.INVALID_ARGUMENT.token + ":raw-offset"
        }
        if (capacity < VictimProtocol.RAW_HEADER_BYTES.toLong()) {
            return VictimError.INVALID_ARGUMENT.token + ":raw-capacity"
        }
        if (offset > config.rawSizeBytes - capacity) {
            return VictimError.INVALID_ARGUMENT.token + ":raw-window"
        }
        return null
    }

    /**
     * 读回 perf raw 数据并校验 raw 帧。
     *
     * @param request 原始请求（raw 窗口）。
     * @param reply helper 机制回执。
     * @return raw 字节+SHA，或具名失败。
     */
    private fun readPerfRaw(
        request: VictimProtocol.PerfSampleRequest,
        reply: VictimProtocol.PerfSampleReply,
    ): VictimResult<VictimPerfSample> {
        if (reply.rawOffset != request.rawOffset) {
            return VictimResult.failed(VictimError.RAW_INVALID.token + ":offset")
        }
        if (reply.dataLen < 0L || reply.dataLen > request.rawCapacity - VictimProtocol.RAW_HEADER_BYTES) {
            return VictimResult.failed(VictimError.RAW_INVALID.token + ":length")
        }
        val needed = VictimProtocol.RAW_HEADER_BYTES.toLong() + reply.dataLen
        val token = handle
        if (token == 0L) return VictimResult.failed(VictimError.NOT_STARTED.token)
        val bytes = when (val read = native.readRaw(token, request.rawOffset, needed)) {
            is VictimResult.Failed -> return read
            is VictimResult.Ok -> read.value
        }
        val frame = when (val decoded = VictimProtocol.decodeRawFrame(bytes, reply.dataLen)) {
            is VictimResult.Failed -> return decoded
            is VictimResult.Ok -> decoded.value
        }
        if (frame.truncated != reply.truncated) {
            return VictimResult.failed(VictimError.RAW_INVALID.token + ":truncated")
        }
        return VictimResult.Ok(
            VictimPerfSample(
                rawBytes = frame.data,
                rawSha256 = VictimProtocol.sha256Hex(frame.data),
                dataHead = reply.dataHead,
                helperErrno = reply.helperErrno,
                truncated = frame.truncated,
            ),
        )
    }

    /**
     * 读回 execScript 输出并校验 raw 帧。
     *
     * @param reply helper 执行回执。
     * @return 执行结果，或具名失败。
     */
    private fun readExecRaw(
        reply: VictimProtocol.ExecScriptReply,
    ): VictimResult<VictimExecResult> {
        val capacity = config.rawSizeBytes
        if (reply.outputLen < 0L || reply.outputLen > capacity - VictimProtocol.RAW_HEADER_BYTES) {
            return VictimResult.failed(VictimError.RAW_INVALID.token + ":length")
        }
        val needed = VictimProtocol.RAW_HEADER_BYTES.toLong() + reply.outputLen
        val token = handle
        if (token == 0L) return VictimResult.failed(VictimError.NOT_STARTED.token)
        val bytes = when (val read = native.readRaw(token, 0L, needed)) {
            is VictimResult.Failed -> return read
            is VictimResult.Ok -> read.value
        }
        val frame = when (val decoded = VictimProtocol.decodeRawFrame(bytes, reply.outputLen)) {
            is VictimResult.Failed -> return decoded
            is VictimResult.Ok -> decoded.value
        }
        if (frame.truncated != reply.truncated) {
            return VictimResult.failed(VictimError.RAW_INVALID.token + ":truncated")
        }
        return VictimResult.Ok(
            VictimExecResult(
                exitCode = reply.exitCode,
                outputSha256 = VictimProtocol.sha256Hex(frame.data),
                outputLength = reply.outputLen,
                truncated = frame.truncated,
                timedOut = reply.timedOut,
                helperErrno = reply.helperErrno,
            ),
        )
    }

    /**
     * @param capability 需要的能力。
     * @return null=已启用；否则具名原因。
     */
    private fun requireCapability(capability: VictimCapability): String? =
        if (config.capabilities.contains(capability)) {
            null
        } else {
            VictimError.CAPABILITY_DISABLED.token + ":" + capability.name.lowercase()
        }

    /**
     * @param capability 命令需要的能力。
     * @return null=RUNNING 且能力已启用；否则具名原因。
     */
    private fun requireRunning(capability: VictimCapability): String? {
        if (state != VictimSessionState.RUNNING) {
            return if (state == VictimSessionState.PARKED) {
                VictimError.NOT_STARTED.token + ":parked"
            } else {
                VictimError.NOT_STARTED.token + ":" + state.name.lowercase()
            }
        }
        return requireCapability(capability)
    }

    /**
     * @param reply helper ERROR 回复。
     * @return 具名原因（含 helper error code/errno）。
     */
    private fun helperErrorReason(reply: VictimProtocol.Reply): String {
        val code = reply.errorCode?.name?.lowercase() ?: "unknown"
        val errno = reply.errorErrno
        return VictimError.HELPER_ERROR.token + ":" + code + ":" + errno
    }
}

/**
 * 通用 victim 进程视图（会话持有的机械资源的命令面）。
 *
 * @property pid helper PID（>0）。
 * @note 不拥有会话：close/detach 语义见对应方法；重复 close 幂等。
 * @note 阶段：批 A（task-52）。
 */
internal class VictimProcess internal constructor(
    private val session: VictimSession,
    val pid: Int,
) {
    /** @return 机械状态（诊断/测试用）。 */
    internal val state: VictimSessionState get() = session.currentState

    /** @return 身份证据（PID/uid/starttime），或具名失败。 */
    fun identity(): VictimResult<VictimIdentity> = session.identity()

    /** @return uid，或具名失败。 */
    fun probeUid(): VictimResult<Long> = session.probeUid()

    /**
     * @param request 采样参数。
     * @return raw+SHA+机制事实，或具名失败。
     */
    fun perfSelfSample(
        request: VictimProtocol.PerfSampleRequest,
    ): VictimResult<VictimPerfSample> = session.perfSelfSample(request)

    /**
     * @param request 脚本参数。
     * @return 执行回执，或具名失败。
     */
    fun execScript(
        request: VictimProtocol.ExecScriptRequest,
    ): VictimResult<VictimExecResult> = session.execScript(request)

    /** @return Ok 或具名失败。 */
    fun park(): VictimResult<Unit> = session.park()

    /** @return Ok 或具名失败。 */
    fun bye(): VictimResult<Unit> = session.bye()

    /**
     * rooted child：发 park 后只 detach（不 kill/reap）。
     *
     * @return Ok（已 detach）/ Failed（park 未确认但已 detach）。
     */
    fun detachRooted(): VictimResult<Unit> = session.detachRooted()

    /** 正常关闭（bye→有界 wait→超时 kill→native close；幂等）。 */
    fun close() = session.close()
}
