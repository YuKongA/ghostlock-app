package com.ghostlock.app.data.nativeaccess

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.access.PageCacheTarget
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class JniPageCacheOpsTest {
    private val target = PageCacheTarget("carrier", "/data/local/tmp/carrier", writable = true)

    @Test
    fun failsClosedWhenSessionCannotAnswer() {
        val ops = JniPageCacheOps(
            SessionInvoker { _, _, _ -> AccessResult.Failure(AccessFailure.NATIVE_FAILURE, "closed") },
            target,
        )
        assertFalse(ops.up())
        assertTrue(ops.targets().isEmpty())
        assertFalse(ops.readBlock(target, 0L, ByteArray(16)))
        assertFalse(ops.writeBlock(target, 0L, ByteArray(16)))
    }
}
