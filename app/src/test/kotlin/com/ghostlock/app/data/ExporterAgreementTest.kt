package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File
import java.nio.file.Files

/**
 * Cross-checks the Gradle exporter output against the app's native documents.
 * Both share `:profile-core`. It asserts the exported set equals the index's
 * non-template entries, then compares each file byte-for-byte.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ExporterAgreementTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val pair = CpuPair(primary = 0, consumer = 1)

    @Test
    fun `exporter output matches the app native documents`() = runBlocking {
        val exporterDir = File("../build/kernel-profiles")
        assertTrue(
            "exporter output missing; run :profile-core:exportKernelProfiles",
            exporterDir.isDirectory,
        )

        val index = HoconSupport.parseValue(
            AssetConfigLoader(context).load("kernel_profiles/index.conf"),
        ).asValueMap() ?: error("index.conf is not an object")
        val expected = index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap() }
            .mapNotNull { it["release"] as? String }
            .filterNot { it.endsWith("-template") }
            .toSortedSet()
        val actual = exporterDir.listFiles { file -> file.isFile && file.name.endsWith(".bin") }
            .orEmpty().map { it.name.removeSuffix(".bin") }.toSortedSet()
        assertEquals("exported set must match the index (excluding templates)", expected, actual)

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
            for (release in expected) {
                val appBytes = controller.nativeDocument(controller.load(release, pair))
                assertNotNull("$release: app has no native document", appBytes)
                assertArrayEquals(
                    "$release: exporter differs from the app",
                    appBytes,
                    File(exporterDir, "$release.bin").readBytes(),
                )
            }
        } finally {
            root.deleteRecursively()
        }
    }
}
