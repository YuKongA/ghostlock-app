package com.ghostlock.app.core.victim

import java.security.MessageDigest
import java.util.zip.CRC32

/**
 * 通用 victim 控制协议 codec（backend-agnostic，批 A）。
 *
 * 线协议 v1（与 `app/src/main/cpp/gl43499/core/victim_protocol.h` 镜像；跨语言常量由
 * 单测对拍该头文件防漂移）：
 * ```
 * wire  = [u32le bodyLen][body]
 * body  = [u8 version][u8 kind][u16le payloadLen][payload][u32le crc32(version..payload)]
 * 命令 payload = [u8 opcode][args...]
 * 回复 payload = [u8 status][data...]
 * ```
 * - `bodyLen` 只由 Native 用于流边界/上限；Kotlin 仍完整校验以保持单一语义权威。
 * - 控制通道只承载小帧（≤ [FRAME_MAX_BODY]）；raw 数据走 memfd（[RawFrame]）。
 * - 协议不包含任何 backend/route/目标常量；opcode 只描述通用物理机制。
 *
 * @note 纯函数（无 I/O、无 Android、无 43499 常量），宿主可直接单测。
 * @note 阶段：批 A（task-52）；格式冻结 v1，后续只允许 append-only（新增 opcode/字段）。
 */
internal object VictimProtocol {
    /** 协议版本（帧头 version 字段）。 */
    const val VERSION: Int = 1

    /** 控制通道 fd（helper 读命令/写回复）。 */
    const val CONTROL_FD: Int = 3

    /** raw 通道 fd（共享 memfd；helper 写、父进程读）。 */
    const val RAW_FD: Int = 4

    /** 控制帧 body 上限（字节；超限拒绝，不截断）。 */
    const val FRAME_MAX_BODY: Int = 4096

    /** body 固定头（version+kind+payloadLen）。 */
    const val BODY_HEADER_BYTES: Int = 4

    /** body 尾部校验和字节数。 */
    const val CHECKSUM_BYTES: Int = 4

    /** body 中非 payload 的固定开销。 */
    const val BODY_OVERHEAD_BYTES: Int = BODY_HEADER_BYTES + CHECKSUM_BYTES

    /** payload 最大字节数。 */
    const val PAYLOAD_MAX_BYTES: Int = FRAME_MAX_BODY - BODY_OVERHEAD_BYTES

    /** 外层长度前缀字节数。 */
    const val LENGTH_PREFIX_BYTES: Int = 4

    /** 完整控制线帧最大字节数（长度前缀 + body）。 */
    const val WIRE_MAX_BYTES: Int = LENGTH_PREFIX_BYTES + FRAME_MAX_BODY

    /** raw 通道帧头字节数。 */
    const val RAW_HEADER_BYTES: Int = 16

    /** raw 通道帧魔数（'V','A','R','1' 小端 ⇒ 0x31524156；见 C 头镜像）。 */
    const val RAW_MAGIC: Int = 0x31524156

    /** raw 通道帧版本。 */
    const val RAW_VERSION: Int = 1

    /** raw 帧 flags bit0：数据被容量截断。 */
    const val RAW_FLAG_TRUNCATED: Int = 1

    /** 脚本路径最大字节数。 */
    const val MAX_SCRIPT_PATH_BYTES: Int = 512

    /** 单条 argv/env 项最大字节数。 */
    const val MAX_ARG_BYTES: Int = 512

    /** argv/env 条数上限。 */
    const val MAX_ARG_COUNT: Int = 16

    /** G04：PROBE_STATUS raw 上限（4 KiB；超限 helper IO err，不截断）。 */
    const val PROBE_STATUS_MAX_BYTES: Int = 4096

    /** G04：frame v1 单帧可回复 raw 上限（body max - 头/CRC - 1B status）。 */
    const val PROBE_STATUS_MAX_REPLY_BYTES: Int = PAYLOAD_MAX_BYTES - 1

    /** G04：PROBE_COMM marker/data 上限（15B）。 */
    const val PROBE_COMM_MAX_BYTES: Int = 15

    /** 帧 kind：命令（父→helper）。 */
    const val FRAME_KIND_COMMAND: Int = 1

    /** 帧 kind：回复（helper→父）。 */
    const val FRAME_KIND_REPLY: Int = 2

    /** 通用命令 opcode（机制命令；无业务语义）。 */
    enum class Opcode(val wire: Int) {
        /** 读回 helper 当前 uid。 */
        PROBE_UID(1),

        /** helper 内 perf self-sample（仅 syscall + raw 回写）。 */
        PERF_SELF_SAMPLE(2),

        /** helper 内 posix_spawn `/system/bin/sh <script>`（当前 uid）。 */
        EXEC_SCRIPT(3),

        /** rooted child 驻留（不杀）。 */
        PARK(4),

        /** helper 正常退出。 */
        BYE(5),

        /* G04/A2 append-only（不重排 1–5；frame v1 不变）。 */
        /** 读 helper self raw `/proc/self/status`（≤4 KiB）。 */
        PROBE_STATUS(6),

        /** 写 helper self comm marker（≤15B）。 */
        PROBE_COMM_SET(7),

        /** 读 helper self comm（len/c0/bytes）。 */
        PROBE_COMM(8),
    }

    /** 回复状态。 */
    enum class Status(val wire: Int) {
        /** 命令成功。 */
        OK(0),

        /** 命令失败（data 携带 [ErrorCode]+errno）。 */
        ERROR(1),
    }

    /** helper 错误码（回复 status=ERROR 时携带）。 */
    enum class ErrorCode(val wire: Int) {
        INVALID_FRAME(1),
        UNSUPPORTED(2),
        IO(3),
        SPAWN(4),
        PERF(5),
        EXEC(6),
        BAD_ARGS(7),
        TIMEOUT(8),
    }

    /**
     * 解码后的命令帧。
     *
     * @property opcode 命令。
     * @property args 原始参数区（由各命令 decoder 校验）。
     */
    data class Command(val opcode: Opcode, val args: ByteArray)

    /**
     * 解码后的回复帧。
     *
     * @property status 状态。
     * @property errorCode ERROR 时的错误码；OK 时为 null。
     * @property errorErrno ERROR 时的 helper errno；OK 时为 0。
     * @property data OK 时的数据区。
     */
    data class Reply(
        val status: Status,
        val errorCode: ErrorCode?,
        val errorErrno: Int,
        val data: ByteArray,
    )

    /**
     * perf self-sample 命令参数（全部由 Kotlin 决定；helper 只按值调用 syscall）。
     *
     * @property attrType `perf_event_attr.type`。
     * @property attrConfig `perf_event_attr.config`。
     * @property attrSamplePeriod `perf_event_attr.sample_period`。
     * @property attrSampleType `perf_event_attr.sample_type`。
     * @property attrSampleRegsIntr `perf_event_attr.sample_regs_intr`（寄存器掩码）。
     * @property attrFlags `perf_event_attr.flags`（只做机制位透传）。
     * @property ringPages 采样环数据页数（>0；mmap=(ringPages+1)*page）。
     * @property iterations enable 期间触发采样的 syscall 次数（>0）。
     * @property rawOffset raw memfd 内本次写入起点（≥0，16 对齐）。
     * @property rawCapacity raw memfd 内可用容量（≥[RAW_HEADER_BYTES]）。
     */
    data class PerfSampleRequest(
        val attrType: Long,
        val attrConfig: Long,
        val attrSamplePeriod: Long,
        val attrSampleType: Long,
        val attrSampleRegsIntr: Long,
        val attrFlags: Long,
        val ringPages: Long,
        val iterations: Long,
        val rawOffset: Long,
        val rawCapacity: Long,
    )

    /**
     * execScript 命令参数（helper 内 posix_spawn；不解释脚本内容）。
     *
     * @property scriptPath 脚本绝对路径（UTF-8；无换行/空白/单引号等协议破坏字符）。
     * @property argv 额外 argv（在 `/system/bin/sh <script>` 之后追加；可为空）。
     * @property env 额外环境变量（`KEY=VALUE`；为空表示继承 helper 环境）。
     * @property timeoutMs helper 等待脚本退出/回收的上限（毫秒；必须 >0）。
     */
    data class ExecScriptRequest(
        val scriptPath: String,
        val argv: List<String>,
        val env: List<String>,
        val timeoutMs: Long,
    )

    /**
     * 解码后的 raw 帧（memfd 内固定头 + 数据）。
     *
     * @property data 数据区（长度=头内 dataLen）。
     * @property truncated 头 flags bit0（helper 因容量截断）。
     */
    data class RawFrame(val data: ByteArray, val truncated: Boolean)

    /* ------------------------------------------------------------------ */
    /* 编码（父侧命令 + 测试用回复）                                        */
    /* ------------------------------------------------------------------ */

    /** @return `probeUid` 命令线帧。 */
    fun encodeProbeUid(): ByteArray = encodeWire(FRAME_KIND_COMMAND, byteArrayOf(Opcode.PROBE_UID.wire.toByte()))

    /** @return `park` 命令线帧。 */
    fun encodePark(): ByteArray = encodeWire(FRAME_KIND_COMMAND, byteArrayOf(Opcode.PARK.wire.toByte()))

    /** @return `bye` 命令线帧。 */
    fun encodeBye(): ByteArray = encodeWire(FRAME_KIND_COMMAND, byteArrayOf(Opcode.BYE.wire.toByte()))

    /* G04/A2 append-only：W3 self-probe（J/helper 路径；frame v1 不变）。 */

    /** @return `probeStatus` 命令线帧。 */
    fun encodeProbeStatus(): ByteArray =
        encodeWire(FRAME_KIND_COMMAND, byteArrayOf(Opcode.PROBE_STATUS.wire.toByte()))

    /** @return `probeComm` 命令线帧。 */
    fun encodeProbeComm(): ByteArray =
        encodeWire(FRAME_KIND_COMMAND, byteArrayOf(Opcode.PROBE_COMM.wire.toByte()))

    /**
     * @param marker 15B 内安全 ASCII（`[A-Za-z0-9._-]`）。
     * @return `probeCommSet` 命令线帧。
     * @throws IllegalArgumentException marker 非法（fail-closed，不编码）。
     */
    fun encodeProbeCommSet(marker: ByteArray): ByteArray {
        require(marker.isNotEmpty() && marker.size <= PROBE_COMM_MAX_BYTES) {
            "victim-protocol:probe-comm-marker"
        }
        require(marker.all { value ->
            val byte = value.toInt() and 0xFF
            byte in 0x30..0x39 || byte in 0x41..0x5A || byte in 0x61..0x7A ||
                byte == 0x2E || byte == 0x5F || byte == 0x2D
        }) { "victim-protocol:probe-comm-marker" }
        val payload = LeSink()
        payload.u8(Opcode.PROBE_COMM_SET.wire)
        payload.bytes(marker)
        return encodeWire(FRAME_KIND_COMMAND, payload.toByteArray())
    }

    /**
     * @param request perf 参数。
     * @return `perfSelfSample` 命令线帧。
     * @throws IllegalArgumentException 参数越界（fail-closed，不编码）。
     */
    fun encodePerfSelfSample(request: PerfSampleRequest): ByteArray {
        require(request.ringPages > 0L) { "victim-protocol:ring-pages" }
        require(request.iterations > 0L) { "victim-protocol:iterations" }
        require(request.rawOffset >= 0L && request.rawOffset % RAW_HEADER_BYTES == 0L) {
            "victim-protocol:raw-offset"
        }
        require(request.rawCapacity >= RAW_HEADER_BYTES.toLong()) {
            "victim-protocol:raw-capacity"
        }
        val args = LeSink()
        args.u64(request.attrType)
        args.u64(request.attrConfig)
        args.u64(request.attrSamplePeriod)
        args.u64(request.attrSampleType)
        args.u64(request.attrSampleRegsIntr)
        args.u64(request.attrFlags)
        args.u64(request.ringPages)
        args.u64(request.iterations)
        args.u64(request.rawOffset)
        args.u64(request.rawCapacity)
        val payload = LeSink()
        payload.u8(Opcode.PERF_SELF_SAMPLE.wire)
        payload.bytes(args.toByteArray())
        return encodeWire(FRAME_KIND_COMMAND, payload.toByteArray())
    }

    /**
     * @param request execScript 参数。
     * @return `execScript` 命令线帧。
     * @throws IllegalArgumentException 路径/参数越界（fail-closed）。
     */
    fun encodeExecScript(request: ExecScriptRequest): ByteArray {
        val path = request.scriptPath.toByteArray(Charsets.UTF_8)
        require(path.isNotEmpty() && path.size <= MAX_SCRIPT_PATH_BYTES) {
            "victim-protocol:script-path"
        }
        require(request.argv.size <= MAX_ARG_COUNT) { "victim-protocol:argv-count" }
        require(request.env.size <= MAX_ARG_COUNT) { "victim-protocol:env-count" }
        require(request.timeoutMs > 0L) { "victim-protocol:exec-timeout" }
        val encodedArgv = request.argv.map { item ->
            val bytes = item.toByteArray(Charsets.UTF_8)
            require(bytes.size <= MAX_ARG_BYTES) { "victim-protocol:argv-bytes" }
            bytes
        }
        val encodedEnv = request.env.map { item ->
            val bytes = item.toByteArray(Charsets.UTF_8)
            require(bytes.size <= MAX_ARG_BYTES) { "victim-protocol:env-bytes" }
            bytes
        }
        val payload = LeSink()
        payload.u8(Opcode.EXEC_SCRIPT.wire)
        payload.u16(path.size)
        payload.bytes(path)
        payload.u16(encodedArgv.size)
        encodedArgv.forEach { bytes ->
            payload.u16(bytes.size)
            payload.bytes(bytes)
        }
        payload.u16(encodedEnv.size)
        encodedEnv.forEach { bytes ->
            payload.u16(bytes.size)
            payload.bytes(bytes)
        }
        payload.u64(request.timeoutMs)
        return encodeWire(FRAME_KIND_COMMAND, payload.toByteArray())
    }

    /**
     * 编码回复帧（helper 侧格式的 Kotlin 镜像；供 fake helper/单测使用）。
     *
     * @param status 状态。
     * @param data OK 数据区。
     * @param errorCode ERROR 错误码。
     * @param errorErrno ERROR errno。
     * @return 回复线帧。
     */
    fun encodeReply(
        status: Status,
        data: ByteArray = ByteArray(0),
        errorCode: ErrorCode = ErrorCode.IO,
        errorErrno: Int = 0,
    ): ByteArray {
        val payload = LeSink()
        payload.u8(status.wire)
        when (status) {
            Status.OK -> payload.bytes(data)
            Status.ERROR -> {
                payload.u8(errorCode.wire)
                payload.u32(errorErrno.toLong() and 0xFFFFFFFFL)
            }
        }
        return encodeWire(FRAME_KIND_REPLY, payload.toByteArray())
    }

    /* ------------------------------------------------------------------ */
    /* 解码与校验                                                           */
    /* ------------------------------------------------------------------ */

    /**
     * 解码命令线帧（完整校验 version/kind/长度/crc/opcode）。
     *
     * @param wire 完整线帧（含外层长度前缀）。
     * @return Command 或具名失败。
     */
    fun decodeCommand(wire: ByteArray): VictimResult<Command> {
        val body = decodeBody(wire, FRAME_KIND_COMMAND)
            .let { result ->
                when (result) {
                    is VictimResult.Ok -> result.value
                    is VictimResult.Failed -> return result
                }
            }
        val source = LeSource(body.payload)
        val opcodeWire = source.u8()
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val opcode = Opcode.entries.firstOrNull { it.wire == opcodeWire }
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val args = if (source.remaining() == 0) {
            ByteArray(0)
        } else {
            source.take(source.remaining())
                ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        return VictimResult.Ok(Command(opcode, args))
    }

    /**
     * 解码回复线帧（完整校验）。
     *
     * @param wire 完整线帧（含外层长度前缀）。
     * @return Reply 或具名失败。
     */
    fun decodeReply(wire: ByteArray): VictimResult<Reply> {
        val body = decodeBody(wire, FRAME_KIND_REPLY)
            .let { result ->
                when (result) {
                    is VictimResult.Ok -> result.value
                    is VictimResult.Failed -> return result
                }
            }
        val source = LeSource(body.payload)
        val statusWire = source.u8()
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val status = Status.entries.firstOrNull { it.wire == statusWire }
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        return when (status) {
            Status.OK -> {
                val data = source.take(source.remaining())
                    ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
                VictimResult.Ok(Reply(Status.OK, null, 0, data))
            }
            Status.ERROR -> {
                val errorWire = source.u8()
                    ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
                val errorCode = ErrorCode.entries.firstOrNull { it.wire == errorWire }
                    ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
                val errno = source.u32()
                    ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
                if (source.remaining() != 0) {
                    return VictimResult.failed(VictimError.BAD_FRAME.token)
                }
                VictimResult.Ok(Reply(Status.ERROR, errorCode, errno.toInt(), ByteArray(0)))
            }
        }
    }

    /**
     * 解码 `perfSelfSample` 命令参数（与 [encodePerfSelfSample] 对拍）。
     *
     * @param args 命令参数区。
     * @return 参数对象或具名失败。
     */
    fun decodePerfSelfSampleArgs(args: ByteArray): VictimResult<PerfSampleRequest> {
        if (args.size != 80) return VictimResult.failed(VictimError.BAD_FRAME.token)
        val source = LeSource(args)
        val attrType = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val attrConfig = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val samplePeriod = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val sampleType = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val sampleRegsIntr = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val attrFlags = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val ringPages = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val iterations = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val rawOffset = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val rawCapacity = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (ringPages <= 0L || iterations <= 0L ||
            rawOffset < 0L || rawCapacity < RAW_HEADER_BYTES.toLong()
        ) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        return VictimResult.Ok(
            PerfSampleRequest(
                attrType = attrType,
                attrConfig = attrConfig,
                attrSamplePeriod = samplePeriod,
                attrSampleType = sampleType,
                attrSampleRegsIntr = sampleRegsIntr,
                attrFlags = attrFlags,
                ringPages = ringPages,
                iterations = iterations,
                rawOffset = rawOffset,
                rawCapacity = rawCapacity,
            ),
        )
    }

    /**
     * 解码 `execScript` 命令参数（与 [encodeExecScript] 对拍）。
     *
     * @param args 命令参数区。
     * @return 参数对象或具名失败。
     */
    fun decodeExecScriptArgs(args: ByteArray): VictimResult<ExecScriptRequest> {
        val source = LeSource(args)
        val pathLength = source.u16() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (pathLength == 0 || pathLength > MAX_SCRIPT_PATH_BYTES) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        val pathBytes = source.take(pathLength)
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val extraCount = source.u16() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (extraCount > MAX_ARG_COUNT) return VictimResult.failed(VictimError.BAD_FRAME.token)
        val extra = ArrayList<String>(extraCount)
        repeat(extraCount) {
            val length = source.u16() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
            if (length > MAX_ARG_BYTES) return VictimResult.failed(VictimError.BAD_FRAME.token)
            val bytes = source.take(length)
                ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
            extra.add(bytes.toString(Charsets.UTF_8))
        }
        val envCount = source.u16() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (envCount > MAX_ARG_COUNT) return VictimResult.failed(VictimError.BAD_FRAME.token)
        val env = ArrayList<String>(envCount)
        repeat(envCount) {
            val length = source.u16() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
            if (length > MAX_ARG_BYTES) return VictimResult.failed(VictimError.BAD_FRAME.token)
            val bytes = source.take(length)
                ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
            env.add(bytes.toString(Charsets.UTF_8))
        }
        val timeoutMs = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (source.remaining() != 0 || timeoutMs <= 0L) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        return VictimResult.Ok(
            ExecScriptRequest(
                scriptPath = pathBytes.toString(Charsets.UTF_8),
                argv = extra,
                env = env,
                timeoutMs = timeoutMs,
            ),
        )
    }

    /**
     * 解码 `probeUid` 回复数据。
     *
     * @param data OK 数据区。
     * @return uid 或具名失败。
     */
    fun decodeProbeUidReply(data: ByteArray): VictimResult<Long> {
        val source = LeSource(data)
        val uid = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (source.remaining() != 0) return VictimResult.failed(VictimError.BAD_FRAME.token)
        return VictimResult.Ok(uid)
    }

    /** G04：PROBE_COMM 回复（len/c0/bytes；helper 已去尾 `\n`/`\0`）。 */
    data class CommProbe(
        val length: Int,
        val firstByte: Int,
        val bytes: ByteArray,
    )

    /**
     * 解码 `probeStatus` 回复数据（raw `/proc/self/status`，≤4 KiB）。
     *
     * @param data OK 数据区。
     * @return raw 字节；超 4 KiB/空返回具名失败（不截断当证据）。
     */
    fun decodeProbeStatusReply(data: ByteArray): VictimResult<ByteArray> {
        if (data.isEmpty() || data.size > PROBE_STATUS_MAX_BYTES) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        return VictimResult.Ok(data)
    }

    /**
     * 解码 `probeComm` 回复数据（`[u8 len][u8 c0][len bytes]`）。
     *
     * @param data OK 数据区。
     * @return len/c0/bytes；格式非法或 len>15 返回具名失败。
     */
    fun decodeProbeCommReply(data: ByteArray): VictimResult<CommProbe> {
        if (data.size < 2) return VictimResult.failed(VictimError.BAD_FRAME.token)
        val length = data[0].toInt() and 0xFF
        val firstByte = data[1].toInt() and 0xFF
        if (length > PROBE_COMM_MAX_BYTES || data.size != 2 + length) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        return VictimResult.Ok(
            CommProbe(
                length = length,
                firstByte = firstByte,
                bytes = data.copyOfRange(2, data.size),
            ),
        )
    }

    /**
     * 解码 `perfSelfSample` 回复数据。
     *
     * @param data OK 数据区。
     * @return 物理回执（raw 偏移/长度/head/errno/flags）或具名失败。
     */
    fun decodePerfSelfSampleReply(data: ByteArray): VictimResult<PerfSampleReply> {
        val source = LeSource(data)
        val rawOffset = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val dataLen = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val dataHead = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val helperErrno = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val flags = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (source.remaining() != 0) return VictimResult.failed(VictimError.BAD_FRAME.token)
        return VictimResult.Ok(
            PerfSampleReply(
                rawOffset = rawOffset,
                dataLen = dataLen,
                dataHead = dataHead,
                helperErrno = helperErrno.toInt(),
                truncated = flags and RAW_FLAG_TRUNCATED.toLong() != 0L,
            ),
        )
    }

    /**
     * 解码 `execScript` 回复数据。
     *
     * @param data OK 数据区。
     * @return 执行回执或具名失败。
     */
    fun decodeExecScriptReply(data: ByteArray): VictimResult<ExecScriptReply> {
        val source = LeSource(data)
        val exitBits = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val outputLen = source.u64() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val truncated = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val timedOut = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val helperErrno = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (source.remaining() != 0) return VictimResult.failed(VictimError.BAD_FRAME.token)
        return VictimResult.Ok(
            ExecScriptReply(
                exitCode = exitBits.toInt(),
                outputLen = outputLen,
                truncated = truncated != 0L,
                timedOut = timedOut != 0L,
                helperErrno = helperErrno.toInt(),
            ),
        )
    }

    /**
     * 编码 raw 通道帧（头 + 数据）。
     *
     * @param data 数据区。
     * @param truncated 是否因容量截断。
     * @return raw 帧字节。
     */
    fun encodeRawFrame(data: ByteArray, truncated: Boolean = false): ByteArray {
        val sink = LeSink()
        sink.u32(RAW_MAGIC.toLong())
        sink.u16(RAW_VERSION)
        sink.u16(if (truncated) RAW_FLAG_TRUNCATED else 0)
        sink.u64(data.size.toLong())
        sink.bytes(data)
        return sink.toByteArray()
    }

    /**
     * `bye` 回执中的 E4 零 fork ① 计数（helper 链接单元 fork/vfork 累计）。
     *
     * @property fork `fork()` 调用次数（生产必须 0）。
     * @property vfork `vfork()` 调用次数（生产必须 0）。
     * @note 仅 debug/E4 证据；不是业务回执。
     */
    data class ForkGuardCounts(val fork: Long, val vfork: Long)

    /**
     * 解码 helper `bye` 回执中的 fork/vfork 计数（E4 零 fork ①）。
     *
     * @param data bye 回复 data（8 字节：u32 fork + u32 vfork）。
     * @return 计数；长度/格式不符 ⇒ BAD_FRAME。
     * @note helper 由 `-Wl,--wrap=fork,--wrap=vfork` 链接；posix_spawn 内部
     *   clone/vfork 不经过 fork/vfork 符号，故生产应恒为 (0,0)。
     */
    fun decodeForkGuardReply(data: ByteArray): VictimResult<ForkGuardCounts> {
        val source = LeSource(data)
        val fork = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val vfork = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (source.remaining() != 0) return VictimResult.failed(VictimError.BAD_FRAME.token)
        return VictimResult.Ok(ForkGuardCounts(fork = fork, vfork = vfork))
    }

    /**
     * 解码 raw 通道帧（魔法/版本/长度/截断位）。
     *
     * @param bytes 从 raw memfd 读回的字节（长度必须 ≥ 头 + dataLen）。
     * @param expectedDataLen 期望的数据长度；null=不校验（按头内长度）。
     * @return RawFrame 或具名失败。
     */
    fun decodeRawFrame(bytes: ByteArray, expectedDataLen: Long? = null): VictimResult<RawFrame> {
        if (bytes.size < RAW_HEADER_BYTES) {
            return VictimResult.failed(VictimError.RAW_INVALID.token)
        }
        val source = LeSource(bytes)
        val magic = source.u32() ?: return VictimResult.failed(VictimError.RAW_INVALID.token)
        val version = source.u16() ?: return VictimResult.failed(VictimError.RAW_INVALID.token)
        val flags = source.u16() ?: return VictimResult.failed(VictimError.RAW_INVALID.token)
        val dataLen = source.u64() ?: return VictimResult.failed(VictimError.RAW_INVALID.token)
        if (magic != RAW_MAGIC.toLong() || version != RAW_VERSION) {
            return VictimResult.failed(VictimError.RAW_INVALID.token)
        }
        if (dataLen < 0L || dataLen > Int.MAX_VALUE.toLong() - RAW_HEADER_BYTES) {
            return VictimResult.failed(VictimError.RAW_INVALID.token)
        }
        if (expectedDataLen != null && dataLen != expectedDataLen) {
            return VictimResult.failed(VictimError.RAW_INVALID.token)
        }
        if (RAW_HEADER_BYTES + dataLen.toInt() > bytes.size) {
            return VictimResult.failed(VictimError.RAW_INVALID.token)
        }
        val data = bytes.copyOfRange(RAW_HEADER_BYTES, RAW_HEADER_BYTES + dataLen.toInt())
        return VictimResult.Ok(
            RawFrame(
                data = data,
                truncated = flags.toLong() and RAW_FLAG_TRUNCATED.toLong() != 0L,
            ),
        )
    }

    /**
     * @param bytes 任意字节。
     * @return SHA-256 小写十六进制（64 字符）。
     */
    fun sha256Hex(bytes: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
        val builder = StringBuilder(digest.size * 2)
        digest.forEach { byte ->
            val value = byte.toInt() and 0xFF
            builder.append(HEX_CHARS[value ushr 4])
            builder.append(HEX_CHARS[value and 0x0F])
        }
        return builder.toString()
    }

    /* ------------------------------------------------------------------ */
    /* 帧编码基础                                                           */
    /* ------------------------------------------------------------------ */

    /** 解码后的帧 body。 */
    private data class Body(val kind: Int, val payload: ByteArray)

    /**
     * 编码线帧（外层长度前缀 + body）。
     *
     * @param kind 帧种类。
     * @param payload payload 区。
     * @return 完整线帧。
     * @throws IllegalArgumentException 超限（fail-closed，不截断）。
     */
    private fun encodeWire(kind: Int, payload: ByteArray): ByteArray {
        require(payload.size <= PAYLOAD_MAX_BYTES) { "victim-protocol:payload-size" }
        val body = LeSink()
        body.u8(VERSION)
        body.u8(kind)
        body.u16(payload.size)
        body.bytes(payload)
        val crcInput = body.toByteArray()
        body.u32(crc32(crcInput))
        val bodyBytes = body.toByteArray()
        val wire = LeSink()
        wire.u32(bodyBytes.size.toLong())
        wire.bytes(bodyBytes)
        return wire.toByteArray()
    }

    /**
     * 解码线帧公共部分（version/kind/长度/crc）。
     *
     * @param wire 完整线帧。
     * @param expectedKind 期望帧种类。
     * @return Body 或具名失败。
     */
    private fun decodeBody(wire: ByteArray, expectedKind: Int): VictimResult<Body> {
        val source = LeSource(wire)
        val bodyLen = source.u32() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (bodyLen < BODY_OVERHEAD_BYTES.toLong() || bodyLen > FRAME_MAX_BODY.toLong()) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        if (bodyLen != source.remaining().toLong()) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        val version = source.u8() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (version != VERSION) return VictimResult.failed(VictimError.BAD_FRAME.token)
        val kind = source.u8() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (kind != expectedKind) return VictimResult.failed(VictimError.BAD_FRAME.token)
        val payloadLen = source.u16() ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (payloadLen > PAYLOAD_MAX_BYTES) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        val payload = source.take(payloadLen)
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val expectedCrcBytes = source.take(CHECKSUM_BYTES)
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        val expectedCrc = LeSource(expectedCrcBytes).u32()
            ?: return VictimResult.failed(VictimError.BAD_FRAME.token)
        if (source.remaining() != 0) return VictimResult.failed(VictimError.BAD_FRAME.token)
        val crcInput = LeSink().apply {
            u8(version)
            u8(kind)
            u16(payloadLen)
            bytes(payload)
        }.toByteArray()
        if (crc32(crcInput) != expectedCrc) {
            return VictimResult.failed(VictimError.BAD_FRAME.token)
        }
        return VictimResult.Ok(Body(kind, payload))
    }

    /**
     * @param bytes 输入字节。
     * @return 标准 CRC32（IEEE）无符号值。
     */
    private fun crc32(bytes: ByteArray): Long {
        val crc = CRC32()
        crc.update(bytes)
        return crc.value and 0xFFFFFFFFL
    }

    /** 小端写器（帧编码用）。 */
    private class LeSink {
        private val out = java.io.ByteArrayOutputStream()

        fun u8(value: Int) {
            out.write(value and 0xFF)
        }

        fun u16(value: Int) {
            out.write(value and 0xFF)
            out.write((value ushr 8) and 0xFF)
        }

        fun u32(value: Long) {
            for (index in 0 until 4) out.write(((value ushr (8 * index)) and 0xFFL).toInt())
        }

        fun u64(value: Long) {
            for (index in 0 until 8) out.write(((value ushr (8 * index)) and 0xFFL).toInt())
        }

        fun bytes(value: ByteArray) {
            out.write(value)
        }

        fun toByteArray(): ByteArray = out.toByteArray()
    }

    /** 小端读器（帧解码用；越界返回 null，不抛异常）。 */
    private class LeSource(private val bytes: ByteArray) {
        private var offset = 0

        fun remaining(): Int = bytes.size - offset

        fun u8(): Int? {
            if (remaining() < 1) return null
            return bytes[offset++].toInt() and 0xFF
        }

        fun u16(): Int? {
            if (remaining() < 2) return null
            var value = 0
            for (index in 0 until 2) {
                value = value or ((bytes[offset + index].toInt() and 0xFF) shl (8 * index))
            }
            offset += 2
            return value
        }

        fun u32(): Long? {
            if (remaining() < 4) return null
            var value = 0L
            for (index in 0 until 4) {
                value = value or ((bytes[offset + index].toLong() and 0xFFL) shl (8 * index))
            }
            offset += 4
            return value
        }

        fun u64(): Long? {
            if (remaining() < 8) return null
            var value = 0L
            for (index in 0 until 8) {
                value = value or ((bytes[offset + index].toLong() and 0xFFL) shl (8 * index))
            }
            offset += 8
            return value
        }

        fun take(count: Int): ByteArray? {
            if (count < 0 || remaining() < count) return null
            val result = bytes.copyOfRange(offset, offset + count)
            offset += count
            return result
        }
    }

    /** SHA-256 十六进制字符集（小写）。 */
    private const val HEX_CHARS: String = "0123456789abcdef"

    /**
     * `perfSelfSample` 回复数据（机制事实）。
     *
     * @property rawOffset raw 写入起点（回显请求值）。
     * @property dataLen helper 实际写入 raw 的数据字节数。
     * @property dataHead helper 观察到的环 data_head（解析在 Kotlin）。
     * @property helperErrno 机制 errno（0=无）。
     * @property truncated helper 因 raw 容量截断。
     */
    data class PerfSampleReply(
        val rawOffset: Long,
        val dataLen: Long,
        val dataHead: Long,
        val helperErrno: Int,
        val truncated: Boolean,
    )

    /**
     * `execScript` 回复数据（机制事实）。
     *
     * @property exitCode 脚本退出码（信号时为 128+signal 的 shell 语义由 helper 回传）。
     * @property outputLen helper 写入 raw 的输出字节数。
     * @property truncated 输出因 raw 容量截断。
     * @property timedOut helper 有界等待到期并 kill 了脚本进程。
     * @property helperErrno 机制 errno（0=无）。
     */
    data class ExecScriptReply(
        val exitCode: Int,
        val outputLen: Long,
        val truncated: Boolean,
        val timedOut: Boolean,
        val helperErrno: Int,
    )
}
