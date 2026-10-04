package com.ghostlock.app.data.ipsec

import android.app.Application
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * The Android factory fails closed instead of throwing when no IpSecManager is
 * reachable (Robolectric has no IPSEC_SERVICE), so the SA path is testable
 * without a device. No real SA is ever built here.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AndroidIpsecSessionFactoryTest {

    @Test
    fun missingIpsecManagerFailsClosedWithoutThrowing() {
        val context: Application = RuntimeEnvironment.getApplication()
        val result = AndroidIpsecSessionFactory(context).create()
        assertTrue("expected a typed failure, got \$result", result is IpsecSessionResult.Failure)
    }
}
