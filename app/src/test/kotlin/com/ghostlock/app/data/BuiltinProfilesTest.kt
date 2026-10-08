package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.nio.file.Files

/**
 * Black-box end-to-end test over every bundled profile: resolve it through the
 * controller, require a clean document, and verify the route-scoped UI
 * projection and the v2 round trip. This is the test that catches "a builtin
 * profile no longer loads after a schema/format change".
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BuiltinProfilesTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val pair = CpuPair(primary = 0, consumer = 1)

    private val routeKindsSeen = mutableSetOf<String>()

    private data class Entry(val release: String, val file: String)

    private fun builtinEntries(): List<Entry> {
        val index = HoconSupport.parseValue(
            AssetConfigLoader(context).load("profile/index.conf"),
        ).asValueMap() ?: error("index.conf is not an object")
        return index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap() }
            .mapNotNull { entry ->
                val release = entry["release"] as? String ?: return@mapNotNull null
                val file = entry["file"] as? String ?: return@mapNotNull null
                Entry(release, file)
            }
            .filterNot { it.release.endsWith("-template") }
    }

    @Test
    fun `cpu pair suggestions follow the profiles that carry them`() {
        val catalog = BuiltinProfileCatalog(context)
        /* The iQOO 12 entry recommends the pair the reference kit measured. */
        assertEquals(
            4 to 5,
            catalog.recommendedCpus["6.1.145-android14-11-maybe-dirty"],
        )
        /* No other profile silently defaults to that pair. */
        val others = catalog.recommendedCpus.filterKeys { it != "6.1.145-android14-11-maybe-dirty" }
        assertTrue(others.none { it.value == (4 to 5) })
    }

    @Test
    fun `every builtin profile resolves cleanly and round trips`() = runBlocking {
        val root = Files.createTempDirectory("builtin-profiles").toFile()
        try {
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = UserProfileStore(
                    directory = root.resolve("user_profiles"),
                    assetLoader = AssetConfigLoader(context),
                ),
                preferences = context.getSharedPreferences("builtin-profiles", 0)
                    .also { it.edit().clear().commit() },
            )

            val entries = builtinEntries()
            assertTrue("index.conf lists no builtin profiles", entries.isNotEmpty())
            for (entry in entries) {
                val config = controller.load(entry.release, pair)
                assertTrue("${entry.release}: profile did not resolve", config.hasProfile)
                assertEquals(
                    "${entry.release}: invalid fields ${config.invalidPaths}",
                    emptySet<String>(),
                    config.invalidPaths,
                )
                /* 沿革: 排序权威 = AvailablePriority（priority，其次 token）；枚举序曾把 43499
                 * 排前 ⇒ 声明被绕过（A301SO）。config.route 是【运行期 route 轴】，(C) 之后它
                 * 跟随声明的默认 backend：默认项有 route 轴 ⇒ 等于它；无 route 轴（8 份 general
                 * + 6.1.145-maybe-dirty）⇒ 必须为 null。派生，零硬编码，两类都测。
                 * config.roots 是编辑器树不是 map：从该 entry 指向的同一份资产文本读声明。 */
                val declaredMap = HoconSupport
                    .parseValue(AssetConfigLoader(context).load("profile/" + entry.file))
                    .asValueMap()
                /* 资产根是 ghostlock { ... }：声明在下一层（与 Sog10 同款形态）。 */
                val declaredRoot = declaredMap?.get("ghostlock").asValueMap() ?: declaredMap
                val declaredAvailable = requireNotNull(declaredRoot?.get("available").asValueMap()) {
                    "${entry.release}: no available declaration; root keys=" + declaredRoot?.keys?.sorted()
                }
                val defaultBackend = AvailablePriority.orderedBackends(declaredAvailable).first()
                val defaultRoutes = CombinationCatalog.specs
                    .filter { spec -> spec.backend.token == defaultBackend }
                    .mapNotNull { spec -> spec.route?.token }
                    .distinct()
                require(defaultRoutes.size <= 1) {
                    "${entry.release}: ambiguous default routes $defaultRoutes"
                }
                /* route 段实测在 ghostlock.backend.<id>.route（三层深）：递归收集，不写死层数。 */
                fun routeSectionNames(node: Any?): List<String> {
                    val map = node as? Map<*, *> ?: return emptyList()
                    val here = (map["route"] as? Map<*, *>)?.keys?.map { it.toString() }.orEmpty()
                    return here + map.values.flatMap { routeSectionNames(it) }
                }
                val declaredRouteNames = routeSectionNames(declaredRoot).distinct()
                val routeKind = when {
                    declaredRouteNames.isEmpty() -> "none"
                    declaredRouteNames.size == 1 -> "single"
                    else -> "multi"
                }
                routeKindsSeen += routeKind
                /* 三类各有其【最强可判】的合同（都不放宽）：
                 *   single = 唯一 route 段 ⇒ config.route 必须 == 该段名
                 *   multi  = 多条调参段（general）⇒ 活动 route 由 resolver 选 ⇒ 只保证 ∈ 段名集合
                 *   none   = 无 route 段 ⇒ config.route 必须 == null
                 * decoded.route（wire 根 route）是【另一个量】，跟随默认 backend。 */
                val route = config.route
                when (routeKind) {
                    "single" -> assertEquals(
                        "${entry.release}: config.route must equal the declared route section",
                        declaredRouteNames.single(),
                        route,
                    )
                    "multi" -> {
                        require(declaredRouteNames.isNotEmpty()) {
                            "${entry.release}: multi implies non-empty declared route sections"
                        }
                        assertTrue(
                            "${entry.release}: active config.route must be one of the declared " +
                                "route sections " + declaredRouteNames,
                            route in declaredRouteNames,
                        )
                    }
                    else -> assertNull("${entry.release}: no route section declared", route)
                }

                /* The general list offers the active route's tuning and
                 * nothing from another route (R6a removed the fallback). */
                val allowed = setOfNotNull(route)
                val routePaths = config.general.map { it.path }
                    .filter { it.startsWith("execution.routes.") }
                for (path in routePaths) {
                    val owner = path.removePrefix("execution.routes.").substringBefore('.')
                    assertTrue(
                        "${entry.release}: $path belongs to $owner, expected $allowed",
                        owner in allowed,
                    )
                }

                /* The resolved v3 document decodes to the expected selection. */
                val bytes = controller.nativeDocument(config)
                assertNotNull("${entry.release}: no native document", bytes)
                val decoded = Glkv3Decoder.decode(bytes!!)
                assertNotNull("${entry.release}: native document failed to decode", decoded)
                assertEquals(entry.release, decoded!!.release)
                /* 沿革: while 43499 declarations were disabled both readings agreed. The user
                 * restored them (SUPPORTED_DEVICES.md) while the DEFAULT entry stays the
                 * first available one (43284), which has no route axis - so config.route
                 * (the route the profile declares) and the wire root route diverge BY
                 * DESIGN (see DeclarationDrivesDocumentShapeTest). Assert the relation,
                 * derived from the default backend via the catalogue, never hard-coded. */
                if (defaultRoutes.isEmpty()) {
                    /* decoded.route = wire 根 route ⇒ 跟随默认 backend（无 route 轴 ⇒ null）。 */
                    assertNull("${entry.release}: default backend has no route axis", decoded.route)
                    /* config.route 已由上面的 when(single/multi/none) 断言 —— 那里覆盖更强
                     * （single 相等 / multi 成员资格 / none 为 null），此处不重复。 */
                } else {
                    assertEquals(
                        "${entry.release}: wire route follows the default backend",
                        defaultRoutes.single(),
                        decoded.route,
                    )
                }
            }
            /* 覆盖守卫（三类形态；钉住【实测】集合）：本轮定向实测 = single + none + multi，
             * 三类全部出现（none = 无 route 段的 profile 真实存在）。任一类增减 ⇒ 该守卫会红
             * ⇒ 应显式更新（守卫要能失败）。消息带 routeKindsSeen：红时即可读出实测集合
             * （取实测值只能靠失败消息，Gradle 吞 stdout）。 */
            assertEquals(
                "route shape coverage; seen=" + routeKindsSeen,
                setOf("single", "none", "multi"),
                routeKindsSeen,
            )
        } finally {
            root.deleteRecursively()
        }
    }

    private fun leafPaths(nodes: List<com.ghostlock.app.domain.model.ProfileFieldNode>): List<String> =
        nodes.flatMap { node ->
            if (node.isGroup) leafPaths(node.children) else listOf(node.path)
        }

    @Test
    fun `advanced editor exposes the complete route and shared geometry`() = runBlocking {
        val root = Files.createTempDirectory("editor-fields").toFile()
        try {
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = UserProfileStore(
                    directory = root.resolve("user_profiles"),
                    assetLoader = AssetConfigLoader(context),
                ),
                preferences = context.getSharedPreferences("editor-fields", 0)
                    .also { it.edit().clear().commit() },
            )
            for (entry in builtinEntries()) {
                val config = controller.load(entry.release, pair)
                val paths = leafPaths(config.roots)
                /* 3 份非四族 general（用户 2026-10-06 ②：旧 template 已删，general 兜底）是
                 * 空骨架：不声明 route/几何 ⇒ 跳过这些断言，但必须无 invalidPaths —— 覆盖不降级。 */
                if (paths.none { it.startsWith("route.") }) {
                    assertTrue("${entry.release}: unexpected invalid paths ${config.invalidPaths}",
                        config.invalidPaths.isEmpty())
                } else {
                    assertTrue("${entry.release}: kernel_phys_load missing", "kernel_phys_load" in paths)
                    for (field in listOf(
                        "prio", "normal_prio", "sched_task_group", "pi_lock", "pi_waiters",
                        "pi_top_task", "pi_blocked_on", "pid", "tgid", "atomic_flags", "real_cred",
                        "cred", "comm", "tasks", "seccomp",
                    )) {
                        assertTrue("${entry.release}: task_struct.$field missing", "task_struct.$field" in paths)
                    }
                    for (field in listOf("copy_size", "caps_count", "ref0_offset", "ref3_image")) {
                        assertTrue("${entry.release}: cred.$field missing", "cred.$field" in paths)
                    }
                    for (field in listOf(
                        "init_task", "init_cred", "empty_zero_page", "root_task_group",
                        "slide_nfulnl_logger", "slide_boot_id",
                    )) {
                        assertTrue("${entry.release}: offset.$field missing", "offset.$field" in paths)
                    }
                    assertTrue("${entry.release}: kernelsnitch.collisions missing",
                        "kernelsnitch.collisions" in paths)
                    assertTrue("${entry.release}: route branch missing",
                        paths.any { it.startsWith("route.") })
                }
                /* Execution tuning belongs to the general page, never here. */
                assertTrue("${entry.release}: execution leaked into advanced tree",
                    paths.none { it.startsWith("execution") })
            }
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `legacy shared defaults stay in sync with the bundled 6x templates`() {
        val loader = AssetConfigLoader(context)
        val credProfile = HoconSupport.parseValue(loader.load("profile/credential-6x.conf"))
            .asValueMap()!!
        ProfileLayout.applyNormalize(credProfile)
        val cred = credProfile["cred"].asValueMap()!!
        val snitchProfile = HoconSupport.parseValue(loader.load("profile/kernelsnitch-6x.conf"))
            .asValueMap()!!
        ProfileLayout.applyNormalize(snitchProfile)
        val snitch = snitchProfile["kernelsnitch"].asValueMap()!!

        /* LegacyProfileConverter seeds these values into imported reports and
         * carries its own copies; changing the assets requires updating it. */
        assertEquals(136L, (cred["copy_size"] as Number).toLong())
        assertEquals(48L, (cred["caps_offset"] as Number).toLong())
        assertEquals(5L, (cred["caps_count"] as Number).toLong())
        assertEquals(1L, (cred["usage_value"] as Number).toLong())
        assertEquals(-1L, (cred["caps_value"] as Number).toLong())
        assertEquals(4L, (snitch["collisions"] as Number).toLong())
    }
}
