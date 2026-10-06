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
            for (path in listOf(
                "backend.cve_2026_43284.execution.wait_timeout_ms",
                "backend.cve_2026_43284.execution.module_poll_attempts",
                "backend.cve_2026_43284.execution.module_poll_interval_ms",
            )) {
                assertTrue(path + " is missing from the advanced tree", path in paths)
            }
            /* The three convention keys are computed natively now, so they are
             * not editable anywhere in the tree. */
            for (key in listOf("kmi", "lkm_path", "carrier_path")) {
                assertFalse(
                    "backend.cve_2026_43284." + key + " must not be editable",
                    "backend.cve_2026_43284." + key in paths,
                )
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
    fun `native-computed convention keys are neither editable nor on the wire`() =
        withController("43284-conventions") { controller ->
            val section = "backend.cve_2026_43284"
            val convention = listOf("kmi", "lkm_path", "carrier_path")
            controller.updateAdvanced(
                release,
                pair,
                convention.associate { section + "." + it to "/data/local/tmp/x" },
            )
            val config = controller.load(release, pair)
            val paths = flatten(config.roots).map { it.path }.toSet()
            for (key in convention) {
                assertFalse(
                    section + "." + key + " must not be editable",
                    section + "." + key in paths,
                )
            }
            val decoded = Glkv3Decoder.decode(requireNotNull(controller.nativeDocument(config)))
            assertNotNull(decoded)
            val keys = decoded!!.sections.firstOrNull { it.name == section }?.entries
                .orEmpty().map { it.key }
            for (key in convention) {
                assertFalse(section + "." + key + " must not reach the wire", key in keys)
            }
        }

    private fun entry(document: Glkv3Document, section: String, key: String): Glkv3Value =
        document.sections
            .first { it.name == section }
            .entries
            .first { it.key == key }
            .value
}
