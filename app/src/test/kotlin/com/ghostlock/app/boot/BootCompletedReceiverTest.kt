package com.ghostlock.app.boot

import android.content.Intent
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BootCompletedReceiverTest {
    @Test
    fun acceptsLockedAndUnlockedBootBroadcasts() {
        assertTrue(isBootAction(Intent.ACTION_LOCKED_BOOT_COMPLETED))
        assertTrue(isBootAction(Intent.ACTION_BOOT_COMPLETED))
    }

    @Test
    fun rejectsUnrelatedBroadcasts() {
        assertFalse(isBootAction(Intent.ACTION_AIRPLANE_MODE_CHANGED))
    }
}
