package com.ghostlock.app.core.victim

import java.util.ArrayDeque
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * VictimSession 单测（批 A，task-52）：能力门禁、命令往返、超时/EOF/子进程退出、
 * raw 长度/SHA/截断校验、park/bye/detachRooted、close 幂等与 handle/fd 泄漏=0（fake native）。
 *
 * @note 用 [VictimNative] fake 驱动；不启动真实 helper、不触 Android。
 */
class VictimSessionTest {
    @Test
    fun `start requires spawn capability and is one shot`() {
        val native = FakeNative()
        val session = VictimSession(
            native,
            config(capabilities = VictimCapabilities.of(VictimCapability.PROBE_UID)),
        )
        val denied = session.start()
        assertTrue(denied is VictimResult.Failed)
        assertEquals(0, native.spawns.size)

        val okNative = FakeNative()
        val okSession = VictimSession(okNative, config())
        val started = okSession.start()
        assertTrue(started is VictimResult.Ok)
        assertEquals(4_242, (started as VictimResult.Ok).value.pid)
        assertEquals(1, okNative.spawns.size)
        val second = okSession.start()
        assertTrue(second is VictimResult.Failed)
        okSession.close()
    }

    @Test
    fun `spawn failure leaks no handle`() {
        val native = FakeNative()
        native.spawnResult = VictimResult.failed(VictimError.SPAWN_FAILED.token)
        val session = VictimSession(native, config())
        val failed = session.start()
        assertTrue(failed is VictimResult.Failed)
        assertEquals(VictimError.SPAWN_FAILED.token, (failed as VictimResult.Failed).reason)
        session.close()
        assertEquals(0, native.closeCalls)
        assertEquals(0, native.detachCalls)
        assertEquals(0, native.killCalls)
    }

    @Test
    fun `probe uid roundtrip and failure mapping`() {
        val native = FakeNative()
        native.receiveQueue.add(VictimResult.Ok(probeReply(0L)))
        val session = VictimSession(native, config())
        assertTrue(session.start() is VictimResult.Ok)
        assertEquals(VictimResult.Ok(0L), session.probeUid())
        val command = (VictimProtocol.decodeCommand(native.sent.last()) as VictimResult.Ok).value
        assertEquals(VictimProtocol.Opcode.PROBE_UID, command.opcode)
        session.close()

        val timeoutNative = FakeNative()
        timeoutNative.receiveQueue.add(VictimResult.failed(VictimError.TIMEOUT.token))
        val timeoutSession = VictimSession(timeoutNative, config())
        timeoutSession.start()
        val timeout = timeoutSession.probeUid()
        assertTrue(timeout is VictimResult.Failed)
        assertEquals(VictimError.TIMEOUT.token, (timeout as VictimResult.Failed).reason)
        timeoutSession.close()

        val eofNative = FakeNative()
        eofNative.receiveQueue.add(VictimResult.failed(VictimError.EOF.token))
        val eofSession = VictimSession(eofNative, config())
        eofSession.start()
        val eof = eofSession.probeUid()
        assertTrue(eof is VictimResult.Failed)
        assertEquals(VictimError.EOF.token, (eof as VictimResult.Failed).reason)
        eofSession.close()
    }

    @Test
    fun `perf self sample reads raw frame and verifies sha`() {
        val raw = byteArrayOf(1, 2, 3, 4, 5)
        val native = FakeNative(rawSize = 1_024L)
        native.raw = VictimProtocol.encodeRawFrame(raw)
        native.receiveQueue.add(
            VictimResult.Ok(perfReply(rawOffset = 0L, dataLen = raw.size.toLong())),
        )
        val session = VictimSession(native, config())
        session.start()
        val request = perfRequest()
        val result = session.perfSelfSample(request)
        assertTrue(result is VictimResult.Ok)
        val sample = (result as VictimResult.Ok).value
        assertArrayEquals(raw, sample.rawBytes)
        assertEquals(VictimProtocol.sha256Hex(raw), sample.rawSha256)
        assertEquals(5L, sample.dataHead)
        assertEquals(1, native.readRawCalls)
        session.close()
    }

    @Test
    fun `perf self sample rejects truncated flag mismatch and bad window`() {
        val raw = byteArrayOf(1, 2, 3)
        val native = FakeNative(rawSize = 1_024L)
        /* reply 声称截断，但 raw 帧 flags 未置位 ⇒ 一致性失败。 */
        native.raw = VictimProtocol.encodeRawFrame(raw, truncated = false)
        native.receiveQueue.add(
            VictimResult.Ok(
                perfReply(rawOffset = 0L, dataLen = raw.size.toLong(), truncated = true),
            ),
        )
        val session = VictimSession(native, config())
        session.start()
        val mismatch = session.perfSelfSample(perfRequest())
        assertTrue(mismatch is VictimResult.Failed)
        assertTrue((mismatch as VictimResult.Failed).reason.startsWith(VictimError.RAW_INVALID.token))
        session.close()

        val windowNative = FakeNative(rawSize = 128L)
        val windowSession = VictimSession(windowNative, config(rawSize = 128L))
        windowSession.start()
        val badWindow = windowSession.perfSelfSample(
            perfRequest(rawCapacity = 1_024L),
        )
        assertTrue(badWindow is VictimResult.Failed)
        assertEquals(0, windowNative.sent.size)
        windowSession.close()
    }

    @Test
    fun `exec script returns exit code and output sha`() {
        val output = byteArrayOf(0x4f, 0x4b)
        val native = FakeNative(rawSize = 1_024L)
        native.raw = VictimProtocol.encodeRawFrame(output)
        native.receiveQueue.add(
            VictimResult.Ok(execReply(exitCode = 0, outputLen = output.size.toLong())),
        )
        val session = VictimSession(native, config())
        session.start()
        val result = session.execScript(
            VictimProtocol.ExecScriptRequest("/data/run.sh", emptyList(), emptyList(), 30_000L),
        )
        assertTrue(result is VictimResult.Ok)
        val executed = (result as VictimResult.Ok).value
        assertEquals(0, executed.exitCode)
        assertEquals(VictimProtocol.sha256Hex(output), executed.outputSha256)
        assertEquals(output.size.toLong(), executed.outputLength)
        assertFalse(executed.truncated)
        assertFalse(executed.timedOut)
        session.close()
    }

    @Test
    fun `exec script helper error and timed out pass through`() {
        val errorNative = FakeNative()
        errorNative.receiveQueue.add(
            VictimResult.Ok(
                VictimProtocol.encodeReply(
                    VictimProtocol.Status.ERROR,
                    errorCode = VictimProtocol.ErrorCode.EXEC,
                    errorErrno = 11,
                ),
            ),
        )
        val errorSession = VictimSession(errorNative, config())
        errorSession.start()
        val error = errorSession.execScript(
            VictimProtocol.ExecScriptRequest("/data/run.sh", emptyList(), emptyList(), 1_000L),
        )
        assertTrue(error is VictimResult.Failed)
        assertEquals(
            VictimError.HELPER_ERROR.token + ":exec:11",
            (error as VictimResult.Failed).reason,
        )
        errorSession.close()

        val timeoutNative = FakeNative(rawSize = 1_024L)
        timeoutNative.raw = VictimProtocol.encodeRawFrame(ByteArray(0))
        timeoutNative.receiveQueue.add(
            VictimResult.Ok(
                execReply(exitCode = 137, outputLen = 0L, timedOut = true),
            ),
        )
        val timeoutSession = VictimSession(timeoutNative, config())
        timeoutSession.start()
        val timedOut = timeoutSession.execScript(
            VictimProtocol.ExecScriptRequest("/data/run.sh", emptyList(), emptyList(), 1_000L),
        )
        assertTrue(timedOut is VictimResult.Ok)
        val executed = (timedOut as VictimResult.Ok).value
        assertTrue(executed.timedOut)
        assertEquals(137, executed.exitCode)
        timeoutSession.close()
    }

    @Test
    fun `capability gating blocks disabled commands`() {
        val native = FakeNative()
        val session = VictimSession(
            native,
            config(capabilities = VictimCapabilities.of(VictimCapability.SPAWN_HELPER)),
        )
        session.start()
        val probe = session.probeUid()
        assertTrue(probe is VictimResult.Failed)
        assertTrue((probe as VictimResult.Failed).reason.contains("capability-disabled"))
        val perf = session.perfSelfSample(perfRequest())
        assertTrue(perf is VictimResult.Failed)
        val exec = session.execScript(
            VictimProtocol.ExecScriptRequest("/a", emptyList(), emptyList(), 1_000L),
        )
        assertTrue(exec is VictimResult.Failed)
        session.close()
    }

    @Test
    fun `park then commands are rejected`() {
        val native = FakeNative()
        native.receiveQueue.add(
            VictimResult.Ok(VictimProtocol.encodeReply(VictimProtocol.Status.OK)),
        )
        val session = VictimSession(native, config())
        session.start()
        assertTrue(session.park() is VictimResult.Ok)
        assertEquals(VictimSessionState.PARKED, session.currentState)
        val probe = session.probeUid()
        assertTrue(probe is VictimResult.Failed)
        assertTrue((probe as VictimResult.Failed).reason.contains("parked"))
        session.close()
    }

    @Test
    fun `detach rooted parks without kill or close`() {
        val native = FakeNative()
        native.receiveQueue.add(
            VictimResult.Ok(VictimProtocol.encodeReply(VictimProtocol.Status.OK)),
        )
        val session = VictimSession(native, config())
        session.start()
        val detached = session.detachRooted()
        assertTrue(detached is VictimResult.Ok)
        assertEquals(VictimSessionState.DETACHED, session.currentState)
        assertEquals(1, native.detachCalls)
        assertEquals(0, native.killCalls)
        assertEquals(0, native.closeCalls)
        val probe = session.probeUid()
        assertTrue(probe is VictimResult.Failed)
        /* 再次 detach/close 幂等且不再触碰 native。 */
        assertTrue(session.detachRooted() is VictimResult.Ok)
        session.close()
        assertEquals(1, native.detachCalls)
        assertEquals(0, native.closeCalls)
    }

    @Test
    fun `close sends bye waits and closes native handle once`() {
        val native = FakeNative()
        native.receiveQueue.add(
            VictimResult.Ok(VictimProtocol.encodeReply(VictimProtocol.Status.OK)),
        )
        native.waits.add(VictimResult.Ok(0))
        val session = VictimSession(native, config())
        session.start()
        session.close()
        session.close()
        assertEquals(VictimSessionState.CLOSED, session.currentState)
        assertEquals(1, native.waitCalls)
        assertEquals(1, native.closeCalls)
        assertEquals(0, native.killCalls)
        assertTrue(
            native.sent.any {
                (VictimProtocol.decodeCommand(it) as VictimResult.Ok).value.opcode ==
                    VictimProtocol.Opcode.BYE
            },
        )
    }

    @Test
    fun `close kills when wait times out`() {
        val native = FakeNative()
        native.receiveQueue.add(
            VictimResult.Ok(VictimProtocol.encodeReply(VictimProtocol.Status.OK)),
        )
        native.waits.add(VictimResult.failed(VictimError.TIMEOUT.token))
        val session = VictimSession(native, config())
        session.start()
        session.close()
        assertEquals(1, native.killCalls)
        assertEquals(1, native.closeCalls)
    }

    @Test
    fun `identity reads uid and starttime`() {
        val native = FakeNative()
        native.receiveQueue.add(VictimResult.Ok(probeReply(0L)))
        val session = VictimSession(
            native,
            config(),
            identityReader = VictimIdentityReader { pid ->
                val fields = (1..25).joinToString(" ") { index ->
                    if (index == 20) "777" else if (index == 1) "S" else index.toString()
                }
                "$pid (comm with space) $fields"
            },
        )
        session.start()
        val identity = session.identity()
        assertTrue(identity is VictimResult.Ok)
        val value = (identity as VictimResult.Ok).value
        assertEquals(4_242, value.pid)
        assertEquals(0L, value.uid)
        assertEquals(777L, value.startTimeTicks)
        session.close()
    }

    /* ------------------------------------------------------------------ */
    /* 夹具                                                                 */
    /* ------------------------------------------------------------------ */

    /** 全能力配置（除 raw/perf 窗口外默认值仅用于单测）。 */
    private fun config(
        capabilities: VictimCapabilities = VictimCapabilities.of(
            *VictimCapability.entries.toTypedArray(),
        ),
        rawSize: Long = 1_024L,
    ): VictimSessionConfig = VictimSessionConfig(
        helperPath = "/data/app/lib/libghostlock_victim_helper.so",
        helperArgv = listOf("libghostlock_victim_helper.so"),
        helperEnv = emptyList(),
        capabilities = capabilities,
        rawSizeBytes = rawSize,
        controlTimeoutMs = 100L,
        commandTimeoutMs = 200L,
        closeTimeoutMs = 100L,
    )

    /** @return 默认 perf 请求（raw 窗口合法）。 */
    private fun perfRequest(rawCapacity: Long = 1_024L): VictimProtocol.PerfSampleRequest =
        VictimProtocol.PerfSampleRequest(
            attrType = 1L,
            attrConfig = 0L,
            attrSamplePeriod = 1_000L,
            attrSampleType = 0x40001L,
            attrSampleRegsIntr = 0xFFL,
            attrFlags = 8L,
            ringPages = 2L,
            iterations = 32L,
            rawOffset = 0L,
            rawCapacity = rawCapacity,
        )

    /** @return probeUid OK 回复线帧。 */
    private fun probeReply(uid: Long): ByteArray {
        val data = ByteArray(4)
        for (index in 0 until 4) data[index] = ((uid ushr (8 * index)) and 0xFF).toByte()
        return VictimProtocol.encodeReply(VictimProtocol.Status.OK, data)
    }

    /** @return perfSelfSample OK 回复线帧。 */
    private fun perfReply(
        rawOffset: Long,
        dataLen: Long,
        truncated: Boolean = false,
    ): ByteArray {
        val data = ByteArray(32)
        putLong(data, 0, rawOffset)
        putLong(data, 8, dataLen)
        putLong(data, 16, 5L)
        /* errno u32 @24；flags u32 @28。 */
        putInt(data, 24, 0)
        putInt(data, 28, if (truncated) 1 else 0)
        return VictimProtocol.encodeReply(VictimProtocol.Status.OK, data)
    }

    /** @return execScript OK 回复线帧。 */
    private fun execReply(
        exitCode: Int,
        outputLen: Long,
        timedOut: Boolean = false,
        truncated: Boolean = false,
    ): ByteArray {
        val data = ByteArray(24)
        putInt(data, 0, exitCode)
        putLong(data, 4, outputLen)
        putInt(data, 12, if (truncated) 1 else 0)
        putInt(data, 16, if (timedOut) 1 else 0)
        putInt(data, 20, 0)
        return VictimProtocol.encodeReply(VictimProtocol.Status.OK, data)
    }

    /** @param bytes 目标；@param offset 偏移；@param value 值（LE 4B）。 */
    private fun putInt(bytes: ByteArray, offset: Int, value: Int) {
        for (index in 0 until 4) {
            bytes[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    /** @param bytes 目标；@param offset 偏移；@param value 值（LE 8B）。 */
    private fun putLong(bytes: ByteArray, offset: Int, value: Long) {
        for (index in 0 until 8) {
            bytes[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    /** 记录调用的 [VictimNative] fake（不解析帧内容，只按脚本返回）。 */
    private class FakeNative(
        var rawSize: Long = 1_024L,
    ) : VictimNative {
        val spawns: MutableList<VictimSpawnRequest> = mutableListOf()
        val sent: MutableList<ByteArray> = mutableListOf()
        val receiveQueue: ArrayDeque<VictimResult<ByteArray>> = ArrayDeque()
        val waits: ArrayDeque<VictimResult<Int>> = ArrayDeque()
        var raw: ByteArray = ByteArray(0)
        var spawnResult: VictimResult<VictimSpawned> =
            VictimResult.Ok(VictimSpawned(handle = 77L, pid = 4_242))
        var sendResult: VictimResult<Unit> = VictimResult.Ok(Unit)
        var readRawCalls: Int = 0
        var waitCalls: Int = 0
        var killCalls: Int = 0
        var detachCalls: Int = 0
        var closeCalls: Int = 0

        override fun spawn(request: VictimSpawnRequest): VictimResult<VictimSpawned> {
            spawns += request
            return spawnResult
        }

        override fun send(
            handle: Long,
            frame: ByteArray,
            timeoutMs: Long,
        ): VictimResult<Unit> {
            sent += frame
            return sendResult
        }

        override fun receive(
            handle: Long,
            capacity: Int,
            timeoutMs: Long,
        ): VictimResult<ByteArray> =
            receiveQueue.pollFirst()
                ?: VictimResult.failed(VictimError.TIMEOUT.token)

        override fun readRaw(handle: Long, offset: Long, length: Long): VictimResult<ByteArray> {
            readRawCalls++
            if (offset < 0L || length < 0L ||
                offset + length > raw.size.toLong()
            ) {
                return VictimResult.failed(VictimError.RAW_READ_FAILED.token)
            }
            return VictimResult.Ok(
                raw.copyOfRange(offset.toInt(), (offset + length).toInt()),
            )
        }

        override fun rawSize(handle: Long): VictimResult<Long> = VictimResult.Ok(rawSize)

        override fun waitExit(handle: Long, timeoutMs: Long): VictimResult<Int> {
            waitCalls++
            return waits.pollFirst() ?: VictimResult.failed(VictimError.TIMEOUT.token)
        }

        override fun kill(handle: Long): VictimResult<Int> {
            killCalls++
            return VictimResult.Ok(137)
        }

        override fun detach(handle: Long) {
            detachCalls++
        }

        override fun close(handle: Long) {
            closeCalls++
        }
    }
}
