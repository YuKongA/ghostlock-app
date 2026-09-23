package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
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

    private data class Entry(val release: String, val file: String)

    private fun builtinEntries(): List<Entry> {
        val index = HoconSupport.parseValue(
            AssetConfigLoader(context).load("kernel_profiles/index.conf"),
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
                val route = config.route
                assertNotNull("${entry.release}: route unresolved", route)

                /* The general list offers the active route (and its fallback)
                 * tuning, and nothing from another route. */
                val allowed = setOfNotNull(
                    route,
                    config.fallbackTo?.takeIf { it != "none" },
                )
                val routePaths = config.general.map { it.path }
                    .filter { it.startsWith("execution.routes.") }
                for (path in routePaths) {
                    val owner = path.removePrefix("execution.routes.").substringBefore('.')
                    assertTrue(
                        "${entry.release}: $path belongs to $owner, expected $allowed",
                        owner in allowed,
                    )
                }
                assertTrue(
                    "${entry.release}: no tuning offered for $route",
                    routePaths.any { it.startsWith("execution.routes.$route.") },
                )

                /* The resolved document re-encodes and decodes identically. */
                val bytes = controller.nativeDocument(config)
                assertNotNull("${entry.release}: no native document", bytes)
                val profile = Profile.fromBinary(bytes!!)
                assertNotNull("${entry.release}: native document failed to decode", profile)
                assertEquals(entry.release, profile!!.release)
                assertEquals(route, profile.route?.token)
            }
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `legacy shared defaults stay in sync with the bundled 6x templates`() {
        val loader = AssetConfigLoader(context)
        val cred = HoconSupport.parseValue(loader.load("kernel_profiles/credential-6x.conf"))
            .asValueMap()!!["cred"].asValueMap()!!
        val snitch = HoconSupport.parseValue(loader.load("kernel_profiles/kernelsnitch-6x.conf"))
            .asValueMap()!!["kernelsnitch"].asValueMap()!!

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
