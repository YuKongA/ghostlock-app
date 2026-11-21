package com.ghostlock.app.data

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
 * filtering, and ksud detection through a real PackageManager. */
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

    private fun install(pkg: String, system: Boolean, ksud: Boolean) {
        // Each package gets its own lib dir so ksud presence is independent.
        val dir = File(app.cacheDir, "libs-${pkg}-${System.nanoTime()}").apply { mkdirs() }
        val info = ApplicationInfo().apply {
            packageName = pkg
            flags = if (system) ApplicationInfo.FLAG_SYSTEM else 0
            nativeLibraryDir = dir.absolutePath
        }
        if (ksud) File(dir, "libksud.so").writeText("stub")
        shadowOf(pm).installPackage(
            PackageInfo().apply {
                packageName = pkg
                applicationInfo = info
            },
        )
    }

    private fun installNovaWithRenamedKsud(pkg: String): File {
        val dir = File(app.cacheDir, "libs-${pkg}-${System.nanoTime()}").apply { mkdirs() }
        File(dir, "liblaapkjdiji.so").writeText("stub userspace/ksud/src marker")
        shadowOf(pm).installPackage(
            PackageInfo().apply {
                packageName = pkg
                applicationInfo = ApplicationInfo().apply {
                    packageName = pkg
                    nativeLibraryDir = dir.absolutePath
                }
            },
        )
        return dir
    }

    @Test
    fun `scan keeps user apps and drops self and system apps`() {
        install("me.weishu.kernelsu", system = false, ksud = true)
        install("com.example.no-ksud", system = false, ksud = false)
        install("com.android.settings", system = true, ksud = false)

        val result = ManagerScanner.scan(pm, "com.ghostlock.app")
        // User apps without libksud stay visible for the show-all section.
        assertEquals(
            listOf("me.weishu.kernelsu", "com.example.no-ksud"),
            result.map { it.packageName },
        )
        assertTrue(result[0].hasKsud)
        assertTrue(result[0].builtin)
        assertFalse(result[1].hasKsud)
    }

    @Test
    fun `scan detects Nova ksud under a randomized library name`() {
        val dir = installNovaWithRenamedKsud("com.sukisu.nova")
        val result = ManagerScanner.scan(pm, "com.ghostlock.app")
        assertEquals(1, result.size)
        assertTrue(result[0].hasKsud)
        assertTrue(result[0].builtin)
        assertEquals("liblaapkjdiji.so", ManagerScanner.findKsud("com.sukisu.nova", dir.absolutePath)?.name)
    }

    @Test
    fun `scan detects Nova PR ksud under a randomized library name`() {
        val dir = installNovaWithRenamedKsud("com.sukisu.nova.pr")
        val result = ManagerScanner.scan(pm, "com.ghostlock.app")
        assertEquals(1, result.size)
        assertTrue(result[0].hasKsud)
        assertTrue(result[0].builtin)
        assertEquals("liblaapkjdiji.so", ManagerScanner.findKsud("com.sukisu.nova.pr", dir.absolutePath)?.name)
    }

    @Test
    fun `findKsud detects marker across streaming buffer boundary`() {
        val dir = File(app.cacheDir, "libs-chunked-${System.nanoTime()}").apply { mkdirs() }
        val marker = "userspace/ksud/src".toByteArray()
        val prefix = ByteArray(64 * 1024 - marker.size + 1) { 65 }
        File(dir, "libchunked.so").writeBytes(prefix + marker)
        assertEquals("libchunked.so", ManagerScanner.findKsud("com.sukisu.nova", dir.absolutePath)?.name)
    }

    @Test
    fun `scan drops the self package even if it ships libksud`() {
        install("com.ghostlock.app", system = false, ksud = true)
        assertTrue(ManagerScanner.scan(pm, "com.ghostlock.app").isEmpty())
    }

    @Test
    fun `scan reports hasKsud false when the lib dir is empty`() {
        install("com.example.bare", system = false, ksud = false)
        val result = ManagerScanner.scan(pm, "com.ghostlock.app")
        assertEquals(1, result.size)
        assertFalse(result[0].hasKsud)
        assertFalse(result[0].builtin)
    }
}
