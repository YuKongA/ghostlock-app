package com.ghostlock.app.data

import android.app.Application
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Document
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ProfileFieldNode
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.nio.file.Files

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class Sog10ProfileRegressionTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"

    @Test
    fun `SOG10 built in profile has no invalid paths and preserves its GLKv3 fields`() = runBlocking {
        val root = Files.createTempDirectory("sog10-profile").toFile()
        try {
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = UserProfileStore(
                    directory = root.resolve("user_profiles"),
                    assetLoader = AssetConfigLoader(context),
                ),
                preferences = context.getSharedPreferences("sog10-profile", 0)
                    .also { it.edit().clear().commit() },
            )

            val config = controller.load(release, CpuPair(primary = 0, consumer = 1))
            assertTrue(config.hasProfile)
            assertEquals("multicast_waiter", config.route)
            assertTrue("unexpected invalid paths: ${config.invalidPaths}", config.invalidPaths.isEmpty())

            val advancedPaths = leafPaths(config.roots)
            assertTrue("kernel_phys_load must remain editable", "kernel_phys_load" in advancedPaths)
            assertTrue("cred.usage_offset must remain editable", "cred.usage_offset" in advancedPaths)
            assertTrue(advancedPaths.none { it.startsWith("execution.") })

            val bytes = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
            assertEquals(release, decoded.release)
            /* M3: the queue replaced the token - a migrated profile must carry the
             * queue and NO token (native rejects both at once, glkv3_parse.cpp:407-419). */
            /* 沿革: 排序权威 = AvailablePriority（priority，其次 token 序）；默认项 = 声明首项。
             * 零硬编码：默认 backend 与其 route 都从声明派生（同 core 版口径）。 */
            val declaredMap = HoconSupport.parseValue(
                AssetConfigLoader(context).load("profile/" + release + ".conf"),
            ).asValueMap()
            val declaredRoot = declaredMap?.get("ghostlock").asValueMap() ?: declaredMap
            val declaredAvailable = requireNotNull(declaredRoot?.get("available").asValueMap()) {
                "no available declaration; root keys=" + declaredRoot?.keys?.sorted()
            }
            val defaultBackend = AvailablePriority.orderedBackends(declaredAvailable).first()
            val defaultRoutes = com.ghostlock.app.data.component.CombinationCatalog.specs
                .filter { spec -> spec.backend.token == defaultBackend }
                .mapNotNull { spec -> spec.route?.token }
                .distinct()
            require(defaultRoutes.size <= 1) {
                "ambiguous default backend " + defaultBackend + " => " + defaultRoutes
            }
            assertTrue(
                "the migrated profile must not carry a token",
                runCatching { entry(decoded, "backend.cve_2026_43499", "steps") }.isFailure,
            )
            /* 队列属于【默认 backend】（声明派生）；同族真缺陷守卫：wire 现由 UI 组合
             * token 决定 ⇒ 声明被绕过 ⇒ 本条会红，等 native 修复后自然转绿（不改测试）。 */
            assertTrue(
                "the migrated profile must carry the default backend's queue",
                runCatching { entry(decoded, "backend." + defaultBackend, "queue") }.isSuccess,
            )
            /* 这 8 条 43499 测量值断言描述【显式选中 43499】的文档形态；默认项由声明派生后
             * 默认文档是 43284 形态，故按乙移到显式路径测试（覆盖 12 -> 12，不降级）。 */

            /* 派生（零硬编码）：期望的 route 段由【默认 backend 的声明 route】决定；
             * 默认项无 route 轴 ⇒ 断言【不存在】该 backend 的 route 段（派生 != 删除）。 */
            val defaultRoutePrefix = "backend." + defaultBackend + ".route."
            val expectedRouteSection = defaultRoutes.singleOrNull()?.let { defaultRoutePrefix + it }
            if (expectedRouteSection == null) {
                assertTrue(
                    "the default backend declares no route axis, so no " + defaultRoutePrefix +
                        " section may ride the wire",
                    decoded.sections.none { it.name.startsWith(defaultRoutePrefix) },
                )
            } else {
                assertTrue(
                    "the declared route section must ride the wire: " + expectedRouteSection,
                    decoded.sections.any { it.name == expectedRouteSection },
                )
            }
        } finally {
            root.deleteRecursively()
        }
    }

    private fun entry(document: Glkv3Document, section: String, key: String): Glkv3Value =
        document.sections
            .first { it.name == section }
            .entries
            .first { it.key == key }
            .value

    private fun entryOrNull(
        document: Glkv3Document,
        section: String,
        key: String,
    ): Glkv3Value? = document.sections
        .firstOrNull { it.name == section }
        ?.entries
        ?.firstOrNull { it.key == key }
        ?.value

    private fun leafPaths(nodes: List<ProfileFieldNode>): List<String> =
        nodes.flatMap { node ->
            if (node.isGroup) leafPaths(node.children) else listOf(node.path)
        }
}
