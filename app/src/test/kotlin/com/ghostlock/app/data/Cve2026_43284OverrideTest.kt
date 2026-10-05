package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Document
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ProfileFieldNode
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
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
 * S4 R4: the 43284 policy paths and handshake tuning are editable in the
 * advanced settings, validated, persisted as sparse HOCON overrides and carried
 * into the encoded GLKv3 document as `str`/numeric entries.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class Cve2026_43284OverrideTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val release = "6.1.118-android14-11-ga3b9c44908dd-ab13320413"
    private val pair = CpuPair(primary = 0, consumer = 1)

    private fun withController(name: String, block: suspend (AndroidProfileConfigController) -> Unit) =
        runBlocking {
            val root = Files.createTempDirectory(name).toFile()
            try {
                val controller = AndroidProfileConfigController(
                    context = context,
                    filesDir = root,
                    userProfiles = UserProfileStore(
                        directory = root.resolve("user_profiles"),
                        assetLoader = AssetConfigLoader(context),
                    ),
                    preferences = context.getSharedPreferences(name, 0)
                        .also { it.edit().clear().commit() },
                    backendSelection = { BackendKind.Cve2026_43284 },
                )
                block(controller)
            } finally {
                root.deleteRecursively()
            }
        }

    private fun flatten(nodes: List<ProfileFieldNode>): List<ProfileFieldNode> =
        nodes.flatMap { if (it.isGroup) flatten(it.children) else listOf(it) }

    @Test
    fun `43284 selection surfaces the policy and tuning fields`() =
        withController("43284-fields") { controller ->
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val paths = flatten(config.roots).map { it.path }.toSet()
            for (path in Cve2026_43284Fields.EditablePaths) {
                assertTrue(path + " is missing from the advanced tree", path in paths)
            }
        }

    @Test
    fun `selection tokens stay out of the advanced tree`() =
        withController("43284-tokens") { controller ->
            val config = controller.load(release, pair)
            val paths = flatten(config.roots).map { it.path }.toSet()
            /* backend.steps is a selection token owned by its dedicated
             * control, never the generic string editor (R6a dropped fallback). */
            assertFalse("fallback.to" in paths)
            assertFalse("backend.steps" in paths)
        }

    @Test
    fun `string overrides reach the wire as GLKv3 str entries`() =
        withController("43284-wire") { controller ->
            controller.updateAdvanced(
                release,
                pair,
                mapOf(
                    "backend.cve_2026_43284.lkm_path" to
                        "/data/local/tmp/helper_custom.ko",
                    "backend.cve_2026_43284.carrier_path" to
                        "/vendor/lib64/libbinderdebug.so",
                ),
            )
            val config = controller.load(release, pair)
            assertTrue(
                "unexpected invalid paths: " + config.invalidPaths,
                config.invalidPaths.isEmpty(),
            )

            val decoded = Glkv3Decoder.decode(requireNotNull(controller.nativeDocument(config)))
            assertNotNull(decoded)
            assertEquals(
                Glkv3Value.Str("/data/local/tmp/helper_custom.ko"),
                entry(decoded!!, "backend.cve_2026_43284", "lkm_path"),
            )
            assertEquals(
                Glkv3Value.Str("/vendor/lib64/libbinderdebug.so"),
                entry(decoded, "backend.cve_2026_43284", "carrier_path"),
            )
        }

    @Test
    fun `clearing a string override suppresses the baseline value`() =
        withController("43284-clear") { controller ->
            val section = "backend.cve_2026_43284"
            controller.updateAdvanced(release, pair, mapOf(section + ".lkm_path" to "/tmp/x.ko"))
            val set = controller.load(release, pair)
            assertEquals(
                Glkv3Value.Str("/tmp/x.ko"),
                entry(
                    requireNotNull(Glkv3Decoder.decode(requireNotNull(controller.nativeDocument(set)))),
                    section,
                    "lkm_path",
                ),
            )

            /* An empty draft is an explicit "absent" request, so the override
             * survives the rebuild and the key leaves the document. */
            controller.updateAdvanced(release, pair, mapOf(section + ".lkm_path" to ""))
            val cleared = controller.load(release, pair)
            assertEquals(emptySet<String>(), cleared.invalidPaths)
            val decoded = Glkv3Decoder.decode(requireNotNull(controller.nativeDocument(cleared)))
            assertNotNull(decoded)
            assertFalse(
                decoded!!.sections.firstOrNull { it.name == section }
                    ?.entries.orEmpty().any { it.key == "lkm_path" },
            )
        }

    @Test
    fun `out-of-range handshake tuning is reported invalid`() =
        withController("43284-uint32") { controller ->
            for (value in listOf(-1L, 0x1_0000_0000L)) {
                controller.updateAdvanced(
                    release,
                    pair,
                    mapOf("backend.cve_2026_43284.wait_timeout_ms" to value),
                )
                val config = controller.load(release, pair)
                assertTrue(
                    "wait_timeout_ms=" + value + " was not surfaced: " + config.invalidPaths,
                    "backend.cve_2026_43284.wait_timeout_ms" in config.invalidPaths,
                )
            }
        }

    @Test
    fun `malformed policy path is reported invalid`() =
        withController("43284-path") { controller ->
            for (bad in listOf("vendor/lib64/x.so", "/" + "a".repeat(256))) {
                controller.updateAdvanced(
                    release,
                    pair,
                    mapOf("backend.cve_2026_43284.carrier_path" to bad),
                )
                val config = controller.load(release, pair)
                assertTrue(
                    "carrier_path=" + bad.length + " was not surfaced: " + config.invalidPaths,
                    "backend.cve_2026_43284.carrier_path" in config.invalidPaths,
                )
            }
        }

    private fun entry(document: Glkv3Document, section: String, key: String): Glkv3Value =
        document.sections
            .first { it.name == section }
            .entries
            .first { it.key == key }
            .value
}
