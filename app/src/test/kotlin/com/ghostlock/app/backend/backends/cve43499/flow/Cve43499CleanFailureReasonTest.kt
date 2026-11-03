package com.ghostlock.app.backend.backends.cve43499.flow

import com.ghostlock.app.backend.backends.cve43499.flow.isRetryableCleanFailure
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-97 E1 有界重试判定单测（先写不运行）。
 *
 * 仅 clean failure 可重试；dirty/receipt-incomplete/参数错误一律立即 FAIL；
 * detail 后缀不影响判定。
 */
class Cve43499CleanFailureReasonTest {

    @Test
    fun `clean failures are retryable with detail suffix`() {
        assertTrue(isRetryableCleanFailure("cve43499.write.w1:landing:clean-failure"))
        assertTrue(
            isRetryableCleanFailure(
                "landing:clean-failure:call=invalid-argument:errno=0",
            ),
        )
        assertTrue(
            isRetryableCleanFailure(
                "actor-execute:clean-failure:call=ok:errno=0:consumer_success=0/3",
            ),
        )
        assertTrue(
            isRetryableCleanFailure(
                "prepare-actor:clean-failure:call=ok:errno=0:owner_ready=false",
            ),
        )
        assertTrue(isRetryableCleanFailure("page.land:page-not-accepted-clean-failure"))
    }

    @Test
    fun `non clean failures are not retryable`() {
        assertFalse(isRetryableCleanFailure("actor-execute:dirty"))
        assertFalse(isRetryableCleanFailure("actor-execute:receipt-incomplete"))
        assertFalse(isRetryableCleanFailure("landing:receipt-incomplete"))
        assertFalse(isRetryableCleanFailure("prepare-actor:not-accepted"))
        assertFalse(isRetryableCleanFailure("placement:system-error"))
        assertFalse(isRetryableCleanFailure("landing:rejected"))
        assertEquals(false, isRetryableCleanFailure(""))
    }
}
