package com.ghostlock.app.backend.cve43284glued

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Device smoke for the App-UID IPsec capability used by the 43284 process launcher. */
@RunWith(AndroidJUnit4::class)
class AndroidIpSecRuntimeSmokeTest {
    @Test
    fun appDomainAcquiresAndReleasesIpSecRuntimeWithoutStartingElf() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val runtime = AndroidIpSecRuntime.create(context)

        try {
            assertEquals("--encap-port", runtime.arguments[0])
            assertTrue(runtime.arguments[1].toInt() in 1..65535)
            assertEquals("--sender-port", runtime.arguments[2])
            assertTrue(runtime.arguments[3].toInt() in 1..65535)
            assertEquals("--spi", runtime.arguments[4])
            assertTrue(runtime.arguments[5].toLong() in 1L..0xFFFF_FFFFL)
            assertEquals("--aes-key", runtime.arguments[6])
            assertTrue(runtime.arguments[7].matches(Regex("[0-9a-f]{64}")))
        } finally {
            runtime.close()
            runtime.close()
        }
        assertTrue(runtime.arguments.isEmpty())
    }
}
