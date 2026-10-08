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
import com.ghostlock.app.data.component.BackendKind

/**
 * Explicit backend preference (parse chain step 1-prime): the declared default is the
 * first entry of the asset's available list (AvailablePriority order), which is
 * cve_2026_43284 - route-less, so the DERIVED document carries the 43284 queue and no
 * route section. Injecting backendSelection = { Cve2026_43499 } must OVERRIDE that
 * derivation and bring the measured 43499 document back (queue + route section +
 * measured values).
 *
 * This file keeps the coverage the measured 43499 document carries (abi/cred values and
 * the multicast_waiter route measurements), which Sog10ProfileRegressionTest and
 * Sog10ProfileCoreRegressionTest used to hold; those tests are now narrowed to the
 * derived-shape assertions and cross reference here. The expected values below are
 * carried over verbatim from those assertions, which were measured against this same
 * asset - they are not re-derived here.
 *
 * 沿革: 用户 ① 恢复了支持列表设备（含 SOG10）的 43499 声明 ⇒ 声明默认仍是 43284
 * （available 首项，无 route 轴）；该声明与 wire 的一致性由上述两个回归测试守卫，
 * 本测试则专门覆盖【显式选择 43499】时其文档携带其测量值。
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class Sog10ExplicitBackendOverrideTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"

    @Test
    fun `an explicit 43499 preference overrides the declared 43284 backend`() = runBlocking {
        val root = Files.createTempDirectory("sog10-explicit").toFile()
        try {
            val store = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            )
            /* The imported document declares cve_2026_43499 explicitly: its block was
             * machine-extracted from the 5.15 asset (the commented declaration) and
             * uncommented, so no value here is hand-copied. */
            /* save() sanitises the name and returns the FINAL one (uniqueFile), so the
             * preference must use that returned name - a literal name would silently
             * miss the imported layer. */
            val imported = store.save("sog10-explicit-43499.conf", java.io.File("/tmp/sog-user.conf").readText())
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = store,
                preferences = context.getSharedPreferences("sog10-explicit", 0)
                    .also { it.edit().clear().commit() },
                backendSelection = { BackendKind.Cve2026_43499 },
            )

            /* User-import selection (parse chain step 1) plus the explicit backend
             * preference (step 1-prime) are both exercised here. */
            controller.selectUserProfile(imported, release, CpuPair(primary = 0, consumer = 1))
            val config = controller.load(release, CpuPair(primary = 0, consumer = 1))
            assertTrue("explicit preference must resolve the profile", config.hasProfile)
            val bytes = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
            assertEquals(release, decoded.release)
            /* M3: the queue replaced the token - a migrated profile must carry the
             * queue and NO token (native rejects both at once, glkv3_parse.cpp:407-419). */
            assertTrue(
                "the migrated profile must not carry a token",
                runCatching { entry(decoded, "backend.cve_2026_43499", "steps") }.isFailure,
            )
            assertTrue(
                "the migrated profile must carry a queue",
            /* Batch 2: only cve_2026_43284 is declared here (43499 commented per user
             * instruction), so the queue that rides is the 43284 one. */
                /* The explicit 43499 preference means the 43499 document must ride, so the
             * queue is looked up under the 43499 owner here (the original test looks
             * under 43284 because it asserts the derived default shape). */
            runCatching { entry(decoded, "backend.cve_2026_43499", "queue") }.isSuccess,
            )
            assertNull(entryOrNull(decoded, "backend.cve_2026_43499.abi.kernel", "kernel_phys_load"))
            assertEquals(Glkv3Value.UInt(0u), entry(decoded, "backend.cve_2026_43499.abi.cred", "usage_offset"))
            assertEquals(
                Glkv3Value.UInt(35027464u),
                entry(decoded, "backend.cve_2026_43499.abi.offset", "selinux_blob_sizes"),
            )
            assertEquals(
                Glkv3Value.UInt(35018112u),
                entry(decoded, "backend.cve_2026_43499.abi.offset", "security_hook_heads"),
            )
            assertEquals(
                Glkv3Value.UInt((-274698454400L).toULong()),
                entry(decoded, "backend.cve_2026_43499.cred", "ref0_image"),
            )
            assertEquals(
                Glkv3Value.UInt((-274696707824L).toULong()),
                entry(decoded, "backend.cve_2026_43499.cred", "ref1_image"),
            )
            assertEquals(
                Glkv3Value.UInt((-274698453008L).toULong()),
                entry(decoded, "backend.cve_2026_43499.cred", "ref2_image"),
            )
            assertEquals(
                Glkv3Value.UInt((-274698454232L).toULong()),
                entry(decoded, "backend.cve_2026_43499.cred", "ref3_image"),
            )
            /* 4 条 route 测量（搬运自 Sog10ProfileCoreRegressionTest/Sog10ProfileRegressionTest，
             * 值以 43499 的 multicast_waiter 段为准；覆盖 12 -> 12 收口）。 */
            val routeSection = "backend.cve_2026_43499.route.multicast_waiter"
            assertEquals(Glkv3Value.UInt(96u), entry(decoded, routeSection, "waiter_off"))
            assertEquals(Glkv3Value.UInt(264u), entry(decoded, routeSection, "buffer_size"))
            assertEquals(Glkv3Value.UInt(48u), entry(decoded, routeSection, "task_offset"))
            assertEquals(Glkv3Value.UInt(56u), entry(decoded, routeSection, "lock_offset"))

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
