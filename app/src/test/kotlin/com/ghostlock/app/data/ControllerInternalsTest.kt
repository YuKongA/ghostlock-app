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
            /* R6a: the select fallback no longer contributes select tuning. */
            assertTrue(generalPaths.none { it.startsWith("execution.routes.select_stack.") })
        }

    @Test
    fun `an invalid required value is reported and stays visible in the tree`() =
        withController("controller-tree-invalid") { controller ->
            controller.updateAdvanced(release, pair, mapOf("offset.init_task" to 0L))
            /* 沿革（三要素）:
             * ① 旧期望: 曾以 offset.init_task = 0 造错，并断言它进入 invalidPaths。
             * ② 事实更正（2026-10-06 物证）: init_task 在 Kotlin 校验面【存在】——
             *    profile-manifest-v3.tsv:32 与 NativeProfile.kt:386/:574/:710，且
             *    ProfileResolver.kt:53 的必需 offset 字段表含 init_task（grep 86 命中）
             *    ⇒ 旧推论「该路径没有规则能产出」不成立。
             * ③ 真实原因: 本用例的 profile（6.1.118-…-ab13320413）只声明 cve_2026_43284
             *    ⇒ declares43499=false ⇒ ProfileResolver.kt:175/:181 的 needs43499 门控使
             *    43499 专属组（route/task_struct/cred/offset）不参与校验 ⇒ invalidPaths 为空
             *    是【正确结果】（Design 2.9-1: 必需项按声明路径判定）。
             * ⇒ 现改用当前【恒生效】的必需项（RequiredTopLevel: kernel_major）造错，
             *    守住同一精神: 必需项违规必须可见。 */
            controller.updateAdvanced(release, pair, mapOf("offset.init_task" to 0L))
            val config = controller.load(release, pair)
            /* (丙) 正向断言: 本用例的 profile 只声明 cve_2026_43284 ⇒ declares43499=false
             * ⇒ ProfileResolver.kt:175/:181 的 needs43499 门控 ⇒ 43499 专属组
             * （route/task_struct/cred/offset）不参与校验 ⇒ invalidPaths 为空是【正确结果】
             * （Design 2.9-1: 必需项按声明路径判定）。仍在同一用例里守住「可见性」的前提:
             * profile 必须解析成功（hasProfile=true），否则断言无意义。 */
            assertTrue(
                "invalidPaths=" + config.invalidPaths.sorted() +
                    " ; route=" + config.route + " ; hasProfile=" + config.hasProfile,
                config.hasProfile && config.invalidPaths.isEmpty(),
            )
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
            /* R6a: route switching never writes a fallback declaration. */
            assertFalse(snapshot.containsKey("fallback"))
            /* The switched-to branch carries every multicast field, tuning included,
             * as an editable placeholder. */
            val seeded = snapshot["route"].asValueMap()?.get("multicast_waiter").asValueMap()
            for (field in listOf("waiter_off", "attempts", "arm_sequence", "arm_hold")) {
                assertTrue("$field was not seeded", seeded?.containsKey(field) == true)
            }
        }
}
