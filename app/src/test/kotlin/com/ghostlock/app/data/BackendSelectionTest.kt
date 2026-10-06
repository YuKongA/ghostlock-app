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
    fun `available 43284 backend reaches the wire with its queue and no token`() = runBlocking {
        val (controller, root) = controller("backend-selection-43284", BackendKind.Cve2026_43284)
        try {
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
            assertEquals("cve_2026_43284", decoded.backend)
            assertEquals("umh_forward", decoded.terminal)
            /* M5: the combination token no longer rides the wire, and 43284 carries no
             * route (design D2'). This profile declares no queue either, so the owner
             * may be absent entirely - the meaningful coverage here is the backend and
             * terminal identity asserted above plus the absence of a token. */
            assertTrue(
                "no combination token may ride the wire",
                decoded.sections.flatMap { it.entries }.none { it.key == "steps" },
            )
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `combination selection derives route and terminal and carries no token`() = runBlocking {
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
            /* M5: the token no longer rides the wire - the queue plus the queue-level
             * route carry the selection. The token-derived terminal/route assertions
             * above keep the coverage that still means something. */
            assertTrue(
                "no combination token may ride the wire",
                decoded.sections.flatMap { it.entries }.none { it.key == "steps" },
            )
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
