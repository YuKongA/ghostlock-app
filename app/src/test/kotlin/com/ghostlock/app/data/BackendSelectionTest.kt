package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.data.component.BackendKind
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
 * B7: the app-level backend selection reaches the native wire header. The
 * controller injects the selected token into the profile builder, and the
 * document codec fails an unavailable backend closed to the 43499 default.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BackendSelectionTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val release = "6.1.118-android14-11-ga3b9c44908dd-ab13320413"
    private val pair = CpuPair(primary = 0, consumer = 1)

    private fun controller(name: String, backend: BackendKind): Pair<AndroidProfileConfigController, File> {
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
        )
        return controller to root
    }

    private fun headerBackendId(bytes: ByteArray): Int =
        (bytes[8].toInt() and 0xff) or ((bytes[9].toInt() and 0xff) shl 8)

    @Test
    fun `selected backend is written into the native header`() = runBlocking {
        val (controller, root) = controller("backend-selection-43499", BackendKind.Cve2026_43499)
        try {
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            assertEquals(1, headerBackendId(bytes))
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `unavailable backend falls back to 43499 in the native document`() = runBlocking {
        val (controller, root) = controller("backend-selection-43284", BackendKind.Cve2026_43284)
        try {
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            assertEquals(1, headerBackendId(bytes))
        } finally {
            root.deleteRecursively()
        }
    }
}
