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
 *
 * The exporter has no session CPU selection, so the app side is loaded with the
 * pair the profile itself recommends: that is what a device resolves before the
 * user picks a pair, and it is what `--load-prebuilt-profile` must match.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ExporterAgreementTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val loader = AssetConfigLoader(context)
    private val exporterDir = File("../build/kernel-profiles")

    /** The MT6878 profile that pins the race pair to the big cluster. */
    private val mt6878Release = "6.1.157-android14-11-ga8b0b542991e-ab15601211"

    @Test
    fun `exporter output matches the app native documents`() = runBlocking {
        assertTrue(
            "exporter output missing; run :profile-core:exportKernelProfiles",
            exporterDir.isDirectory,
        )

        val expected = indexReleases()
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
                val pair = recommendedPair(release)
                val appBytes = controller.nativeDocument(controller.load(release, pair))
                assertNotNull("$release: app has no native document", appBytes)
                assertArrayEquals(
                    "$release: exporter differs from the app (pair ${pair.primary}/${pair.consumer})",
                    appBytes,
                    File(exporterDir, "$release.bin").readBytes(),
                )
            }
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `exported profile carries the profile recommendation, not a frozen 0-1`() {
        val bytes = File(exporterDir, "$mt6878Release.bin")
        assertTrue(
            "exporter output missing; run :profile-core:exportKernelProfiles",
            bytes.isFile,
        )
        val document = NativeProfileDocument.fromBinary(bytes.readBytes())
        assertNotNull("$mt6878Release: exported binary did not decode", document)
        assertEquals(
            "$mt6878Release: export must keep the profile's big-cluster pair",
            4u,
            document!!.execution.recommendedMainCpu,
        )
        assertEquals(
            "$mt6878Release: export must keep the profile's big-cluster pair",
            5u,
            document.execution.recommendedConsumerCpu,
        )
    }

    private fun indexReleases(): Set<String> {
        val index = HoconSupport.parseValue(loader.load("kernel_profiles/index.conf"))
            .asValueMap() ?: error("index.conf is not an object")
        return index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap() }
            .mapNotNull { it["release"] as? String }
            .filterNot { it.endsWith("-template") }
            .toSortedSet()
    }

    /**
     * The pair the app resolves when the user has not picked one: the profile's
     * own `recommended_cpus`, else the shared `execution-tuning.conf` default.
     */
    private fun recommendedPair(release: String): CpuPair {
        val file = indexFile(release) ?: error("index.conf has no file for $release")
        val profile = HoconSupport.parseValue(loader.load("kernel_profiles/$file")).asValueMap()
            ?: error("$release: profile is not an object")
        val declared = profile["execution"].asValueMap()?.get("recommended_cpus").asValueMap()
        val tuning = HoconSupport.parseValue(loader.load("kernel_profiles/execution-tuning.conf"))
            .asValueMap()?.get("execution").asValueMap()?.get("recommended_cpus").asValueMap()
        fun slot(key: String, fallback: Int): Int =
            ((declared?.get(key) ?: tuning?.get(key)) as? Number)?.toInt() ?: fallback
        return CpuPair(primary = slot("main", 0), consumer = slot("consumer", 1))
    }

    private fun indexFile(release: String): String? {
        val index = HoconSupport.parseValue(loader.load("kernel_profiles/index.conf"))
            .asValueMap() ?: error("index.conf is not an object")
        return index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap() }
            .firstOrNull { it["release"] == release }
            ?.get("file") as? String
    }
}
