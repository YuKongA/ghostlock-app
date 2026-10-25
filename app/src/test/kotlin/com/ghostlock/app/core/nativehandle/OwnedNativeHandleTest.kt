package com.ghostlock.app.core.nativehandle

import com.ghostlock.app.core.nativeabi.NativeCallResult
import com.ghostlock.app.core.nativeabi.NativeCallStatus
import com.ghostlock.app.core.nativeabi.NativeEffect
import org.junit.Assert.assertEquals
import org.junit.Assert.fail
import org.junit.Test
import java.util.concurrent.atomic.AtomicInteger

class OwnedNativeHandleTest {
    @Test
    fun `close is idempotent and calls native once`() {
        val calls = AtomicInteger()
        val scope = RunResourceScope()
        val resource = FakeHandle(scope, NativeHandleId(7L)) { _, _ ->
            calls.incrementAndGet()
            NativeCallResult(1UL, NativeCallStatus.OK)
        }

        resource.close()
        resource.close()

        assertEquals(1, calls.get())
        assertEquals(NativeHandlePhase.CLOSED, resource.phase)
    }

    @Test
    fun `scope closes resources in reverse registration order`() {
        val order = mutableListOf<Long>()
        val scope = RunResourceScope()
        FakeHandle(scope, NativeHandleId(1L)) { value, _ ->
            order += value.encoded
            NativeCallResult(1UL, NativeCallStatus.OK)
        }
        FakeHandle(scope, NativeHandleId(2L)) { value, _ ->
            order += value.encoded
            NativeCallResult(1UL, NativeCallStatus.OK)
        }

        scope.close()

        assertEquals(listOf(2L, 1L), order)
    }

    @Test
    fun `dirty close quarantines without retry`() {
        val calls = AtomicInteger()
        val scope = RunResourceScope()
        val resource = FakeHandle(scope, NativeHandleId(7L)) { _, _ ->
            calls.incrementAndGet()
            NativeCallResult(1UL, NativeCallStatus.DIRTY_FAILURE, effect = NativeEffect.DIRTY)
        }

        resource.close()
        resource.close()

        assertEquals(1, calls.get())
        assertEquals(NativeHandlePhase.QUARANTINED, resource.phase)
    }

    @Test
    fun `transfer invalidates old wrapper`() {
        val scope = RunResourceScope()
        val resource = FakeHandle(scope, NativeHandleId(7L)) { _, _ ->
            NativeCallResult(1UL, NativeCallStatus.OK)
        }

        resource.transfer()

        assertEquals(NativeHandlePhase.TRANSFERRED, resource.phase)
        try {
            resource.openId()
            fail("transferred wrapper accepted access")
        } catch (_: IllegalStateException) {
            Unit
        }
    }

    @Test
    fun `scope continues reverse cleanup after one close throws`() {
        val order = mutableListOf<Long>()
        val scope = RunResourceScope()
        FakeHandle(scope, NativeHandleId(1L)) { value, _ ->
            order += value.encoded
            NativeCallResult(1UL, NativeCallStatus.OK)
        }
        FakeHandle(scope, NativeHandleId(2L)) { value, _ ->
            order += value.encoded
            error("close failed")
        }

        try {
            scope.close()
            fail("scope swallowed close failure")
        } catch (_: IllegalStateException) {
            Unit
        }
        assertEquals(listOf(2L, 1L), order)
    }

    private class FakeHandle(
        scope: RunResourceScope,
        id: NativeHandleId,
        port: NativeHandlePort,
    ) : OwnedNativeHandle(
        id,
        scope,
        port,
    ) {
        fun transfer(): NativeHandleId = detachForTransfer()
        fun openId(): NativeHandleId = requireOpenId()
    }
}
