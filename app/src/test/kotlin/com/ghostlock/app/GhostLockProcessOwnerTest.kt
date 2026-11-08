package com.ghostlock.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Locks the exact main-process owner predicate used before any DataStore is constructed. */
class GhostLockProcessOwnerTest {
    @Test
    fun onlyExactPackageProcessOwnsUiContainer() {
        assertTrue(GhostLockApp.isMainApplicationProcess("com.ghostlock.app", "com.ghostlock.app"))
        assertFalse(GhostLockApp.isMainApplicationProcess("com.ghostlock.app:flowhost", "com.ghostlock.app"))
        assertFalse(GhostLockApp.isMainApplicationProcess(null, "com.ghostlock.app"))
    }
}
