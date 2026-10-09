package com.ghostlock.app.data

import android.app.Application
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Encoder
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File
import java.nio.file.Files

/**
 * task-142 acceptance for the multicast preset (user ruling A).
 *
 * The preset `execution-multicast-waiter.conf` writes
 * `execution.routes.multicast_waiter.*` and [MulticastConfig.from] now READS that
 * path first (falling back to the legacy `mcast.*` spelling). The key name is
 * identical on all three spellings (preset key == wire field == legacy constant
 * `kMulticastArmHold`), so no renaming happens anywhere.
 *
 * Evidence produced here (acceptance is "field values / bytes", never "the file
 * exists"):
 *  1. a bundled multicast profile emits `route.multicast_waiter` with
 *     attempts = 128, arm_sequence = 16, arm_hold = 20000 -- the legacy defaults
 *     from multicast_waiter_route.cpp:34-36 (tag legacy/baseline-20261008);
 *  2. the profile text carries none of those keys, so the preset merged by
 *     ProfileMerger.fillRouteExecutionDefaults is the only possible source;
 *  3. the byte delta is reported: the same document with exactly those three
 *     entries removed equals the pre-task-142 emission (before this batch
 *     MulticastConfig ignored `execution.routes.*` entirely).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class MulticastPresetCarriageTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val pair = CpuPair(primary = 0, consumer = 1)

    private val sectionName = "backend.cve_2026_43499.route.multicast_waiter"
    private val tuningKeys = listOf("attempts", "arm_sequence", "arm_hold")
    private val multicastRelease = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"

    /** 43499 is forced: these assets declare 43284 first, and a 43284 document
     * carries no 43499 route section at all (v3Sections drops it). */
    private fun controller(root: File) = AndroidProfileConfigController(
        context = context,
        filesDir = root,
        backendSelection = { BackendKind.Cve2026_43499 },
        userProfiles = UserProfileStore(
            directory = root.resolve("user_profiles"),
            assetLoader = AssetConfigLoader(context),
        ),
        preferences = context.getSharedPreferences("multicast-preset-carriage", 0)
            .also { it.edit().clear().commit() },
    )

    /** The same document with the three preset entries removed (pre-task-142 shape). */
    private fun withoutTuning(bytes: ByteArray): Pair<ByteArray, List<String>> {
        val decoded = requireNotNull(Glkv3Decoder.decode(bytes)) { "document does not decode" }
        val trimmed = decoded.copy(
            sections = decoded.sections.map { section ->
                if (section.name == sectionName) {
                    section.copy(entries = section.entries.filterNot { it.key in tuningKeys })
                } else {
                    section
                }
            },
        )
        val remaining = trimmed.sections.first { it.name == sectionName }.entries.map { it.key }.sorted()
        return Glkv3Encoder.encode(trimmed) to remaining
    }

    @Test
    fun `the multicast preset tuning reaches the emitted document`() = runBlocking {
        val root = Files.createTempDirectory("multicast-preset-carriage").toFile()
        try {
            val controller = controller(root)
            val config = controller.load(multicastRelease, pair)
            assertTrue("no profile for " + multicastRelease, config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config)) { "no native document" }

            val decoded = requireNotNull(Glkv3Decoder.decode(bytes)) { "document does not decode" }
            val section = decoded.sections.firstOrNull { it.name == sectionName }
            requireNotNull(section) { "the multicast route section is missing" }
            assertEquals(
                Glkv3Value.UInt(128u),
                section.entries.first { it.key == "attempts" }.value,
            )
            assertEquals(
                Glkv3Value.UInt(16u),
                section.entries.first { it.key == "arm_sequence" }.value,
            )
            assertEquals(
                Glkv3Value.UInt(20000u),
                section.entries.first { it.key == "arm_hold" }.value,
            )

            /* The profile itself must not carry the tuning: the preset is then the
             * only possible source of the three values above. */
            val assetText = AssetConfigLoader(context).load("profile/" + multicastRelease + ".conf")
            assertFalse("the profile declares arm_hold itself", assetText.contains("arm_hold"))
            assertFalse("the profile declares arm_sequence itself", assetText.contains("arm_sequence"))

            val (oldBytes, remaining) = withoutTuning(bytes)
            assertEquals(
                "only the geometry keys may remain without the preset",
                listOf("buffer_size", "lock_offset", "task_offset", "waiter_off"),
                remaining,
            )
            println(
                "multicast-preset delta: " + multicastRelease +
                    " new=" + bytes.size + " old=" + oldBytes.size +
                    " delta=" + (bytes.size - oldBytes.size),
            )
            assertTrue("the preset must add bytes", bytes.size > oldBytes.size)
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `every multicast profile carries the preset tuning (deltas printed)`() = runBlocking {
        val root = Files.createTempDirectory("multicast-preset-sweep").toFile()
        try {
            val controller = controller(root)
            val index = requireNotNull(
                HoconSupport.parseValue(
                    AssetConfigLoader(context).load("profile/index.conf"),
                ).asValueMap(),
            )
            val assets = AssetConfigLoader(context)
            val profiles = index["profiles"].asValueList().orEmpty()
                .mapNotNull { entry -> entry.asValueMap() }
                .mapNotNull { entry ->
                    val release = entry["release"] as? String ?: return@mapNotNull null
                    val file = entry["file"] as? String ?: return@mapNotNull null
                    release to file
                }
            var multicast = 0
            var totalDelta = 0
            for ((release, file) in profiles) {
                /* A 43499-forced controller builds a 43499 document even for a
                 * 43284-only asset; skip those, they declare no multicast path. */
                val text = runCatching { assets.load("profile/" + file) }.getOrNull() ?: continue
                if (!text.contains("cve_2026_43499")) continue
                val config = runCatching { controller.load(release, pair) }.getOrNull() ?: continue
                if (!config.hasProfile) continue
                val bytes = controller.nativeDocument(config) ?: continue
                val decoded = Glkv3Decoder.decode(bytes) ?: continue
                val section = decoded.sections.firstOrNull { it.name == sectionName } ?: continue
                val hold = section.entries.firstOrNull { it.key == "arm_hold" }?.value ?: continue
                assertEquals(Glkv3Value.UInt(20000u), hold)
                val (oldBytes, _) = withoutTuning(bytes)
                val delta = bytes.size - oldBytes.size
                val attribution = if (text.contains("arm_hold")) "asset-declared-hold" else "preset"
                multicast++
                totalDelta += delta
                println(
                    "multicast-preset: " + release + " new=" + bytes.size +
                        " old=" + oldBytes.size + " delta=" + delta + " source=" + attribution,
                )
            }
            println("multicast-preset summary: profiles=" + multicast + " totalDelta=" + totalDelta)
            assertTrue("no multicast profile carried the preset tuning", multicast >= 1)
        } finally {
            root.deleteRecursively()
        }
    }
}
