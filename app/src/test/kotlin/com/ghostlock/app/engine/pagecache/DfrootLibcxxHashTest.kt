package com.ghostlock.app.engine.pagecache

import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Test

class DfrootLibcxxHashTest {
    @Test
    fun digestMatchesCompiledPayload() {
        val digest = MessageDigest.getInstance("SHA-256").digest(DfrootLibcxx.bytes())
            .joinToString("") { "%02x".format(it) }
        assertEquals("da7ea0f6d88b3ce00f55e1cf40c6c595279af392f61313c8257e88d22393b4be", digest)
    }
}
