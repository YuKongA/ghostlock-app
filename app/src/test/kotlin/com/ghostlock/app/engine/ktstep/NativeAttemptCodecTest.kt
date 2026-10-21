package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.assertNull
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class NativeAttemptCodecTest {
    private fun params(page: ByteArray = ByteArray(64) { it.toByte() }) = NativeAttemptParams(
        chainAddress = 0x1000L,
        waitAddress = 0x2000L,
        targetAddress = 0x3000L,
        pageSinkAddress = 0x4000L,
        chainValue = 0,
        waitValue = 0,
        targetValue = 0,
        page = page,
        pointerBytes = 8,
        parkTimeoutNs = -1L,
        joinTimeoutMs = 2000,
        settleUs = 500,
        waiterCpu = 0,
        ownerCpu = 1,
        requeueCpu = 2,
        waiterNice = 0,
        ownerNice = 0,
        requeueNice = 0,
        policy = 0,
    )

    private class Port(var answer: NativeAttemptResult) : NativeAttemptPort {
        var seen: NativeAttemptParams? = null
        override fun open() = SyscallResult.Ok(0L)
        override fun attempt(params: NativeAttemptParams): NativeAttemptResult {
            seen = params
            return answer
        }
        override fun spawnChild(params: NativeChildParams) = SyscallResult.Ok(0L)
        override fun childCommand(cmd: Int, payload: ByteArray) = SyscallResult.Ok(0L)
        override fun childFinish(action: Int) = SyscallResult.Ok(0L)
        override fun close() = SyscallResult.Ok(0L)
    }

    @Test
    fun paramsRoundTrip() {
        val source = params()
        val bytes = (NativeAttemptCodec.encode(source) as NativeAttemptCodec.Encoded.Ok).bytes
        val decoded = (NativeAttemptCodec.decode(bytes) as NativeAttemptCodec.Decoded.Ok).params

        assertEquals(source.chainAddress, decoded.chainAddress)
        assertEquals(source.waitAddress, decoded.waitAddress)
        assertEquals(source.targetAddress, decoded.targetAddress)
        assertEquals(source.pageSinkAddress, decoded.pageSinkAddress)
        assertEquals(source.pointerBytes, decoded.pointerBytes)
        assertEquals(source.chainValue, decoded.chainValue)
        assertEquals(source.waitValue, decoded.waitValue)
        assertEquals(source.targetValue, decoded.targetValue)
        assertEquals(source.parkTimeoutNs, decoded.parkTimeoutNs)
        assertEquals(source.joinTimeoutMs, decoded.joinTimeoutMs)
        assertEquals(source.settleUs, decoded.settleUs)
        assertEquals(source.waiterCpu, decoded.waiterCpu)
        assertEquals(source.ownerCpu, decoded.ownerCpu)
        assertEquals(source.requeueCpu, decoded.requeueCpu)
        assertEquals(source.waiterNice, decoded.waiterNice)
        assertEquals(source.ownerNice, decoded.ownerNice)
        assertEquals(source.requeueNice, decoded.requeueNice)
        assertEquals(source.policy, decoded.policy)
        assertTrue(source.page.contentEquals(decoded.page))
    }

    @Test
    fun invalidParamsAreNamed() {
        assertTrue(NativeAttemptCodec.encode(params(ByteArray(0))) is NativeAttemptCodec.Encoded.Invalid)
        assertTrue(NativeAttemptCodec.encode(params().copy(pointerBytes = 9)) is NativeAttemptCodec.Encoded.Invalid)
        assertTrue(NativeAttemptCodec.encode(params(ByteArray(0x10001))) is NativeAttemptCodec.Encoded.Invalid)
    }

    @Test
    fun malformedBlobsAreRejected() {
        val good = (NativeAttemptCodec.encode(params()) as NativeAttemptCodec.Encoded.Ok).bytes
        val badMagic = good.copyOf().also { it[0] = 0 }
        val badVersion = good.copyOf().also { it[4] = 2 }
        val badPage = good.copyOf().also { it[12] = 100 }
        val badAlign = good.copyOf().also {
            val buffer = ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN)
            buffer.putInt(12, 113)
            buffer.putInt(16, 63)
            buffer.putInt(8, 176)
        }
        val badTotal = good.copyOf().also { it[8] = 10 }
        val badTotalUpper = ByteArray(0x10071).also {
            val buffer = ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN)
            buffer.putInt(0, NativeAttemptCodec.MAGIC)
            buffer.putShort(4, NativeAttemptCodec.VERSION.toShort())
            buffer.putShort(6, NativeAttemptCodec.HEADER_BYTES.toShort())
            buffer.putInt(8, 0x10071)
            buffer.putInt(12, 0x10070)
            buffer.putInt(16, 1)
        }

        assertTrue(NativeAttemptCodec.decode(badMagic) is NativeAttemptCodec.Decoded.Invalid)
        assertTrue(NativeAttemptCodec.decode(badVersion) is NativeAttemptCodec.Decoded.Invalid)
        assertTrue(NativeAttemptCodec.decode(badPage) is NativeAttemptCodec.Decoded.Invalid)
        assertEquals("page", (NativeAttemptCodec.decode(badAlign) as NativeAttemptCodec.Decoded.Invalid).reason)
        assertTrue(NativeAttemptCodec.decode(badTotal) is NativeAttemptCodec.Decoded.Invalid)
        assertEquals("total", (NativeAttemptCodec.decode(badTotalUpper) as NativeAttemptCodec.Decoded.Invalid).reason)
        assertTrue(NativeAttemptCodec.decode(ByteArray(10)) is NativeAttemptCodec.Decoded.Invalid)
    }

    @Test
    fun resultRoundTrip() {
        val answer = NativeAttemptResult.Ok(0, 0, 0, 1, true, 2, 0, 64, false, 1234L)
        val bytes = NativeAttemptCodec.encodeResult(answer)!!
        assertEquals(answer, NativeAttemptCodec.decodeResult(bytes))
        assertTrue(NativeAttemptCodec.decodeResult(ByteArray(4)) is NativeAttemptResult.Malformed)
        val badActors = bytes.copyOf().also { it[24] = 2 }
        val badJoin = bytes.copyOf().also { it[25] = 2 }
        assertEquals(NativeAttemptResult.Malformed("actors"), NativeAttemptCodec.decodeResult(badActors))
        assertEquals(NativeAttemptResult.Malformed("join"), NativeAttemptCodec.decodeResult(badJoin))
    }

    @Test
    fun resultLayoutIsFixed() {
        val answer = NativeAttemptResult.Ok(1, 2, 3, 4, true, 2, 5, 64, false, 99L)
        val bytes = NativeAttemptCodec.encodeResult(answer)!!
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)

        assertEquals(48, bytes.size)
        assertEquals(NativeAttemptCodec.RESULT_MAGIC, buffer.getInt(0))
        assertEquals(1, buffer.get(24).toInt())
        assertEquals(0, buffer.get(25).toInt())
        assertEquals(2, buffer.get(26).toInt())
        assertEquals(5, buffer.getInt(28))
        assertEquals(64, buffer.getInt(32))
        assertEquals(99L, buffer.getLong(40))
        assertNull(NativeAttemptCodec.encodeResult(answer.copy(pageStage = 3)))
    }

    @Test
    fun theWriterOnlyReplacesSinkAndPage() {
        val port = Port(NativeAttemptResult.Ok(0, 0, 0, 1, true, 1, 0, 64, false, 1L))
        val writer = NativeKernelPageWriter(port, params())
        val page = ByteArray(64) { 0x5A }

        assertEquals(PageWriteResult.Written(64), writer.write(0x9000L, page))
        assertEquals(0x9000L, port.seen?.pageSinkAddress)
        assertTrue(page.contentEquals(port.seen!!.page))
        assertEquals(0x1000L, port.seen?.chainAddress)
        assertEquals(2000, port.seen?.joinTimeoutMs)
    }

    @Test
    fun shortAndErrnoResultsMap() {
        val short = NativeKernelPageWriter(
            Port(NativeAttemptResult.Ok(0, 0, 0, 0, true, 1, 0, 10, false, 1L)), params(),
        )
        assertEquals(PageWriteResult.ShortWrote(10), short.write(0x9000L, ByteArray(64)))

        val failed = NativeKernelPageWriter(
            Port(NativeAttemptResult.Ok(0, 0, 0, 0, true, 1, 5, 0, false, 1L)), params(),
        )
        assertEquals(PageWriteResult.Errno(5), failed.write(0x9000L, ByteArray(64)))
    }

    @Test
    fun writerRejectsStageZeroOversizeNegativeAndTimeout() {
        fun writerFor(result: NativeAttemptResult) = NativeKernelPageWriter(Port(result), params())
        val page = ByteArray(64)

        assertEquals(PageWriteResult.Errno(KernelErrno.EINVAL),
            writerFor(NativeAttemptResult.Ok(0, 0, 0, 0, true, 0, 0, 64, false, 1L)).write(0x9000L, page))
        assertEquals(PageWriteResult.Errno(KernelErrno.EINVAL),
            writerFor(NativeAttemptResult.Ok(0, 0, 0, 0, true, 3, 0, 64, false, 1L)).write(0x9000L, page))
        assertEquals(PageWriteResult.Errno(KernelErrno.EINVAL),
            writerFor(NativeAttemptResult.Ok(0, 0, 0, 0, true, 1, 0, 65, false, 1L)).write(0x9000L, page))
        assertEquals(PageWriteResult.Errno(KernelErrno.EINVAL),
            writerFor(NativeAttemptResult.Ok(0, 0, 0, 0, true, 1, 0, -1, false, 1L)).write(0x9000L, page))
        assertEquals(PageWriteResult.Errno(KernelErrno.ETIMEDOUT),
            writerFor(NativeAttemptResult.Ok(0, 0, 0, 0, true, 1, 0, 64, true, 1L)).write(0x9000L, page))
        assertEquals(PageWriteResult.Errno(5),
            writerFor(NativeAttemptResult.Errno(5)).write(0x9000L, page))
        assertEquals(PageWriteResult.Errno(KernelErrno.EINVAL),
            writerFor(NativeAttemptResult.Malformed("x")).write(0x9000L, page))
    }

    @Test
    fun theUnavailablePortFailsClosed() {
        val writer = NativeKernelPageWriter(UnavailableNativeAttemptPort, params())
        assertEquals(PageWriteResult.Errno(KernelErrno.ENOSYS), writer.write(0x9000L, ByteArray(64)))
        assertEquals(SyscallResult.Errno(KernelErrno.ENOSYS), UnavailableNativeAttemptPort.open())
        assertEquals(NativeAttemptResult.Errno(KernelErrno.ENOSYS),
            UnavailableNativeAttemptPort.attempt(params()))
        assertEquals(SyscallResult.Errno(KernelErrno.ENOSYS),
            UnavailableNativeAttemptPort.spawnChild(NativeChildParams(emptyList(), 1, 1)))
        assertEquals(SyscallResult.Errno(KernelErrno.ENOSYS),
            UnavailableNativeAttemptPort.childCommand(0, ByteArray(0)))
        assertEquals(SyscallResult.Errno(KernelErrno.ENOSYS), UnavailableNativeAttemptPort.childFinish(0))
        assertEquals(SyscallResult.Errno(KernelErrno.ENOSYS), UnavailableNativeAttemptPort.close())
    }
}
