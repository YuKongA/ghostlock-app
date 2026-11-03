package com.ghostlock.app.backend.backends.cve43499.victim

import com.ghostlock.app.core.victim.VictimCapability
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-86/A1-A2a：公共 victim launch spec 单测（先写不运行）。
 *
 * 覆盖：helperPath 透传、argv/env 空、capability 用符号位（10 项含 7/8/9）、
 * rawSize 由 perf 派生值透传、timeout 用既有 limits。
 */
class Cve43499VictimLaunchSpecFactoryTest {

    @Test
    fun `launch spec uses symbolic capabilities and empty argv env`() {
        val config = Cve43499VictimLaunchSpecFactory.create(
            helperPath = "/data/app/lib/arm64/libghostlock_victim_helper.so",
            rawSizeBytes = 1L shl 20,
        )
        assertEquals("/data/app/lib/arm64/libghostlock_victim_helper.so", config.helperPath)
        assertEquals(listOf("/data/app/lib/arm64/libghostlock_victim_helper.so"), config.helperArgv)
        assertTrue(config.helperEnv.isEmpty())
        assertEquals(1L shl 20, config.rawSizeBytes)
        for (capability in Cve43499VictimLaunchSpecFactory.COMMON_CAPABILITIES) {
            assertTrue(capability.name, config.capabilities.contains(capability))
        }
        assertEquals(
            "all appended capability bits",
            listOf(
                VictimCapability.PROBE_STATUS,
                VictimCapability.PROBE_COMM_SET,
                VictimCapability.PROBE_COMM,
            ),
            Cve43499VictimLaunchSpecFactory.COMMON_CAPABILITIES.takeLast(3),
        )
        assertEquals(Cve43499JniVictimLimits.CONTROL_TIMEOUT_MS, config.controlTimeoutMs)
        assertEquals(Cve43499JniVictimLimits.COMMAND_TIMEOUT_MS, config.commandTimeoutMs)
        assertEquals(Cve43499JniVictimLimits.CLOSE_TIMEOUT_MS, config.closeTimeoutMs)
    }

    @Test
    fun `common runtime exposes victim session factory without script launcher`() {
        val runtime = Cve43499CommonVictimRuntime(
            helperPath = "/data/app/lib/arm64/libghostlock_victim_helper.so",
            rawSizeBytes = 1L shl 20,
        )
        val session = runtime.newVictimSession()
        assertTrue(session is Cve43499VictimAdapter)
        runtime.dispose()
    }
}
