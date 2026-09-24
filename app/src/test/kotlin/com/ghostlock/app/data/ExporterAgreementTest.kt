package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File
import java.nio.file.Files

/**
 * Cross-checks the Gradle exporter output against the app's native documents.
 * Both share `:profile-core`, so this stays a guard against future drift. It
 * runs only when the exporter output exists (after `exportKernelProfiles`).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ExporterAgreementTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val pair = CpuPair(primary = 0, consumer = 1)

    @Test
    fun `exporter output matches the app native documents`() = runBlocking {
        val exporterDir = File("../build/kernel-profiles")
        org.junit.Assert.assertTrue(
            "exporter output missing; run :profile-core:exportKernelProfiles",
            exporterDir.isDirectory,
        )

        val root = Files.createTempDirectory("exporter-agreement").toFile()
        try {
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = UserProfileStore(
                    directory = root.resolve("user_profiles"),
                    assetLoader = AssetConfigLoader(context),
                ),
                preferences = context.getSharedPreferences("exporter-agreement", 0)
                    .also { it.edit().clear().commit() },
            )
            for (file in exporterDir.listFiles { it -> it.isFile && it.name.endsWith(".bin") }.orEmpty()) {
                val release = file.name.removeSuffix(".bin")
                if (release.endsWith("-template")) continue
                val config = controller.load(release, pair)
                val appBytes = controller.nativeDocument(config)
                assertNotNull("$release: app has no native document", appBytes)
                assertArrayEquals("$release: exporter differs from the app", appBytes, file.readBytes())
            }
        } finally {
            root.deleteRecursively()
        }
    }
}
