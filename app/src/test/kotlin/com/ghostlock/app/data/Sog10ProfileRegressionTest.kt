package com.ghostlock.app.data

import android.app.Application
import com.ghostlock.app.data.route.MulticastConfig
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
    fun `SOG10 built in profile has no invalid paths and preserves its GLK1 fields`() = runBlocking {
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
            assertEquals("none", config.fallbackTo)
            assertTrue("unexpected invalid paths: ${config.invalidPaths}", config.invalidPaths.isEmpty())

            val advancedPaths = leafPaths(config.roots)
            assertTrue("kernel_phys_load must remain editable", "kernel_phys_load" in advancedPaths)
            assertTrue("cred.usage_offset must remain editable", "cred.usage_offset" in advancedPaths)
            assertTrue(advancedPaths.none { it.startsWith("execution.") })

            val binary = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(NativeProfileDocument.fromBinary(binary))
            assertEquals(release, decoded.release)
            assertEquals(1u, decoded.recommendShizuku)
            assertNull(decoded.kernelPhysLoad)
            assertEquals(0u, decoded.cred.usageOffset)
            assertEquals(35027464uL, decoded.kernelOffset.selinuxBlobSizes)
            assertEquals(35018112uL, decoded.kernelOffset.securityHookHeads)
            assertEquals(-274698454400L, decoded.cred.ref0Image.toLong())
            assertEquals(-274696707824L, decoded.cred.ref1Image.toLong())
            assertEquals(-274698453008L, decoded.cred.ref2Image.toLong())
            assertEquals(-274698454232L, decoded.cred.ref3Image.toLong())

            val geometry = (decoded.routeConfig as MulticastConfig).geometry
            assertEquals(96, geometry.waiterOff)
            assertEquals(264u, geometry.bufferSize)
            assertEquals(48u, geometry.taskOffset)
            assertEquals(56u, geometry.lockOffset)
        } finally {
            root.deleteRecursively()
        }
    }

    private fun leafPaths(nodes: List<ProfileFieldNode>): List<String> =
        nodes.flatMap { node ->
            if (node.isGroup) leafPaths(node.children) else listOf(node.path)
        }
}
