package com.ghostlock.app.data

import android.app.Application
import android.content.Intent
import android.provider.Settings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BootRootSettingsTest {
    private val app: Application = RuntimeEnvironment.getApplication()
    private val settings = BootRootSettings(app)

    @Test
    fun bootBroadcastStartsServiceOnlyAfterOptInAndNewBoot() {
        val receiver = BootRootReceiver()
        val broadcast = Intent(Intent.ACTION_BOOT_COMPLETED)
        setBootCount(10)
        receiver.onReceive(app, broadcast)
        assertNull(shadowOf(app).nextStartedService)

        assertTrue(settings.setEnabled(true))
        receiver.onReceive(app, broadcast)
        assertNull(shadowOf(app).nextStartedService)

        setBootCount(11)
        receiver.onReceive(app, Intent(Intent.ACTION_USER_PRESENT))
        assertNull(shadowOf(app).nextStartedService)

        receiver.onReceive(app, broadcast)
        assertEquals(
            BootRootService::class.java.name,
            shadowOf(app).nextStartedService.component?.className,
        )
    }

    @Test
    fun completedAttemptIsDeduplicatedForTheRestOfThatBoot() {
        setBootCount(10)
        assertTrue(settings.setEnabled(true))
        assertTrue(settings.begin(11))
        assertFalse(settings.begin(11))
        settings.finish(11)
        assertFalse(BootRootSettings(app).begin(11))
        assertTrue(BootRootSettings(app).begin(12))
    }

    @Test
    fun interruptedAttemptDisarmsFollowingBootUntilExplicitlyEnabledAgain() {
        setBootCount(10)
        assertTrue(settings.setEnabled(true))
        assertTrue(settings.begin(11))

        setBootCount(12)
        val afterReboot = BootRootSettings(app)
        assertFalse(afterReboot.begin(12))
        assertFalse(afterReboot.isEnabled())
        assertFalse(afterReboot.begin(13))

        assertTrue(afterReboot.setEnabled(true))
        assertFalse(afterReboot.begin(12))
        assertTrue(afterReboot.begin(13))
    }

    @Test
    fun finishingAnAttemptDoesNotUndoTheUsersOptOut() {
        setBootCount(10)
        assertTrue(settings.setEnabled(true))
        setBootCount(11)
        assertTrue(settings.begin(11))
        assertTrue(settings.setEnabled(false))
        settings.finish(11)
        assertFalse(settings.isEnabled())
        assertFalse(settings.begin(12))
    }

    private fun setBootCount(count: Int) {
        Settings.Global.putInt(app.contentResolver, Settings.Global.BOOT_COUNT, count)
    }
}
