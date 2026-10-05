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
            assertEquals(Glkv3Value.Str("mcast_rootchild"), entry(decoded, "backend.cve_2026_43499", "steps"))
            assertNull(entryOrNull(decoded, "platform.abi.kernel", "kernel_phys_load"))
            assertEquals(Glkv3Value.UInt(0u), entry(decoded, "platform.abi.cred", "usage_offset"))
            assertEquals(
                Glkv3Value.UInt(35027464u),
                entry(decoded, "platform.abi.offset", "selinux_blob_sizes"),
            )
            assertEquals(
                Glkv3Value.UInt(35018112u),
                entry(decoded, "platform.abi.offset", "security_hook_heads"),
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
