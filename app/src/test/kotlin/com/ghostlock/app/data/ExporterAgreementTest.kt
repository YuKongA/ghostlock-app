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
        /* The Gradle build tree lives under ~/.ghostlock/build (commit 03b4ddc
         * moved it out of iCloud), so the exporter output is the external root
         * build/profiles; the in-tree path is kept for the native build
         * layout and for a developer who exports there. */
        val exporterDir = listOf(
            File("../build/profiles"),
            File(System.getProperty("user.home"), ".ghostlock/build/root/profiles"),
        ).firstOrNull { it.isDirectory }
        assertTrue(
            "exporter output missing; run :profile-core:exportProfiles",
            exporterDir != null,
        )
        val exportDir = requireNotNull(exporterDir)

        val index = HoconSupport.parseValue(
            AssetConfigLoader(context).load("profile/index.conf"),
        ).asValueMap() ?: error("index.conf is not an object")
        val expected = index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap() }
            .mapNotNull { it["release"] as? String }
            .filterNot { it.endsWith("-template") }
            .toSortedSet()
        val actual = exportDir.listFiles { file -> file.isFile && file.name.endsWith(".bin") }
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
            /* SELF-DIAGNOSTICS (permanent, in the failure message - never stdout):
             * on any mismatch report both lengths, the FIRST differing byte offset
             * and a short window from each side, so a one-byte divergence can be
             * located without re-running the whole suite. */
            val mismatches = mutableListOf<String>()
            for (release in expected) {
                val appBytes = controller.nativeDocument(controller.load(release, pair))
                assertNotNull("$release: app has no native document", appBytes)
                val exported = File(exportDir, "$release.bin").readBytes()
                val app = appBytes!!
                val firstDiff = (0 until minOf(app.size, exported.size))
                    .firstOrNull { app[it] != exported[it] }
                if (app.size != exported.size || firstDiff != null) {
                    val at = firstDiff ?: minOf(app.size, exported.size)
                    fun window(text: ByteArray): String {
                        val end = minOf(at + 24, text.size)
                        return (at until end).joinToString(" ") { String.format("%02x", text[it]) }
                    }
                    mismatches += release + " app=" + app.size + "B export=" + exported.size +
                        "B firstDiff@" + at + " app[" + window(app) + "] export[" +
                        window(exported) + "]"
                }
            }
            assertEquals(
                "exporter/app v3 mismatch: " + mismatches.size + " of " + expected.size +
                    " releases, detail=" + mismatches,
                emptyList<String>(),
                mismatches,
            )
        } finally {
            root.deleteRecursively()
        }
    }
}
