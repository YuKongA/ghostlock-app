package com.ghostlock.app.data.run

import android.os.ParcelFileDescriptor
import com.ghostlock.app.composition.backend.PluginObjectRef
import com.ghostlock.app.core.run.RunInputCutoff
import com.ghostlock.app.data.plugin.PluginObjectPfdLease
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.run.RunCapability
import java.io.File
import java.nio.file.Files
import com.ghostlock.plugin.core.PluginContentSha256
import com.ghostlock.plugin.core.PluginPackageIdentity
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** Android freezer publication cleanup and descriptor-ownership tests. */
@RunWith(RobolectricTestRunner::class)
class AndroidRunInputFreezerTest {
    /** A diagnostic freeze transfers one master into a lease and deletes only after duplication. */
    @Test
    fun diagnosticFreezeOwnsProfileUntilLeaseClose() = runBlocking {
        val context = RuntimeEnvironment.getApplication()
        val freezer = freezer(context)
        val lease = freezer.freeze(
            DIAGNOSTIC_SPEC,
            cutoff(DIAGNOSTIC_SPEC.key, PayloadDefinition(items = emptyList())),
        )
        val artifact = lease.input.inputArtifacts.single()
        val profileFiles = inputFiles(context.noBackupFilesDir)
        val published = profileFiles.single { file -> file.name.startsWith(lease.input.runId) }
        val duplicate = lease.duplicate(artifact.sourceToken)

        assertTrue(published.isFile)
        assertTrue(readText(duplicate).contains("\"backend_key\":\"diagnose\""))
        lease.close()

        assertFalse(published.exists())
        assertTrue(readText(duplicate).contains("\"schema_version\":3"))
        duplicate.close()
        lease.close()
    }

    /** Snapshot and manager-resolution failures remove the profile created before either operation. */
    @Test
    fun failuresAfterExclusiveProfileCreationLeaveNoPartialFile() {
        val context = RuntimeEnvironment.getApplication()
        val freezer = freezer(context)
        val before = inputFiles(context.noBackupFilesDir).map(File::getName).toSet()
        val failedPayload = CompletableDeferred<PayloadDefinition>().also { deferred ->
            deferred.completeExceptionally(IllegalStateException("snapshot"))
        }

        assertThrows(IllegalStateException::class.java) {
            runBlocking {
                freezer.freeze(
                    DIAGNOSTIC_SPEC,
                    RunInputCutoff(
                        backendKey = DIAGNOSTIC_SPEC.key,
                        pluginObjectRef = PLUGIN_OBJECT,
                        payload = failedPayload,
                        finish = CompletableDeferred(FinishDefinition()),
                    ),
                )
            }
        }
        assertEquals(before, inputFiles(context.noBackupFilesDir).map(File::getName).toSet())

        assertThrows(IllegalArgumentException::class.java) {
            runBlocking {
                freezer.freeze(
                    PRODUCT_SPEC,
                    cutoff(PRODUCT_SPEC.key, PayloadDefinition(rootManager = "unsupported")),
                )
            }
        }
        assertEquals(before, inputFiles(context.noBackupFilesDir).map(File::getName).toSet())
    }

    /** Returns a completed actor cutoff for one backend key. */
    private fun cutoff(key: String, payload: PayloadDefinition): RunInputCutoff = RunInputCutoff(
        backendKey = key,
        pluginObjectRef = PLUGIN_OBJECT,
        payload = CompletableDeferred(payload),
        finish = CompletableDeferred(FinishDefinition()),
    )

    /** Opens one disposable regular object with the exact cutoff content root. */
    private fun freezer(context: android.content.Context): AndroidRunInputFreezer =
        AndroidRunInputFreezer(context, RootManagerArtifactResolver(context)) { ref ->
            val file = Files.writeString(Files.createTempFile("freezer-plugin", ".zip"), "plugin")
            PluginObjectPfdLease(
                ref.pluginContentSha256,
                ParcelFileDescriptor.open(file.toFile(), ParcelFileDescriptor.MODE_READ_ONLY),
            )
        }

    /** Reads one descriptor through a duplicate so the caller retains explicit close ownership. */
    private fun readText(descriptor: ParcelFileDescriptor): String =
        ParcelFileDescriptor.AutoCloseInputStream(
            ParcelFileDescriptor.dup(descriptor.fileDescriptor),
        ).use { input -> input.readBytes().toString(Charsets.UTF_8) }

    /** Lists only current profile fixtures without deleting unrelated test state. */
    private fun inputFiles(noBackupDirectory: File): List<File> =
        File(noBackupDirectory, "run-input").listFiles().orEmpty().toList()

    private companion object {
        val DIAGNOSTIC_SPEC = BackendDescriptor(
            key = "diagnose",
            selectionPolicy = BackendSelectionPolicy.SELECTABLE,
            abortPolicy = AbortPolicy.SAFE_DURING_RUN,
            executionDomain = ExecutionDomain.APP,
            runCapability = RunCapability.DIAGNOSTIC_ONLY,
        )
        val PRODUCT_SPEC = DIAGNOSTIC_SPEC.copy(
            key = "product",
            runCapability = RunCapability.PRODUCT_ROOT,
        )
        val PLUGIN_OBJECT = PluginObjectRef(
            packageIdentity = PluginPackageIdentity("com.ghostlock.firstparty", "fixture"),
            variant = "android-arm64-v8a",
            versionCode = 1L,
            pluginContentSha256 = PluginContentSha256("a".repeat(64)),
        )
    }
}
