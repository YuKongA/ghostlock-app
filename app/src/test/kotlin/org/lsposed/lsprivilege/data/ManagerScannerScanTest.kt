package org.lsposed.lsprivilege.data

import android.app.Application
import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import java.io.File

/** Covers the platform-facing scan(): package enumeration, self/system
 * filtering, and libksud.so detection through a real PackageManager. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ManagerScannerScanTest {
    private lateinit var pm: PackageManager
    private lateinit var app: Application

    @Before
    fun setUp() {
        app = RuntimeEnvironment.getApplication() as Application
        pm = app.packageManager
    }

    private fun install(pkg: String, system: Boolean, ksud: Boolean, daemonLib: String = "libksud.so") {
        // Each package gets its own lib dir so ksud/apd presence is independent.
        val dir = File(app.cacheDir, "libs-${pkg}-${System.nanoTime()}").apply { mkdirs() }
        val info = ApplicationInfo().apply {
            packageName = pkg
            flags = if (system) ApplicationInfo.FLAG_SYSTEM else 0
            nativeLibraryDir = dir.absolutePath
        }
        if (ksud) File(dir, daemonLib).writeText("stub")
        shadowOf(pm).installPackage(
            PackageInfo().apply {
                packageName = pkg
                applicationInfo = info
            },
        )
    }

    @Test
    fun `scan keeps user apps and drops self and system apps`() {
        install("me.weishu.kernelsu", system = false, ksud = true)
        install("me.bmax.apatch", system = false, ksud = true, daemonLib = "libapd.so")
        install("com.example.no-ksud", system = false, ksud = false)
        install("com.android.settings", system = true, ksud = false)

        val result = ManagerScanner.scan(pm, "org.lsposed.lsprivilege")
        // User apps without libksud/libapd stay visible for the show-all section.
        assertEquals(
            listOf("me.bmax.apatch", "me.weishu.kernelsu", "com.example.no-ksud"),
            result.map { it.packageName },
        )
        assertTrue(result[0].hasKsud)
        assertTrue(result[0].builtin)
        assertTrue(result[1].hasKsud)
        assertTrue(result[1].builtin)
        assertFalse(result[2].hasKsud)
    }

    @Test
    fun `scan drops the self package even if it ships libksud`() {
        install("org.lsposed.lsprivilege", system = false, ksud = true)
        assertTrue(ManagerScanner.scan(pm, "org.lsposed.lsprivilege").isEmpty())
    }

    @Test
    fun `scan reports hasKsud false when the lib dir is empty`() {
        install("com.example.bare", system = false, ksud = false)
        val result = ManagerScanner.scan(pm, "org.lsposed.lsprivilege")
        assertEquals(1, result.size)
        assertFalse(result[0].hasKsud)
        assertFalse(result[0].builtin)
    }
}
