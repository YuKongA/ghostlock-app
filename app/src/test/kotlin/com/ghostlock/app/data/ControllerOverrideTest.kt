package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File
import java.nio.file.Files

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ControllerOverrideTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val release = "6.1.118-android14-11-ga3b9c44908dd-ab13320413"

    @Test
    fun `edits persist in preferences and leave stored documents untouched`() = runBlocking {
        val legacy = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("remote-main-6x-offsets.json"),
        ).bufferedReader().use { it.readText() }
        val root = Files.createTempDirectory("controller-override").toFile()
        try {
            val store = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            )
            store.save("remote-main-6x-offsets.json", legacy)
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = store,
                preferences = context.getSharedPreferences("controller-override", 0)
                    .also { it.edit().clear().commit() },
            )
            val pair = CpuPair(primary = 0, consumer = 1)
            assertNull("imports must not be auto-loaded", controller.activeUserProfile())
            controller.selectUserProfile("remote-main-6x-offsets.json", release, pair)
            assertEquals("remote-main-6x-offsets.json", controller.activeUserProfile())
            val baseline = controller.load(release, pair)
            assertTrue(baseline.hasProfile)

            controller.updateGeneral(release, pair, mapOf("execution.stages.w1_attempts" to 42L))
            val tuned = controller.load(release, pair)
            assertEquals(
                42L,
                tuned.general.first { it.path == "execution.stages.w1_attempts" }.value,
            )

            controller.updateRoute(release, pair, "select_stack")
            assertEquals("select_stack", controller.load(release, pair).route)

            assertEquals(legacy, store.rawText("remote-main-6x-offsets.json"))

            controller.reset(release, pair)
            val reset = controller.load(release, pair)
            assertEquals(baseline.route, reset.route)
            assertEquals(
                baseline.general.first { it.path == "execution.stages.w1_attempts" }.value,
                reset.general.first { it.path == "execution.stages.w1_attempts" }.value,
            )

            controller.onUserProfileDeleted("remote-main-6x-offsets.json")
            assertNull("deleting the loaded document unloads it", controller.activeUserProfile())
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `save modified stores the resolved profile in the user folder`() = runBlocking {
        val legacy = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("remote-main-6x-offsets.json"),
        ).bufferedReader().use { it.readText() }
        val root = Files.createTempDirectory("controller-save-modified").toFile()
        try {
            val store = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            )
            store.save("remote-main-6x-offsets.json", legacy)
            File(store.directory, "remote-main-6x-offsets.json").setLastModified(1_000L)
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = store,
                preferences = context.getSharedPreferences("controller-save-modified", 0)
                    .also { it.edit().clear().commit() },
            )
            val pair = CpuPair(primary = 0, consumer = 1)
            controller.selectUserProfile("remote-main-6x-offsets.json", release, pair)
            controller.updateGeneral(release, pair, mapOf("execution.stages.w1_attempts" to 42L))

            assertTrue(controller.saveModified(release, pair))

            val modified = store.list().first { it.name.endsWith("-modified.conf") }
            assertEquals(listOf(release), modified.releases)
            assertEquals(
                "the imported document stays untouched",
                legacy,
                store.rawText("remote-main-6x-offsets.json"),
            )

            val entry = requireNotNull(store.loadEntry(release, modified.name))
            val execution = entry["execution"].asValueMap()
            assertEquals(42, execution?.get("stages").asValueMap()?.get("w1_attempts"))
            assertEquals(null, execution?.get("selected_cpus"))
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `native document folds the selected cpu pair into recommended cpus`() = runBlocking {
        val legacy = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("remote-main-6x-offsets.json"),
        ).bufferedReader().use { it.readText() }
        val root = Files.createTempDirectory("controller-cpu-pair").toFile()
        try {
            val store = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            )
            store.save("remote-main-6x-offsets.json", legacy)
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = store,
                preferences = context.getSharedPreferences("controller-cpu-pair", 0)
                    .also { it.edit().clear().commit() },
            )
            val pair = CpuPair(primary = 2, consumer = 3)
            val config = controller.load(release, pair)
            assertTrue(config.hasProfile)
            val document = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(NativeProfileDocument.fromBinary(document))
            assertEquals(2L, decoded.execution.recommendedMainCpu)
            assertEquals(3L, decoded.execution.recommendedConsumerCpu)
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `reopening a session sees the general edits saved into the overrides`() = runBlocking {
        val legacy = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("remote-main-6x-offsets.json"),
        ).bufferedReader().use { it.readText() }
        val root = Files.createTempDirectory("controller-reopen").toFile()
        try {
            val store = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            )
            store.save("remote-main-6x-offsets.json", legacy)
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = store,
                preferences = context.getSharedPreferences("controller-reopen", 0)
                    .also { it.edit().clear().commit() },
            )
            val pair = CpuPair(primary = 0, consumer = 1)
            controller.selectUserProfile("remote-main-6x-offsets.json", release, pair)

            /* The save sequence: general first, then the advanced rebuild with
             * the general drafts merged in (they share the execution.* paths). */
            controller.updateGeneral(release, pair, mapOf("execution.stages.w1_attempts" to 42L))
            controller.updateAdvanced(
                release,
                pair,
                mapOf("execution.stages.w1_attempts" to 42L),
            )

            val saved = controller.load(release, pair)
            assertEquals(
                42L,
                saved.general.first { it.path == "execution.stages.w1_attempts" }.value,
            )

            /* A fresh session seeded from the live overrides, as if reopened. */
            val sessionPreferences = context.getSharedPreferences("controller-reopen-session", 0)
                .also { it.edit().clear().commit() }
            sessionPreferences.edit(commit = true) {
                putString(
                    AndroidProfileConfigController.PrefDebugProfileOverrides,
                    HoconSupport.render(
                        valueMapOf(release to controller.overridesSnapshot(release)),
                    ),
                )
            }
            val session = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = store,
                preferences = sessionPreferences,
                forcedUserProfile = "remote-main-6x-offsets.json",
                forcedBuiltinRelease = controller.activeBuiltinRelease(),
            )
            val reopened = session.load(release, pair)
            assertEquals(
                42L,
                reopened.general.first { it.path == "execution.stages.w1_attempts" }.value,
            )
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `general editor only offers the resolved route tuning`() = runBlocking {
        val root = Files.createTempDirectory("controller-general-route").toFile()
        try {
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = UserProfileStore(
                    directory = root.resolve("user_profiles"),
                    assetLoader = AssetConfigLoader(context),
                ),
                preferences = context.getSharedPreferences("controller-general-route", 0)
                    .also { it.edit().clear().commit() },
            )
            val pair = CpuPair(primary = 0, consumer = 1)

            /* multicast builtin: only its own tuning, never select's. */
            val multicast = controller.load(
                "5.15.189-android13-8-00016-g51bba4309aac-ab14546557", pair,
            )
            assertTrue(multicast.hasProfile)
            val multicastPaths = multicast.general.map { it.path }
            assertTrue(
                multicastPaths.any { it.startsWith("execution.routes.multicast_waiter.") },
            )
            assertTrue(multicastPaths.none { it.startsWith("execution.routes.select_stack.") })
            assertTrue(multicastPaths.none { it.startsWith("execution.routes.tcp_zerocopy.") })

            /* tcp profile with a select fallback: both groups, no multicast. */
            val tcp = controller.load("6.1.118-android14-11-ga3b9c44908dd-ab13320413", pair)
            val tcpPaths = tcp.general.map { it.path }
            assertTrue(tcpPaths.any { it.startsWith("execution.routes.tcp_zerocopy.") })
            assertTrue(tcpPaths.any { it.startsWith("execution.routes.select_stack.") })
            assertTrue(tcpPaths.none { it.startsWith("execution.routes.multicast_waiter.") })
        } finally {
            root.deleteRecursively()
        }
    }
}
