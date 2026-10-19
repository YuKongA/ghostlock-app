package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ProfileFieldNode
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
 * White-box tests for the controller's UI projections: the advanced tree is
 * derived from the resolved HOCON, while route tuning lives only in the
 * general list, and validation/route switching prune as documented.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ControllerInternalsTest {
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
                )
                block(controller)
            } finally {
                root.deleteRecursively()
            }
        }

    private fun flatten(nodes: List<ProfileFieldNode>): List<ProfileFieldNode> =
        nodes.flatMap { if (it.isGroup) flatten(it.children) else listOf(it) }

    @Test
    fun `advanced tree picks up numeric leaves from the resolved document`() =
        withController("controller-tree-dynamic") { controller ->
            controller.updateAdvanced(release, pair, mapOf("offset.host_test_marker" to 7L))
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val leaf = flatten(config.roots).firstOrNull { it.path == "offset.host_test_marker" }
            assertEquals(7L, leaf?.value)
            assertTrue("marker should be flagged as overridden", leaf?.overridden == true)
        }

    @Test
    fun `route tuning is excluded from the tree but offered by the general list`() =
        withController("controller-tree-routes") { controller ->
            val config = controller.load(release, pair)
            val treePaths = flatten(config.roots).map { it.path }
            assertTrue(treePaths.none { it == "execution.routes" })
            assertTrue(treePaths.none { it.startsWith("execution.routes.") })
            assertTrue(treePaths.none { it.startsWith("execution.selected_cpus.") })

            val generalPaths = config.general.map { it.path }
            assertTrue(generalPaths.any { it.startsWith("execution.routes.tcp_zerocopy.") })
            assertTrue(generalPaths.any { it.startsWith("execution.routes.select_stack.") })
        }

    @Test
    fun `an invalid required value is reported and stays visible in the tree`() =
        withController("controller-tree-invalid") { controller ->
            controller.updateAdvanced(release, pair, mapOf("offset.init_task" to 0L))
            val config = controller.load(release, pair)
            assertTrue(config.invalidPaths.contains("offset.init_task"))
            val leaf = flatten(config.roots).firstOrNull { it.path == "offset.init_task" }
            assertEquals(0L, leaf?.value)
            assertTrue(leaf?.overridden == true)
        }

    @Test
    fun `switching routes prunes the previous branch from the override`() =
        withController("controller-tree-prune") { controller ->
            controller.updateRoute(release, pair, "multicast_waiter")
            val snapshot = controller.overridesSnapshot(release)
            val branches = snapshot["route"].asValueMap()?.keys.orEmpty()
            assertEquals(listOf("multicast_waiter"), branches.toList())
            assertFalse(snapshot["fallback"].asValueMap()?.containsKey("route") == true)
        }
}
