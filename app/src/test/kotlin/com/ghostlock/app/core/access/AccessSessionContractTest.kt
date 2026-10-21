package com.ghostlock.app.core.access

import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.CleanupDisposition
import com.ghostlock.app.core.SessionPhase
import org.junit.Assert.assertEquals
import org.junit.Test

class AccessSessionContractTest {
    private class Fake : AccessSession {
        var closed = false; private set
        override val capabilities = object : AccessCapabilities {
            override val kernelMemory: KernelMemoryAccess? = null
            override val pageCache: PageCacheAccess? = null
        }
        override val phase: SessionPhase get() = if (closed) SessionPhase.CLOSED else SessionPhase.READY
        override val cleanup: CleanupDisposition get() = CleanupDisposition.CLEAN
        override fun close() { closed = true }
        override suspend fun initialize() = AccessResult.Success(InitializationReceipt(emptyList()))
        override suspend fun snapshot() = AccessResult.Success(SessionSnapshot("ready", emptyMap()))
        override suspend fun shutdown() = AccessResult.Success(ShutdownReceipt(clean = true))
    }

    @Test
    fun canonicalSessionHasLifecycleAndCapabilities() {
        val session: AccessSession = Fake()
        assertEquals(SessionPhase.READY, session.phase)
        session.close()
        assertEquals(SessionPhase.CLOSED, session.phase)
    }
}
