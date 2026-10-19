package com.ghostlock.app.core.access

import com.ghostlock.app.core.AccessResult
import java.io.FileDescriptor
import java.nio.ByteBuffer
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AccessCapabilityTest {

    /** Fake glk transport: records requests, simulates PING/READ/WRITE/UNLOAD. */
    private class FakeTransport : GlkTransport {
        val requests = mutableListOf<GlkRequest>()
        var nextStatus = 0

        override fun ioctl(fd: FileDescriptor, request: ByteBuffer, data: ByteBuffer?): Int {
            val decoded = GlkRequest.decode(request)
            requests += decoded
            if (decoded.op == GlkRequest.OP_READ && data != null) {
                data.position(0)
                for (i in 0 until minOf(8, data.capacity())) data.put(i, (i + 1).toByte())
            }
            request.position(24)
            request.putInt(nextStatus)
            return if (nextStatus == 0) 0 else -1
        }
    }

    private fun descriptor(): FileDescriptor = FileDescriptor()

    @Test
    fun requestRoundTripKeepsWireLayout() {
        val encoded = GlkRequest(
            op = GlkRequest.OP_READ,
            address = 0x1122334455667788L,
            value = 0x99aabbccddeeff00uL.toLong(),
            length = 4096,
        ).encode()
        encoded.position(0)
        val decoded = GlkRequest.decode(encoded)
        assertEquals(GlkRequest.GLK_ABI_VERSION, decoded.abiVersion)
        assertEquals(GlkRequest.OP_READ, decoded.op)
        assertEquals(0x1122334455667788L, decoded.address)
        assertEquals(0x99aabbccddeeff00uL.toLong(), decoded.value)
        assertEquals(4096, decoded.length)
        assertEquals(GlkRequest.SIZE, encoded.capacity())
    }

    @Test
    fun lkmSessionPublishesStagedCapability() = runBlocking {
        val transport = FakeTransport()
        val session = LkmGlkSession(transport = transport, open = { descriptor() }, close = { })
        val before = session.kernelMemory.descriptor
        assertEquals(null, before.read)
        assertEquals(null, before.write)

        val init = session.initialize()
        assertTrue(init is AccessResult.Success)
        assertEquals(GlkRequest.OP_PING, transport.requests.first().op)
        val ready = session.kernelMemory.descriptor
        assertEquals(1, ready.read?.granularity)
        assertEquals(GlkRequest.MAX_TRANSFER, ready.read?.maxBytes)
        assertEquals(1u, ready.generation)

        val write = session.kernelMemory.write(KernelAddress(0x1000), byteArrayOf(9, 8, 7))
        assertTrue(write is AccessResult.Success)
        assertEquals(3, (write as AccessResult.Success).value.bytes)
        assertEquals(EffectState.CONFIRMED, write.value.effect)

        val read = session.kernelMemory.read(KernelAddress(0x1000), 8)
        assertTrue(read is AccessResult.Success)
        assertEquals(8, (read as AccessResult.Success).value.size)

        val down = session.shutdown()
        assertTrue(down is AccessResult.Success)
        assertFalse(session.kernelMemory.descriptor.ready)
        assertEquals(2u, session.kernelMemory.descriptor.generation)
    }

    @Test
    fun lkmWriteFailureReportsUnknownEffect() = runBlocking {
        val transport = FakeTransport()
        val session = LkmGlkSession(transport = transport, open = { descriptor() }, close = { })
        session.initialize()
        transport.nextStatus = 5
        val write = session.kernelMemory.write(KernelAddress(0x1000), byteArrayOf(1))
        assertTrue(write is AccessResult.Success)
        assertEquals(EffectState.UNKNOWN, (write as AccessResult.Success).value.effect)
    }

    @Test
    fun lkmRejectsBadWidthAndNotReady() = runBlocking {
        val session = LkmGlkSession(transport = FakeTransport(), open = { descriptor() }, close = { })
        assertTrue(session.kernelMemory.read(KernelAddress(0), 8) is AccessResult.Failure)
        session.initialize()
        assertTrue(session.kernelMemory.read(KernelAddress(0), GlkRequest.MAX_TRANSFER + 1) is AccessResult.Failure)
        assertTrue(session.kernelMemory.write(KernelAddress(0), ByteArray(0)) is AccessResult.Failure)
    }

    @Test
    fun pageCacheSessionUsesBlockWidthAndTargets() = runBlocking {
        var up = true
        val target = PageCacheTarget("main", "/data/local/tmp/app.bin", writable = true)
        val ops = object : PageCacheOps {
            override fun up(): Boolean = up
            override fun targets(): List<PageCacheTarget> = listOf(target)
            override fun readBlock(target: PageCacheTarget, offset: Long, out: ByteArray): Boolean {
                for (i in out.indices) out[i] = i.toByte()
                return true
            }
            override fun writeBlock(target: PageCacheTarget, offset: Long, block: ByteArray): Boolean =
                block.size == 16
            override fun tearDown(): Boolean {
                up = false
                return true
            }
        }
        val session = PageCacheKernelAccess(ops)
        val init = session.initialize()
        assertTrue(init is AccessResult.Success)
        assertEquals(1u, session.pageCache.descriptor.generation)
        assertEquals("main", session.pageCache.descriptor.targets.single().id)

        val read = session.pageCache.read(target, 0, 16)
        assertTrue(read is AccessResult.Success)
        assertEquals(16, (read as AccessResult.Success).value.size)
        assertTrue(session.pageCache.read(target, 0, 8) is AccessResult.Failure)
        val write = session.pageCache.write(target, 0, ByteArray(16))
        assertEquals(EffectState.CONFIRMED, (write as AccessResult.Success).value.effect)

        val down = session.shutdown()
        assertTrue(down is AccessResult.Success)
        assertEquals(2u, session.pageCache.descriptor.generation)
    }
}
