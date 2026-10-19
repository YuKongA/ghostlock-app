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

    /** A flattened `--format conf` extractor output: no includes, cred inlined. */
    private val extractorConf = """
        # GhostLock kernel profile: 6.6.89-test (HOCON, self-contained).
        release = "6.6.89-test"
        schema_version = 1
        kernel_major = 6
        recommend_shizuku = 0
        kernel_phys_load = 0xA8000000
        route {
          select_stack {
            waiter_shift = -2
          }
        }
        fallback {
          to = "none"
        }
        kernelsnitch {
          collisions = 4
          mm_struct_sz = 1024
        }
        task_struct {
          prio = 132
        }
        cred {
          caps_offset = 48
          copy_size = 136
          usage_value = 1
          caps_count = 5
          caps_value = -1
        }
        offset {
          init_task = 34595456
          security_hook_heads = 0
        }
    """.trimIndent()

    /**
     * An unverified candidate `--format conf` output: only the fields the image
     * yielded. For an unverified 5.x multicast route that means the BTF-derived
     * waiter offsets, with the proven Xperia constants left out.
     */
    private val candidateConf = """
        # GhostLock kernel profile: 5.15.178-g3575c47dc7ce-dirty (HOCON, self-contained).
        release = "5.15.178-g3575c47dc7ce-dirty"
        schema_version = 1
        kernel_major = 5
        recommend_shizuku = 0
        route {
          multicast_waiter {
            task_offset = 48
            lock_offset = 56
          }
        }
        fallback {
          to = "none"
        }
        cred {
          caps_offset = 40
          copy_size = 176
        }
        offset {
          init_task = 34595456
        }
    """.trimIndent()

    @Test
    fun `unverified candidate keeps the BTF-derived route geometry on import`() {
        withStore { store ->
            store.save("candidate.conf", candidateConf)

            val entry = requireNotNull(
                store.loadEntry("5.15.178-g3575c47dc7ce-dirty", "candidate.conf"),
            )
            val geometry = entry["route"].asValueMap()!!["multicast_waiter"].asValueMap()!!
            assertEquals(48L, (geometry["task_offset"] as Number).toLong())
            assertEquals(56L, (geometry["lock_offset"] as Number).toLong())
            assertFalse(
                "the extractor must not seed the proven Xperia constants",
                geometry.containsKey("waiter_off"),
            )

            val exported = requireNotNull(store.exportHocon("candidate.conf"))
            assertFalse("export must not re-introduce includes", exported.contains("include"))
            assertTrue(exported.contains("release = \"5.15.178-g3575c47dc7ce-dirty\""))
        }
    }

    @Test
    fun `flattened extractor conf round trips without conversion`() {
        withStore { store ->
            store.save("6.6.89-test.conf", extractorConf)

            val entry = requireNotNull(store.loadEntry("6.6.89-test", "6.6.89-test.conf"))
            val exported = requireNotNull(store.exportHocon("6.6.89-test.conf"))
            val exportedEntry = HoconSupport.parseValue(exported).asValueMap()!!

            assertFalse("export must not re-introduce includes", exported.contains("include"))
            assertEquals(entry, exportedEntry)
            val cred = exportedEntry["cred"].asValueMap()!!
            assertEquals(48L, (cred["caps_offset"] as Number).toLong())
            assertEquals(-1L, (cred["caps_value"] as Number).toLong())
            assertEquals(
                4L,
                (exportedEntry["kernelsnitch"].asValueMap()!!["collisions"] as Number).toLong(),
            )
        }
    }

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
            assertTrue(hocon.contains("schema_version = 1"))
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

    @Test
    fun `releasesOf reads the stored document releases`() {
        withStore { store ->
            store.save("offsets.json", legacyDocument)
            store.save("multi.json", """[{"release": "a"}, {"release": "b"}]""")

            assertEquals(listOf("6.1.118-test"), store.releasesOf("offsets.json"))
            assertEquals(listOf("a", "b"), store.releasesOf("multi.json"))
            assertTrue(store.releasesOf("missing.json").isEmpty())
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
