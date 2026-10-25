package com.ghostlock.app.core.nativeabi

import org.junit.Assert.assertEquals
import org.junit.Test

class NativeCallResultTest {
    @Test
    fun `errno and effect stay independent from status`() {
        val result = NativeCallResult(
            requestId = 42UL,
            status = NativeCallStatus.CLEAN_FAILURE,
            errnoValue = 16,
            effect = NativeEffect.CLEAN,
        )

        assertEquals(42UL, result.requestId)
        assertEquals(NativeCallStatus.CLEAN_FAILURE, result.status)
        assertEquals(16, result.errnoValue)
        assertEquals(NativeEffect.CLEAN, result.effect)
    }
}
