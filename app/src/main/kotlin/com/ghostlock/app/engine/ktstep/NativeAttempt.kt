package com.ghostlock.app.engine.ktstep

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** N1 的 Kotlin 侧 attempt 参数；C++ 只按 blob 执行。 */
data class NativeAttemptParams(
    val chainAddress: Long,
    val waitAddress: Long,
    val targetAddress: Long,
    val pageSinkAddress: Long,
    val chainValue: Int,
    val waitValue: Int,
    val targetValue: Int,
    val page: ByteArray,
    val pointerBytes: Int,
    val parkTimeoutNs: Long,
    val joinTimeoutMs: Int,
    val settleUs: Int,
    val waiterCpu: Int,
    val ownerCpu: Int,
    val requeueCpu: Int,
    val waiterNice: Int,
    val ownerNice: Int,
    val requeueNice: Int,
    val policy: Int,
    val pageFd: Int = -1,
)

sealed interface NativeAttemptResult {
    data class Ok(
        val waiterResult: Int,
        val waitResult: Int,
        val ownerResult: Int,
        val requeueResult: Int,
        val actorsStarted: Boolean,
        val pageStage: Int,
        val pageErrno: Int,
        val pageBytesWritten: Int,
        val joinTimedOut: Boolean,
        val elapsedNs: Long,
    ) : NativeAttemptResult

    data class Errno(val code: Int) : NativeAttemptResult
    data class Malformed(val reason: String) : NativeAttemptResult
}

data class NativeChildParams(
    val argv: List<String>,
    val commandTimeoutMs: Int,
    val maxPayload: Int,
)

/** N1 端口：未注入时所有调用 fail-closed。 */
interface NativeAttemptPort {
    fun open(): SyscallResult
    fun attempt(params: NativeAttemptParams): NativeAttemptResult
    fun spawnChild(params: NativeChildParams): SyscallResult
    fun childCommand(cmd: Int, payload: ByteArray): SyscallResult
    fun childFinish(action: Int): SyscallResult
    fun close(): SyscallResult
}

object UnavailableNativeAttemptPort : NativeAttemptPort {
    override fun open() = SyscallResult.Errno(KernelErrno.ENOSYS)
    override fun attempt(params: NativeAttemptParams) = NativeAttemptResult.Errno(KernelErrno.ENOSYS)
    override fun spawnChild(params: NativeChildParams) = SyscallResult.Errno(KernelErrno.ENOSYS)
    override fun childCommand(cmd: Int, payload: ByteArray) = SyscallResult.Errno(KernelErrno.ENOSYS)
    override fun childFinish(action: Int) = SyscallResult.Errno(KernelErrno.ENOSYS)
    override fun close() = SyscallResult.Errno(KernelErrno.ENOSYS)
}

/** v1 编码器：固定 112 字节头 + page；C++ 解析同一布局。 */
object NativeAttemptCodec {
    const val MAGIC = 0x474C4E50
    const val RESULT_MAGIC = 0x474C5253
    const val VERSION = 1
    const val HEADER_BYTES = 112
    const val RESULT_BYTES = 48
    const val MAX_PAGE_BYTES = 0x10000

    sealed interface Encoded { data class Ok(val bytes: ByteArray) : Encoded; data class Invalid(val reason: String) : Encoded }
    sealed interface Decoded { data class Ok(val params: NativeAttemptParams) : Decoded; data class Invalid(val reason: String) : Decoded }

    fun encode(params: NativeAttemptParams): Encoded {
        if (params.page.isEmpty() || params.page.size > MAX_PAGE_BYTES) return Encoded.Invalid("page-size")
        if (params.pointerBytes !in 1..8) return Encoded.Invalid("pointer-bytes")
        if (params.chainAddress == 0L || params.waitAddress == 0L || params.targetAddress == 0L ||
            params.pageSinkAddress == 0L
        ) return Encoded.Invalid("address")
        if (params.parkTimeoutNs != -1L && (params.parkTimeoutNs < 1L || params.parkTimeoutNs > 10_000_000_000L)) {
            return Encoded.Invalid("park-timeout")
        }
        if (params.joinTimeoutMs !in 1..10_000) return Encoded.Invalid("join-timeout")
        if (params.settleUs !in 0..1_000_000) return Encoded.Invalid("settle")
        if (params.waiterCpu !in 0..1023 || params.ownerCpu !in 0..1023 || params.requeueCpu !in 0..1023) {
            return Encoded.Invalid("cpu")
        }
        if (params.waiterNice !in -20..19 || params.ownerNice !in -20..19 || params.requeueNice !in -20..19) {
            return Encoded.Invalid("nice")
        }
        if (params.policy !in 0..6) return Encoded.Invalid("policy")
        if (params.pageFd < -1) return Encoded.Invalid("page-fd")
        val total = HEADER_BYTES + params.page.size
        val bytes = ByteArray(total)
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, MAGIC)
        buffer.putShort(4, VERSION.toShort())
        buffer.putShort(6, HEADER_BYTES.toShort())
        buffer.putInt(8, total)
        buffer.putInt(12, HEADER_BYTES)
        buffer.putInt(16, params.page.size)
        buffer.put(20, params.pointerBytes.toByte())
        buffer.put(21, 0)
        buffer.putShort(22, 0)
        buffer.putInt(24, params.chainValue)
        buffer.putInt(28, params.waitValue)
        buffer.putInt(32, params.targetValue)
        buffer.putLong(36, params.parkTimeoutNs)
        buffer.putInt(44, params.joinTimeoutMs)
        buffer.putInt(48, params.settleUs)
        buffer.putLong(52, params.chainAddress)
        buffer.putLong(60, params.waitAddress)
        buffer.putLong(68, params.targetAddress)
        buffer.putLong(76, params.pageSinkAddress)
        buffer.putShort(84, params.waiterCpu.toShort())
        buffer.putShort(86, params.ownerCpu.toShort())
        buffer.putShort(88, params.requeueCpu.toShort())
        buffer.putShort(90, 0)
        buffer.putInt(92, params.waiterNice)
        buffer.putInt(96, params.ownerNice)
        buffer.putInt(100, params.requeueNice)
        buffer.putInt(104, params.policy)
        buffer.putInt(108, params.pageFd)
        params.page.copyInto(bytes, HEADER_BYTES)
        return Encoded.Ok(bytes)
    }

    fun decode(bytes: ByteArray): Decoded {
        if (bytes.size < HEADER_BYTES) return Decoded.Invalid("short")
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (buffer.getInt(0) != MAGIC) return Decoded.Invalid("magic")
        if (buffer.getShort(4).toInt() != VERSION) return Decoded.Invalid("version")
        if (buffer.getShort(6).toInt() != HEADER_BYTES) return Decoded.Invalid("header")
        val total = buffer.getInt(8)
        if (total != bytes.size || total !in (HEADER_BYTES + 1)..(HEADER_BYTES + MAX_PAGE_BYTES)) {
            return Decoded.Invalid("total")
        }
        val pageOffset = buffer.getInt(12)
        val pageLength = buffer.getInt(16)
        if (pageOffset < HEADER_BYTES || pageOffset % 8 != 0 || pageLength !in 1..MAX_PAGE_BYTES) {
            return Decoded.Invalid("page")
        }
        if (pageOffset.toLong() + pageLength.toLong() != total.toLong()) return Decoded.Invalid("page-total")
        if (buffer.get(21).toInt() != 0) return Decoded.Invalid("flags")
        if (buffer.getShort(22).toInt() != 0 || buffer.getShort(90).toInt() != 0) {
            return Decoded.Invalid("reserved")
        }
        val pageFd = buffer.getInt(108)
        val pointer = buffer.get(20).toInt() and 0xFF
        if (pointer !in 1..8) return Decoded.Invalid("pointer-bytes")
        val park = buffer.getLong(36)
        if (park != -1L && (park < 1L || park > 10_000_000_000L)) return Decoded.Invalid("park-timeout")
        val join = buffer.getInt(44)
        if (join !in 1..10_000) return Decoded.Invalid("join-timeout")
        val settle = buffer.getInt(48)
        if (settle !in 0..1_000_000) return Decoded.Invalid("settle")
        val chain = buffer.getLong(52)
        val wait = buffer.getLong(60)
        val target = buffer.getLong(68)
        val sink = buffer.getLong(76)
        if (chain == 0L || wait == 0L || target == 0L || sink == 0L) return Decoded.Invalid("address")
        val waiterCpu = buffer.getShort(84).toInt() and 0xFFFF
        val ownerCpu = buffer.getShort(86).toInt() and 0xFFFF
        val requeueCpu = buffer.getShort(88).toInt() and 0xFFFF
        if (waiterCpu !in 0..1023 || ownerCpu !in 0..1023 || requeueCpu !in 0..1023) return Decoded.Invalid("cpu")
        val waiterNice = buffer.getInt(92)
        val ownerNice = buffer.getInt(96)
        val requeueNice = buffer.getInt(100)
        if (waiterNice !in -20..19 || ownerNice !in -20..19 || requeueNice !in -20..19) return Decoded.Invalid("nice")
        val policy = buffer.getInt(104)
        if (policy !in 0..6) return Decoded.Invalid("policy")
        val page = ByteArray(pageLength)
        bytes.copyInto(page, 0, pageOffset, pageOffset + pageLength)
        return Decoded.Ok(
            NativeAttemptParams(
                chainAddress = chain, waitAddress = wait, targetAddress = target, pageSinkAddress = sink,
                chainValue = buffer.getInt(24), waitValue = buffer.getInt(28), targetValue = buffer.getInt(32),
                page = page, pointerBytes = pointer, parkTimeoutNs = park, joinTimeoutMs = join,
                settleUs = settle, waiterCpu = waiterCpu, ownerCpu = ownerCpu, requeueCpu = requeueCpu,
                waiterNice = waiterNice, ownerNice = ownerNice, requeueNice = requeueNice, policy = policy,
                pageFd = pageFd,
            ),
        )
    }

    fun encodeResult(result: NativeAttemptResult): ByteArray? {
        if (result !is NativeAttemptResult.Ok) return null
        if (result.pageStage !in 0..2) return null
        val bytes = ByteArray(RESULT_BYTES)
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, RESULT_MAGIC)
        buffer.putShort(4, VERSION.toShort())
        buffer.putInt(8, result.waiterResult)
        buffer.putInt(12, result.waitResult)
        buffer.putInt(16, result.ownerResult)
        buffer.putInt(20, result.requeueResult)
        buffer.put(24, if (result.actorsStarted) 1 else 0)
        buffer.put(25, if (result.joinTimedOut) 1 else 0)
        buffer.put(26, result.pageStage.toByte())
        buffer.put(27, 0)
        buffer.putInt(28, result.pageErrno)
        buffer.putInt(32, result.pageBytesWritten)
        buffer.putInt(36, 0)
        buffer.putLong(40, result.elapsedNs)
        return bytes
    }

    fun decodeResult(bytes: ByteArray): NativeAttemptResult {
        if (bytes.size != RESULT_BYTES) return NativeAttemptResult.Malformed("size")
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (buffer.getInt(0) != RESULT_MAGIC) return NativeAttemptResult.Malformed("magic")
        if (buffer.getShort(4).toInt() != VERSION) return NativeAttemptResult.Malformed("version")
        val actors = buffer.get(24).toInt() and 0xFF
        if (actors !in 0..1) return NativeAttemptResult.Malformed("actors")
        val join = buffer.get(25).toInt() and 0xFF
        if (join !in 0..1) return NativeAttemptResult.Malformed("join")
        val stage = buffer.get(26).toInt() and 0xFF
        if (stage > 2) return NativeAttemptResult.Malformed("stage")
        return NativeAttemptResult.Ok(
            waiterResult = buffer.getInt(8),
            waitResult = buffer.getInt(12),
            ownerResult = buffer.getInt(16),
            requeueResult = buffer.getInt(20),
            actorsStarted = actors != 0,
            pageStage = stage,
            pageErrno = buffer.getInt(28),
            pageBytesWritten = buffer.getInt(32),
            joinTimedOut = (buffer.get(25).toInt() and 0xFF) != 0,
            elapsedNs = buffer.getLong(40),
        )
    }
}

/** N1 生产 writer 形状：模板来自 Kotlin，sink/page 由 PageWriter 覆盖。 */
class NativeKernelPageWriter(
    private val port: NativeAttemptPort,
    private val template: NativeAttemptParams,
) : PageWriter {
    override val kind: PageWriterKind = PageWriterKind.Production

    override fun write(sink: Long, page: ByteArray): PageWriteResult {
        if (page.isEmpty()) return PageWriteResult.Errno(KernelErrno.EINVAL)
        val params = template.copy(pageSinkAddress = sink, page = page)
        return when (val result = port.attempt(params)) {
            is NativeAttemptResult.Ok -> when {
                result.joinTimedOut -> PageWriteResult.Errno(KernelErrno.ETIMEDOUT)
                result.pageStage !in 1..2 -> PageWriteResult.Errno(KernelErrno.EINVAL)
                result.pageErrno != 0 -> PageWriteResult.Errno(result.pageErrno)
                result.pageBytesWritten !in 0..page.size -> PageWriteResult.Errno(KernelErrno.EINVAL)
                result.pageBytesWritten == page.size -> PageWriteResult.Written(page.size)
                else -> PageWriteResult.ShortWrote(result.pageBytesWritten.toLong())
            }
            is NativeAttemptResult.Errno -> PageWriteResult.Errno(result.code)
            is NativeAttemptResult.Malformed -> PageWriteResult.Errno(KernelErrno.EINVAL)
        }
    }
}
