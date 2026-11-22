package com.ghostlock.app.data.run

import android.system.Os
import android.system.OsConstants
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.runhost.RunHostIdentity
import java.io.File
import java.nio.file.Files
import java.util.UUID
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Device coverage for no-follow cleanup of extracted plugin package trees. */
@RunWith(AndroidJUnit4::class)
class AndroidRunWorkspaceTest {
    /** Complete and partial 0500 package trees are made traversable only during reverse deletion. */
    @Test
    fun deletesCompleteAndPartialReadOnlyPackageTrees() {
        val workspace = workspace()
        val complete = File(workspace.pluginPackages, "complete")
        val code = File(complete, "code")
        val native = File(complete, "lib/arm64-v8a")
        val partial = File(workspace.pluginPackages, "partial/nested")
        assertTrue(code.mkdirs())
        assertTrue(native.mkdirs())
        assertTrue(partial.mkdirs())
        File(code, "classes.dex").apply { writeText("dex"); Os.chmod(absolutePath, 0b100000000) }
        File(native, "libghostlock_diagnose.so").apply {
            writeText("elf")
            Os.chmod(absolutePath, 0b100000000)
        }
        File(partial, "interrupted.tmp").apply {
            writeText("partial")
            Os.chmod(absolutePath, 0b100000000)
        }
        listOf(partial, partial.parentFile, complete, code, native, native.parentFile, workspace.pluginPackages)
            .distinct()
            .forEach { directory -> Os.chmod(directory.absolutePath, 0b101000000) }

        assertEquals(0b101000000, mode(complete))
        assertTrue(workspace.deleteAfterTerminal())
        assertFalse(workspace.root.exists())
        assertTrue(workspace.persistentStateDirectory.isDirectory)
    }

    /** A retained terminal leaves its 0500 evidence tree untouched until a caller chooses deletion. */
    @Test
    fun retainedWorkspaceRemainsUntouchedWithoutDeletionDecision() {
        val workspace = workspace()
        val retained = File(workspace.pluginPackages, "retained").apply { mkdir() }
        val evidence = File(retained, "classes.dex").apply { writeText("evidence") }
        Os.chmod(evidence.absolutePath, 0b100000000)
        Os.chmod(retained.absolutePath, 0b101000000)

        assertTrue(workspace.root.isDirectory)
        assertTrue(evidence.isFile)
        assertEquals(0b101000000, mode(retained))
        cleanup(workspace.root)
    }

    /** A symlink or replaced root fails closed and preserves evidence instead of following/deleting it. */
    @Test
    fun symlinkAndRootIdentityReplacementFailClosed() {
        val first = workspace()
        val outside = File(first.root.parentFile, "outside-${System.nanoTime()}").apply {
            writeText("outside")
        }
        val link = File(first.pluginPackages, "escape")
        Os.symlink(outside.absolutePath, link.absolutePath)
        assertFalse(first.deleteAfterTerminal())
        assertTrue(first.root.exists())
        assertEquals("outside", outside.readText())
        Files.delete(link.toPath())
        cleanup(first.root)
        outside.delete()

        val second = workspace()
        val capturedRoot = second.root
        val moved = File(capturedRoot.parentFile, capturedRoot.name + "-moved")
        assertTrue(capturedRoot.renameTo(moved))
        assertTrue(capturedRoot.mkdir())
        assertFalse(second.deleteAfterTerminal())
        assertTrue(capturedRoot.exists())
        assertTrue(moved.exists())
        cleanup(capturedRoot)
        cleanup(moved)
    }

    private fun workspace(): AndroidRunWorkspace {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        return AndroidRunWorkspace.createApp(
            context = context,
            identity = RunHostIdentity(
                UUID.randomUUID().toString().replace("-", ""),
                System.nanoTime().and(Long.MAX_VALUE).coerceAtLeast(1L),
            ),
            backendKey = "DIAGNOSE",
        )
    }

    private fun mode(file: File): Int = Os.lstat(file.absolutePath).st_mode and 0b111111111

    /** Test-only recovery after an intentional fail-closed case. */
    private fun cleanup(root: File) {
        if (!root.exists()) return
        Files.walk(root.toPath()).use { paths ->
            paths.filter(Files::isDirectory).forEach { directory ->
                Os.chmod(directory.toString(), OsConstants.S_IRWXU)
            }
        }
        assertTrue(root.deleteRecursively())
    }
}
