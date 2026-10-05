package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.plugin.EnabledPlugin
import com.ghostlock.app.data.plugin.PluginEmission
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginProbe
import com.ghostlock.app.data.plugin.PluginValue
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.domain.model.CpuPair
import java.io.File
import java.nio.file.Files
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * P1 plugin wire shape, end to end.
 *
 * The canonical shape is ONE section named `plugin` whose keys are spelled
 * `<id>.<field>` (the id may itself contain dots) — never a `plugin.<id>`
 * section. The assertions below pin the exact section name and key spellings,
 * because the native side (plugin/schema.hpp + plugin/wire.cpp) validates them.
 *
 * The encoded document is also compared byte-for-byte against [GOLDEN]
 * (`app/src/test/resources/plugin-wire-shape-golden.bin`), the single artifact
 * native-core's host test reads to prove `parse` + `validate_plugin_wire`
 * accept what this encoder produces. To regenerate it intentionally, delete the
 * file and re-run this test once (it writes the file and fails with a message);
 * the second run asserts.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class PluginEmissionDocumentTest {

    private val context: Application = RuntimeEnvironment.getApplication()
    private val release = "6.1.118-android14-11-ga3b9c44908dd-ab13320413"
    private val pair = CpuPair(0, 1)

    private val descriptor = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read\n" +
            "plugin\tdemo.plugin\t1.0\t1\t64\t" + DIGEST +
            "\tpost_terminal\tkernel_read\n" +
            "param\tdemo.plugin\tthreshold\tuint\t0\t200\tdoc\n" +
            "param\tdemo.plugin\tmode\tstr\t0\tauto\tdoc\n",
    )

    private fun entry(enabled: Boolean) = PluginManifestEntry(
        id = "demo.plugin",
        version = "1.0",
        abiVersion = 1,
        sha256 = DIGEST,
        modulePath = "demo.plugin/1.0/demo.plugin.so",
        enabled = enabled,
        stage = "post_terminal",
        importedAtMs = 7L,
    )

    private fun controller(root: File, emissions: () -> List<EnabledPlugin>) =
        AndroidProfileConfigController(
            context = context,
            filesDir = root,
            userProfiles = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            ),
            preferences = context.getSharedPreferences("plugin-emission", 0)
                .also { it.edit().clear().commit() },
            backendSelection = { BackendKind.Cve2026_43499 },
            pluginSelection = emissions,
        )

    private fun pluginSection(bytes: ByteArray) =
        Glkv3Decoder.decode(bytes)?.sections?.firstOrNull { it.name == "plugin" }

    /** Builds the document the shape golden is taken from. */
    private fun goldenDocument(root: File): ByteArray = runBlocking {
        val controller = controller(root) { listOf(EnabledPlugin(entry(true), descriptor)) }
        controller.setPluginParam(release, "demo.plugin", "threshold", PluginValue.UInt(7u))
        val config = controller.load(release, pair)
        requireNotNull(controller.nativeDocument(config))
    }

    @Test
    fun editingRoundTrip() = runBlocking {
        val root = Files.createTempDirectory("glk-plugin-emission").toFile()
        val controller = controller(root) { listOf(EnabledPlugin(entry(true), descriptor)) }
        try {
            controller.setPluginParam(release, "demo.plugin", "threshold", PluginValue.UInt(7u))
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
            /* Exactly one plugin section, named plugin. */
            assertTrue(decoded.sections.none { it.name.startsWith("plugin.") })
            val section = requireNotNull(pluginSection(bytes))
            assertEquals(
                setOf(
                    "demo.plugin.enabled",
                    "demo.plugin.stage",
                    "demo.plugin.module_path",
                    "demo.plugin.module_hash",
                    "demo.plugin.params.threshold",
                ),
                section.entries.map { it.key }.toSet(),
            )
            assertEquals(
                Glkv3Value.Bool(true),
                section.entries.first { it.key == "demo.plugin.enabled" }.value,
            )
            assertEquals(
                Glkv3Value.Str("post_terminal"),
                section.entries.first { it.key == "demo.plugin.stage" }.value,
            )
            assertEquals(
                Glkv3Value.Str("demo.plugin/1.0/demo.plugin.so"),
                section.entries.first { it.key == "demo.plugin.module_path" }.value,
            )
            assertEquals(
                Glkv3Value.Str(DIGEST),
                section.entries.first { it.key == "demo.plugin.module_hash" }.value,
            )
            assertEquals(
                Glkv3Value.UInt(7u),
                section.entries.first { it.key == "demo.plugin.params.threshold" }.value,
            )
            /* Only explicit overrides ride, and extract.* is never emitted. */
            assertTrue(section.entries.none { it.key == "demo.plugin.params.mode" })
            assertTrue(section.entries.none { it.key.contains(".extract.") })

            controller.setPluginParam(release, "demo.plugin", "threshold", null)
            val cleared = controller.load(release, pair)
            val clearedSection = requireNotNull(
                Glkv3Decoder.decode(requireNotNull(controller.nativeDocument(cleared)))
                    ?.sections?.firstOrNull { it.name == "plugin" },
            )
            assertTrue(clearedSection.entries.none { it.key == "demo.plugin.params.threshold" })
            assertEquals(
                Glkv3Value.Bool(true),
                clearedSection.entries.first { it.key == "demo.plugin.enabled" }.value,
            )
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun disabledPluginEmitsNoSection() = runBlocking {
        val root = Files.createTempDirectory("glk-plugin-emission-off").toFile()
        val controller = controller(root) { emptyList() }
        try {
            val config = controller.load(release, pair)
            assertNull(pluginSection(requireNotNull(controller.nativeDocument(config))))
            assertNull(PluginEmission.of(entry(false), descriptor))
        } finally {
            root.deleteRecursively()
        }
    }

    /**
     * The cross-language artifact: one enabled plugin, three static fields and
     * one parameter override, encoded by this App's encoder. Native-core's host
     * test reads the same file and asserts parse + validate_plugin_wire accept
     * it, so a shape change cannot pass on one side only.
     */
    @Test
    fun matchesThePluginWireShapeGolden() {
        val root = Files.createTempDirectory("glk-plugin-golden").toFile()
        val bytes = try {
            goldenDocument(root)
        } finally {
            root.deleteRecursively()
        }
        val golden = javaClass.classLoader?.getResourceAsStream(GOLDEN)?.use { it.readBytes() }
        if (golden == null) {
            val target = File(System.getProperty("user.dir"), "src/test/resources/" + GOLDEN)
            target.parentFile?.mkdirs()
            target.writeBytes(bytes)
            org.junit.Assert.fail(
                "plugin wire shape golden was missing; wrote " + target.path +
                    " - re-run this test to assert against it",
            )
        }
        assertArrayEquals(
            "the encoded plugin document drifted from " + GOLDEN,
            golden,
            bytes,
        )
        /* The golden must carry the canonical shape, not just equal bytes. */
        val section = requireNotNull(pluginSection(bytes))
        assertTrue(section.entries.any { it.key == "demo.plugin.enabled" })
        assertTrue(section.entries.any { it.key == "demo.plugin.params.threshold" })
    }

    private companion object {
        const val DIGEST = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        const val GOLDEN = "plugin-wire-shape-golden.bin"
    }
}
