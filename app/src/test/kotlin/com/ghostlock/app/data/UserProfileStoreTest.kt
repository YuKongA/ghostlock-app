package com.ghostlock.app.data

import android.app.Application
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
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
class UserProfileStoreTest {
    private val context: Application = RuntimeEnvironment.getApplication()

    private val legacyDocument = """
        [
          {
            "release": "6.1.118-test",
            "pselect_waiter_shift": 1,
            "compact_waiter": 1,
            "kernel_phys_load": 2818572288,
            "symbols": { "off_init_task": 33420800 },
            "struct_fields": { "task_prio": 132 }
          }
        ]
    """.trimIndent()

    @Test
    fun `documents are stored byte for byte and converted on read`() {
        withStore { store ->
            store.save("offsets.json", legacyDocument)

            assertEquals(legacyDocument, store.rawText("offsets.json"))

            val entry = requireNotNull(store.loadEntry("6.1.118-test", "offsets.json"))
            assertEquals(33420800, entry["offset"].asValueMap()?.get("init_task"))
            assertEquals(132, entry["task_struct"].asValueMap()?.get("prio"))
            assertEquals(
                1,
                entry["route"].asValueMap()?.get("tcp_zerocopy").asValueMap()
                    ?.get("compact_waiter"),
            )
        }
    }

    @Test
    fun `export renders the converted document as hocon`() {
        withStore { store ->
            store.save("offsets.json", legacyDocument)

            val hocon = requireNotNull(store.exportHocon("offsets.json"))
            assertTrue(hocon.contains("release = \"6.1.118-test\""))
            assertTrue(hocon.contains("offset {"))
            assertTrue(hocon.contains("init_task = 33420800"))
            assertFalse(hocon.contains("\"symbols\""))
        }
    }

    @Test
    fun `rename keeps the extension and refuses existing names`() {
        withStore { store ->
            store.save("offsets.json", legacyDocument)
            store.save("other.json", legacyDocument)

            assertEquals("renamed.json", store.rename("offsets.json", "renamed"))
            assertNotNull(store.rawText("renamed.json"))
            assertNull(store.rename("renamed.json", "other.json"))
        }
    }

    @Test
    fun `delete removes the stored document`() {
        withStore { store ->
            store.save("offsets.json", legacyDocument)
            assertTrue(store.delete("offsets.json"))
            assertFalse(store.delete("offsets.json"))
            assertFalse(store.containsRelease("6.1.118-test"))
        }
    }

    @Test
    fun `the loaded document decides which entry is used`() {
        withStore { store ->
            store.save("old.json", legacyDocument)
            store.save("new.json", legacyDocument.replace("132", "140"))

            val old = requireNotNull(store.loadEntry("6.1.118-test", "old.json"))
            assertEquals(132, old["task_struct"].asValueMap()?.get("prio"))
            val new = requireNotNull(store.loadEntry("6.1.118-test", "new.json"))
            assertEquals(140, new["task_struct"].asValueMap()?.get("prio"))
        }
    }

    @Test
    fun `documents carry their layout version`() {
        withStore { store ->
            store.save("offsets.json", legacyDocument)
            val converted = requireNotNull(store.exportHocon("offsets.json"))
            store.save("converted.conf", converted)

            val profiles = store.list().associateBy { it.name }
            assertEquals(1, profiles.getValue("offsets.json").version)
            assertEquals(2, profiles.getValue("converted.conf").version)
        }
    }

    @Test
    fun `shizuku recommendation comes from any stored document`() {
        withStore { store ->
            store.save("rec.json", """[{"release": "r", "recommend_shizuku": 1}]""")
            assertTrue(store.recommendsShizuku("r"))
            assertFalse(store.recommendsShizuku("other"))
        }
    }

    @Test
    fun `list reports releases and unreadable documents`() {
        withStore { store ->
            store.save("offsets.json", legacyDocument)
            store.save("broken.conf", "this is not a config")

            val profiles = store.list().associateBy { it.name }
            assertEquals(listOf("6.1.118-test"), profiles.getValue("offsets.json").releases)
            assertTrue(profiles.getValue("broken.conf").parseError)
            assertTrue(profiles.getValue("offsets.json").sizeBytes > 0L)
            assertNull(store.loadEntry("unknown-release", "offsets.json"))
        }
    }

    private fun withStore(block: (UserProfileStore) -> Unit) {
        val root = Files.createTempDirectory("user-profile-store").toFile()
        try {
            val store = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            )
            block(store)
        } finally {
            root.deleteRecursively()
        }
    }
}
