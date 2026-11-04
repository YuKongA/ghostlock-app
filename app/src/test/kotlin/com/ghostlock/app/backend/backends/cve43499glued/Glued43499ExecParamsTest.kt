package com.ghostlock.app.backend.backends.cve43499glued

import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowHostParams
import com.ghostlock.app.backend.declarations.BackendFlowParams
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** B4a（I5）：profile→参数等价映射 / 解码缺键与类型错。 */
class Glued43499ExecParamsTest {
    private val profile = Resolved43499Profile(
        release = "6.1.145-android14",
        sourceText = "profile {\n  value = 1\n}\n",
        binary = byteArrayOf(0x47, 0x4c, 0x4b, 0x31, 0x00, 0x7f),
    )

    @Test
    fun `fromResolved preserves source bytes and release`() {
        val ready = Glued43499ExecParams.fromResolved(profile)
        assertEquals(profile.sourceText, ready.params[Glued43499ExecParams.KEY_PROFILE_CONF])
        assertEquals(profile.release, ready.params[Glued43499ExecParams.KEY_PROFILE_RELEASE])
        assertArrayEquals(
            profile.binary,
            ready.params[Glued43499ExecParams.KEY_PROFILE_BIN] as ByteArray,
        )
    }

    @Test
    fun `build seam maps resolved profile`() {
        val built = Glued43499ExecParams.build("builtin:x") { profile }
        assertTrue(built is BackendFlowParams.Ready)
        assertEquals(profile.release, (built as BackendFlowParams.Ready)
            .params[Glued43499ExecParams.KEY_PROFILE_RELEASE])
    }

    @Test
    fun `build seam failure is named profile-invalid`() {
        val built = Glued43499ExecParams.build("bad") { error("profile-hocon-not-an-object") }
        assertTrue(built is BackendFlowParams.Refused)
        val reason = (built as BackendFlowParams.Refused).reasons.single()
        assertEquals("cve43499:profile-invalid:profile-hocon-not-an-object", reason)
    }

    @Test
    fun `decode roundtrips injected params`() {
        val parsed = Glued43499ExecParams.decode(
            env(
                mapOf(
                    Glued43499ExecParams.KEY_PROFILE_CONF to profile.sourceText,
                    Glued43499ExecParams.KEY_PROFILE_BIN to profile.binary,
                    Glued43499ExecParams.KEY_PROFILE_RELEASE to profile.release,
                ),
            ),
        )
        assertEquals(profile.sourceText, parsed?.conf)
        assertEquals(profile.release, parsed?.release)
        assertArrayEquals(profile.binary, parsed?.bin)
    }

    @Test
    fun `decode rejects missing or wrong typed keys`() {
        assertNull(
            Glued43499ExecParams.decode(
                env(
                    mapOf(
                        Glued43499ExecParams.KEY_PROFILE_CONF to profile.sourceText,
                        Glued43499ExecParams.KEY_PROFILE_RELEASE to profile.release,
                    ),
                ),
            ),
        )
        assertNull(
            Glued43499ExecParams.decode(
                env(
                    mapOf(
                        Glued43499ExecParams.KEY_PROFILE_CONF to 42,
                        Glued43499ExecParams.KEY_PROFILE_BIN to profile.binary,
                        Glued43499ExecParams.KEY_PROFILE_RELEASE to profile.release,
                    ),
                ),
            ),
        )
    }

    /** @return 假 FlowHostEnv（参数固定；只测纯 decode）。 */
    private fun env(values: Map<String, Any?>): FlowHostEnv = object : FlowHostEnv {
        override val params: FlowHostParams = FlowHostParams.of(values)
        override val payload: ByteArray? = null
        override fun log(line: String) = Unit
        override fun isCancelled(): Boolean = false
    }
}
