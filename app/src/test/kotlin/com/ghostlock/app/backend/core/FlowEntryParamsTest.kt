package com.ghostlock.app.backend.core

import com.ghostlock.app.backend.host.FlowEntryParams
import com.ghostlock.app.core.flow.FlowHostParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** B4a（I4）：host 保留键拒绝 / 注入覆盖 / 注入后预算。 */
class FlowEntryParamsTest {
    @Test
    fun `reserved prefix and keys are fixed`() {
        assertEquals("host.", FlowEntryParams.RESERVED_PREFIX)
        assertEquals("host.uid", FlowEntryParams.KEY_HOST_UID)
        assertEquals("host.pid", FlowEntryParams.KEY_HOST_PID)
        assertEquals("host.domain", FlowEntryParams.KEY_HOST_DOMAIN)
        assertEquals("host.run_dir", FlowEntryParams.KEY_HOST_RUN_DIR)
        assertEquals("host.native_lib_dir", FlowEntryParams.KEY_HOST_NATIVE_LIB_DIR)
        assertEquals("host.log_path", FlowEntryParams.KEY_HOST_LOG_PATH)
    }

    @Test
    fun `forged host key is rejected`() {
        val problem = runCatching {
            FlowEntryParams.validateUserParams(mapOf("host.uid" to 1000, "ok" to 1))
        }.exceptionOrNull()
        assertTrue(problem is IllegalArgumentException)
        assertEquals("flow-params:reserved-key:host.uid", problem!!.message)
    }

    @Test
    fun `inject strips caller host keys and host wins`() {
        val base = FlowHostParams.of(
            linkedMapOf("user" to 7, "host.uid" to 999, "host.run_dir" to "/caller"),
        )
        val injected = FlowEntryParams.injectHost(
            base,
            linkedMapOf(
                FlowEntryParams.KEY_HOST_UID to 2000,
                FlowEntryParams.KEY_HOST_RUN_DIR to "/host/run",
            ),
        )
        assertEquals(7, injected.getInt("user"))
        assertEquals(2000, injected.getInt(FlowEntryParams.KEY_HOST_UID))
        assertEquals("/host/run", injected.getString(FlowEntryParams.KEY_HOST_RUN_DIR))
    }

    @Test
    fun `bad host key fails closed`() {
        val base = FlowHostParams.of(emptyMap())
        val problem = runCatching {
            FlowEntryParams.injectHost(base, mapOf("not-host" to 1))
        }.exceptionOrNull()
        assertTrue(problem is IllegalArgumentException)
        assertEquals("flow-params:bad-host-key:not-host", problem!!.message)
    }

    @Test
    fun `injection over budget fails closed`() {
        val base = FlowHostParams.of(mapOf("user" to 1))
        val oversized = ByteArray(FlowHostParams.MAX_TOTAL_BYTES + 1)
        val problem = runCatching {
            FlowEntryParams.injectHost(base, mapOf("host.blob" to oversized))
        }.exceptionOrNull()
        assertTrue(problem is IllegalArgumentException)
        assertTrue(problem!!.message!!.startsWith("flow-params:"))
    }
}
