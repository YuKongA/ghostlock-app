package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File
import java.nio.file.Files

/**
 * B7/T5: the app-level backend selection reaches the native GLKv3 wire
 * document. The controller injects the selected token into the profile builder;
 * the available 43284 backend carries the sparse pagecache_write + umh_forward
 * triple, while a placeholder backend still fails closed to the 43499 default.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BackendSelectionTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val release = "6.1.118-android14-11-ga3b9c44908dd-ab13320413"
    private val pair = CpuPair(primary = 0, consumer = 1)

    private fun controller(
        name: String,
        backend: BackendKind,
        combination: CombinationSpec? = null,
    ): Pair<AndroidProfileConfigController, File> {
        val root = Files.createTempDirectory(name).toFile()
        val controller = AndroidProfileConfigController(
            context = context,
            filesDir = root,
            userProfiles = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            ),
            preferences = context.getSharedPreferences(name, 0).also { it.edit().clear().commit() },
            backendSelection = { backend },
            combinationSelection = combination?.let { combo -> { combo } },
        )
        return controller to root
    }

    /** The resolved backend token in the production GLKv3 document. */
    private fun backendToken(bytes: ByteArray): String? =
        Glkv3Decoder.decode(bytes)?.backend

    @Test
    fun `selected backend is written into the native document`() = runBlocking {
        val (controller, root) = controller("backend-selection-43499", BackendKind.Cve2026_43499)
        try {
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            assertEquals("cve_2026_43499", backendToken(bytes))
            assertEquals("root_child", Glkv3Decoder.decode(bytes)?.terminal)
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `available 43284 backend reaches the wire with the sparse triple`() = runBlocking {
        val (controller, root) = controller("backend-selection-43284", BackendKind.Cve2026_43284)
        try {
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
            assertEquals("cve_2026_43284", decoded.backend)
            assertEquals("umh_forward", decoded.terminal)
            val steps = decoded.sections
                .first { it.name == "backend.cve_2026_43284" }
                .entries.first { it.key == "steps" }
                .value
            assertEquals(Glkv3Value.Str("umh"), steps)
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `combination token reaches the wire as exactly one string and derives route and terminal`() = runBlocking {
        val (controller, root) = controller(
            "combination-selection",
            BackendKind.Cve2026_43499,
            requireNotNull(CombinationCatalog.resolve("pselect_shizuku")) { "pselect_shizuku" },
        )
        try {
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val decoded = requireNotNull(Glkv3Decoder.decode(requireNotNull(controller.nativeDocument(config))))
            assertEquals("cve_2026_43499", decoded.backend)
            /* The token derives the root terminal and route. */
            assertEquals("root_child", decoded.terminal)
            assertEquals("select_stack", decoded.route)
            val steps = decoded.sections.flatMap { it.entries }.filter { it.key == "steps" }
            assertEquals("exactly one token may ride the wire", 1, steps.size)
            assertEquals(Glkv3Value.Str("pselect_shizuku"), steps.single().value)
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `placeholder backend falls back to 43499 in the native document`() = runBlocking {
        val (controller, root) = controller("backend-selection-64560", BackendKind.Cve2026_64560)
        try {
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            assertEquals("cve_2026_43499", backendToken(bytes))
        } finally {
            root.deleteRecursively()
        }
    }
}
