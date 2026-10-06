/*
 * COMMENTED OUT (user ruling 2026-10-05): this suite only served plugin emission,
 * which is withdrawn while the plugin design is redone. The file is kept so the
 * coverage comes back with the feature.
 *
 * Restore = uncomment the body below (and the emission code it exercises).
 */

// package com.ghostlock.app.data
//
// import android.app.Application
// import androidx.core.content.edit
// import com.ghostlock.app.data.component.BackendKind
// import com.ghostlock.app.data.plugin.EnabledPlugin
// import com.ghostlock.app.data.plugin.PluginEmission
// import com.ghostlock.app.data.plugin.PluginManifestEntry
// import com.ghostlock.app.data.plugin.PluginProbe
// import com.ghostlock.app.data.plugin.PluginSelection
// import com.ghostlock.app.data.plugin.PluginValue
// import com.ghostlock.app.data.profile.Glkv3Decoder
// import com.ghostlock.app.domain.model.CpuPair
// import java.io.File
// import java.nio.file.Files
// import kotlinx.coroutines.runBlocking
// import org.junit.Assert.assertEquals
// import org.junit.Assert.assertNotNull
// import org.junit.Assert.assertNull
// import org.junit.Assert.assertTrue
// import org.junit.Test
// import org.junit.runner.RunWith
// import org.robolectric.RobolectricTestRunner
// import org.robolectric.RuntimeEnvironment
// import org.robolectric.annotation.Config
//
// /**
//  * The document build with plugin/custom-handoff emission PAUSED (user instruction
//  * 2026-10-05, see [PLUGIN_AND_PAYLOAD_EMISSION]).
//  *
//  * What used to be the "the encoder writes the canonical plugin shape" suite is now
//  * the "the encoder writes NOTHING for plugins" suite: an enabled plugin, its
//  * overrides and any run selection must leave the document without a `plugin`
//  * section, and a blocked selection must no longer block the run — because the
//  * selection is not even resolved while paused (no probe runs either).
//  *
//  * The plugin SHAPE itself is still covered where it lives: the pure model
//  * (profile-core PluginEmissionTest), the wire shape golden
//  * (app/src/test/resources/plugin-wire-shape-golden.bin, kept untouched as the
//  * reference for restoring) and the probe/validator tests.
//  */
// @RunWith(RobolectricTestRunner::class)
// @Config(sdk = [35])
// class PluginEmissionDocumentTest {
//
//     private val context: Application = RuntimeEnvironment.getApplication()
//     private val release = "6.1.118-android14-11-ga3b9c44908dd-ab13320413"
//     private val pair = CpuPair(0, 1)
//
//     private val descriptor = PluginProbe.parse(
//         "host_abi\t1\n" +
//             "countermeasures_root\tcountermeasures\n" +
//             "host_stages\tpost_terminal\n" +
//             "host_caps\tkernel_read\n" +
//             "plugin\tdemo.plugin\t1.0\t1\t64\t" + DIGEST +
//             "\tpost_terminal\tkernel_read\n" +
//             "param\tdemo.plugin\tthreshold\tuint\t0\t200\tdoc\n" +
//             "param\tdemo.plugin\tmode\tstr\t0\tauto\tdoc\n" +
//             "extract\tdemo.plugin\toffset\tuint\t1\t-\tfrom the boot image\n",
//     )
//
//     private fun entry(enabled: Boolean) = PluginManifestEntry(
//         id = "demo.plugin",
//         version = "1.0",
//         abiVersion = 1u,
//         sha256 = DIGEST,
//         modulePath = "demo.plugin/1.0/demo.plugin.so",
//         enabled = enabled,
//         stage = "post_terminal",
//         importedAtMs = 7L,
//     )
//
//     private fun controller(root: File, emissions: suspend () -> PluginSelection) =
//         AndroidProfileConfigController(
//             context = context,
//             filesDir = root,
//             userProfiles = UserProfileStore(
//                 directory = root.resolve("user_profiles"),
//                 assetLoader = AssetConfigLoader(context),
//             ),
//             preferences = context.getSharedPreferences("plugin-emission", 0)
//                 .also { it.edit().clear().commit() },
//             backendSelection = { BackendKind.Cve2026_43499 },
//             pluginSelection = emissions,
//         )
//
//     private fun pluginSection(bytes: ByteArray) =
//         Glkv3Decoder.decode(bytes)?.sections?.firstOrNull { it.name == "plugin" }
//
//     /**
//      * The pause itself: an ENABLED plugin with a parameter and an extractor
//      * override contributes no key — and the descriptor is not consulted, so no
//      * error can surface either.
//      */
//     @Test
//     fun pluginEmissionIsPausedSoNothingReachesTheDocument() = runBlocking {
//         assertTrue("the pause switch is expected to be OFF", !PLUGIN_AND_PAYLOAD_EMISSION)
//         val root = Files.createTempDirectory("glk-plugin-paused").toFile()
//         var consulted = 0
//         val controller = controller(root) {
//             consulted++
//             PluginSelection.Ready(listOf(EnabledPlugin(entry(true), descriptor)))
//         }
//         try {
//             controller.setPluginParam(release, "demo.plugin", "threshold", PluginValue.UInt(7u))
//             controller.setPluginExtract(release, "demo.plugin", "offset", PluginValue.UInt(4096u))
//             val config = controller.load(release, pair)
//             assertTrue(config.hasProfile)
//             val bytes = requireNotNull(controller.nativeDocument(config))
//             assertNull("no plugin section may be emitted while paused", pluginSection(bytes))
//             /* No `plugin.<id>` section either: the shape is a single section. */
//             assertTrue(
//                 requireNotNull(Glkv3Decoder.decode(bytes)).sections.none {
//                     it.name.startsWith("plugin.")
//                 },
//             )
//             assertEquals(emptyList<String>(), config.pluginErrors)
//             /* The selection is never even resolved: the probe cannot run. */
//             assertEquals(0, consulted)
//         } finally {
//             root.deleteRecursively()
//         }
//     }
//
//     /**
//      * P0 result contract, dormant while paused: a plugin the probe could not
//      * describe used to fail the build with a reason. With emission off it is
//      * simply not resolved, so the run must not be blocked by it.
//      */
//     @Test
//     fun aBlockedSelectionIsInertWhilePaused() = runBlocking {
//         val root = Files.createTempDirectory("glk-plugin-blocked").toFile()
//         val controller = controller(root) {
//             PluginSelection.Blocked(listOf("glk.probe: the module file is missing"))
//         }
//         try {
//             val config = controller.load(release, pair)
//             assertTrue(config.hasProfile)
//             val bytes = requireNotNull(controller.nativeDocument(config))
//             assertNull(pluginSection(bytes))
//             assertTrue(config.pluginErrors.isEmpty())
//         } finally {
//             root.deleteRecursively()
//         }
//     }
//
//     /** No run selection can put a plugin on the wire while paused. */
//     @Test
//     fun theRunSelectionCannotEmitWhilePaused() = runBlocking {
//         val root = Files.createTempDirectory("glk-plugin-selection").toFile()
//         val controller = controller(root) {
//             PluginSelection.Ready(listOf(EnabledPlugin(entry(true), descriptor)))
//         }
//         try {
//             for (selection in listOf(null, setOf("demo.plugin"), emptySet())) {
//                 val config = controller.load(release, pair)
//                 assertNull(
//                     "selection " + selection + " must not emit",
//                     pluginSection(requireNotNull(controller.nativeDocument(config))),
//                 )
//             }
//         } finally {
//             root.deleteRecursively()
//         }
//     }
//
//     /** A disabled plugin emits nothing — still true, and now trivially so. */
//     @Test
//     fun disabledPluginEmitsNoSection() = runBlocking {
//         val root = Files.createTempDirectory("glk-plugin-emission-off").toFile()
//         val controller = controller(root) { PluginSelection.Ready(emptyList()) }
//         try {
//             val config = controller.load(release, pair)
//             assertNull(pluginSection(requireNotNull(controller.nativeDocument(config))))
//             /* The pure model keeps its own rule: a disabled plugin emits nothing. */
//             assertNull(PluginEmission.of(entry(false), descriptor))
//         } finally {
//             root.deleteRecursively()
//         }
//     }
//
//     /**
//      * The cross-language artifact stays in the tree as the shape reference for
//      * when the feature returns; while paused the App must simply not produce it.
//      */
//     @Test
//     fun theShapeGoldenStaysAsTheReferenceWhilePaused() {
//         val golden = javaClass.classLoader?.getResourceAsStream(GOLDEN)?.use { it.readBytes() }
//         assertNotNull("the plugin wire shape golden must stay in the tree", golden)
//         assertTrue("the golden must not be emptied", requireNotNull(golden).isNotEmpty())
//     }
//
//     private companion object {
//         const val DIGEST = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
//         const val GOLDEN = "plugin-wire-shape-golden.bin"
//     }
// }
