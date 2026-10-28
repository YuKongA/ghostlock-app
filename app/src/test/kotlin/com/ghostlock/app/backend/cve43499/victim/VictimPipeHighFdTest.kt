package com.ghostlock.app.backend.cve43499.victim

import java.io.FileDescriptor
import java.io.IOException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/** task-132/S1：victim pipe 高位策略的宿主逻辑测试（不触 Android）。 */
class VictimPipeHighFdTest {
    @Test
    fun `high fd floor is pselect nfds plus 64`() {
        assertEquals(320, VictimPipeHighFd.PSELECT_NFDS)
        assertEquals(384, VictimPipeHighFd.MIN_FD)
    }

    @Test
    fun `relocation accepts fd at or above floor`() {
        val high = VictimPipeHighFd.relocateOrThrow(
            VictimPipeFdRelocator { _, minimum ->
                assertEquals(VictimPipeHighFd.MIN_FD, minimum)
                VictimPipeHighFd.MIN_FD
            },
            FileDescriptor(),
        )
        assertEquals(VictimPipeHighFd.MIN_FD, high)
    }

    @Test
    fun `relocation below floor fails closed`() {
        try {
            VictimPipeHighFd.relocateOrThrow(
                VictimPipeFdRelocator { _, _ -> VictimPipeHighFd.MIN_FD - 1 },
                FileDescriptor(),
            )
            fail("low fd must fail closed")
        } catch (expected: IOException) {
            assertTrue(expected.message!!.startsWith("victim-pipe:high-fd:"))
        }
    }

    @Test
    fun `spawn fails closed when parent pipe fd locate fails`() {
        val result = runCatching {
            ProcessVictim(
                command = listOf("/bin/sh", "-c", "cat"),
                pipeFdLocator = { throw IOException("victim-pipe:fd-list") },
            )
        }
        assertTrue("spawn must fail", result.isFailure)
        assertTrue("failure must be IOException",
            result.exceptionOrNull() is IOException)
    }

    @Test
    fun `relocation failure propagates as IOException`() {
        try {
            VictimPipeHighFd.relocateOrThrow(
                VictimPipeFdRelocator { _, _ -> -1 },
                FileDescriptor(),
            )
            fail("negative fd must fail closed")
        } catch (expected: IOException) {
            assertTrue(expected.message!!.startsWith("victim-pipe:high-fd:"))
        }
    }
}

/** task-132/S1：/proc pipe 链接解析的宿主逻辑测试（不触 Android）。 */
class AndroidProcessPipeFdsTest {
    @Test
    fun `parses pipe inode only`() {
        assertEquals(12345L, AndroidProcessPipeFds.pipeInode("pipe:[12345]"))
        assertEquals(null, AndroidProcessPipeFds.pipeInode("/dev/null"))
        assertEquals(null, AndroidProcessPipeFds.pipeInode("pipe:[not-number]"))
    }
}
