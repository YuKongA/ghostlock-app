package com.ghostlock.app.data.profile

import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * M4(a): a profile that DECLARES a queue must reach the wire.
 *
 * The chain under test is the production one: canonical map -> [ProfileLayout]
 * (runtime model) -> [NativeProfileDocument.from] with the dotted-path
 * accessors -> GLKv3 adapter -> encoder. The bytes are compared against the
 * native golden, so a queue that never leaves the runtime model fails here
 * instead of silently shipping a different plan.
 */
class QueueWireCarriageTest {

    /** The native fixture values, declared through the M2 object form. */
    private fun canonicalProfile(withSelection: Boolean = true): ValueMap = valueMapOf(
        "schema_version" to 3,
        "release" to "5.15.189-android13-8-00016-g51bba4309aac",
        "kernel_major" to 5,
        "kernel_minor" to 15,
        "safe_mode" to true,
        "available" to valueMapOf(
            "cve_2026_43499" to if (withSelection) {
                valueMapOf(
                    "route" to "multicast_waiter",
                    "queue" to listOf(valueMapOf("step" to "w1")),
                    "experimental" to true,
                )
            } else {
                /* The legacy token list declares no queue selection. */
                valueMapOf()
            },
        ),
        "backend" to valueMapOf(
            "cve_2026_43499" to valueMapOf(
                "steps" to "mcast_rootchild",
                "abi" to valueMapOf(
                    "task_struct" to valueMapOf("prio" to 101),
                    "kernel" to valueMapOf(
                        "kernel_phys_load" to 0xb000,
                        "kernel_phys_offset" to 0xc000,
                    ),
                    "offset" to valueMapOf("init_task" to 34677760),
                ),
                "cred" to valueMapOf(
                    "caps_value" to 0x123456789abcdef0L,
                    "ref0_image" to 0x1111111111111111L,
                ),
                "offset" to valueMapOf("vr_sys_exit_tp" to 42),
                "kernel" to valueMapOf(
                    "compact_waiter" to true,
                    "kernelsnitch_collisions" to 7,
                    "mm_struct_sz" to 0x400,
                ),
                "route" to valueMapOf(
                    "multicast_waiter" to valueMapOf(
                        "waiter_off" to -2,
                        "buffer_size" to 512,
                        "task_offset" to 0x30,
                        "lock_offset" to 0x40,
                        "attempts" to 3,
                        "arm_sequence" to 4,
                        "arm_hold" to 20000,
                    ),
                ),
            ),
        ),
    )

    private fun runtimeOf(profile: ValueMap): ValueMap = ProfileLayout.normalize(profile)

    /**
     * The runtime carrier `backend.queue_selection.<id>.{queue,experimental,
     * queue_route}`. It is owner-qualified (the App re-kinds `backend.kind`) and
     * nested under a NEUTRAL key (a `backend.<id>` key would make
     * [ProfileLayout.isCanonical] treat the runtime model as a canonical
     * document, which is the v1 save/load regression this batch fixes).
     */
    private fun selectionOf(runtime: ValueMap): ValueMap = checkNotNull(
        runtime["backend"].asValueMap()?.get(ProfileLayout.QueueSelectionKey)
            .asValueMap()?.get("cve_2026_43499").asValueMap(),
    ) { "the runtime model must carry the declared selection" }

    /** The production accessor set, over the runtime map. */
    private fun documentOf(profile: ValueMap): NativeProfileDocument {
        val runtime = runtimeOf(profile)
        return NativeProfileDocument.from(
            release = runtime["release"] as String,
            /* The profile declares this route (available.<id>.route). */
            route = "multicast_waiter",
            value = { path -> ProfileResolver.nativeValue(runtime, "multicast_waiter", path) },
            text = { path -> ProfileResolver.nativeText(runtime, path) },
            bool = { path -> ProfileResolver.nativeBool(runtime, path) },
            raw = { path -> runtime.getValueAt(path) },
            /* safe_mode is patched separately in production (0 => absent here);
             * pin it so the byte comparison covers the whole document. */
        ).copy(safeMode = 1u)
    }

    @Test
    fun `the declared queue rides the runtime model`() {
        val selection = selectionOf(runtimeOf(canonicalProfile()))
        assertEquals("multicast_waiter", selection["queue_route"])
        assertEquals(true, selection["experimental"])
        assertEquals(listOf(valueMapOf("step" to "w1")), selection["queue"])
    }

    @Test
    fun `a declared queue reaches the wire byte for byte`() {
        val adapted = NativeProfileGlkv3Adapter.adapt(documentOf(canonicalProfile()))
        assertEquals(
            Glkv3Value.Array(listOf(Glkv3Value.Map(listOf("step" to Glkv3Value.Str("w1"))))),
            adapted.valueOf("backend.cve_2026_43499", "queue"),
        )
        /* The queue-level route token is the wire key `route`; the geometry map
         * keeps its own section, so both ride at once. */
        assertEquals(
            Glkv3Value.Str("multicast_waiter"),
            adapted.valueOf("backend.cve_2026_43499", "route"),
        )
        assertEquals(
            Glkv3Value.Bool(true),
            adapted.valueOf("backend.cve_2026_43499", "experimental"),
        )
        assertTrue(
            "the route geometry must still ride",
            adapted.sections.any { it.name == "backend.cve_2026_43499.route.multicast_waiter" },
        )
        /* Strongest form: the runtime path reproduces the native fixture exactly. */
        assertEquals(
            "the runtime -> from() -> wire path drifted from the native fixture",
            Glkv3Golden.SELECTION_HEX,
            hex(Glkv3Encoder.encode(adapted)),
        )
    }

    @Test
    fun `a 43284 runtime model is not canonical and survives re-validation`() {
        val canonical = ProfileLayout.canonicalize(
            valueMapOf(
                "schema_version" to 3,
                "release" to "6.1.118-test",
                "kernel_major" to 6,
                "available" to valueMapOf("cve_2026_43284" to valueMapOf("queue" to listOf(valueMapOf("step" to "pagecache_write")))),
                "backend" to valueMapOf("cve_2026_43284" to valueMapOf("steps" to "umh")),
            ),
        )
        val runtime = ProfileLayout.toRuntime(canonical)
        val backend = runtime["backend"].asValueMap()!!
        assertEquals("cve_2026_43284", backend["kind"])
        /* The 43284 policy view is what used to make this model look canonical. */
        assertTrue(backend.containsKey("cve_2026_43284"))
        assertFalse(
            "the 43284 runtime model must not look canonical",
            ProfileLayout.isCanonical(runtime),
        )
        /* The App persists this shape and re-validates it on load. */
        val reloaded = ProfileLayout.normalize(runtime)
        assertEquals("cve_2026_43284", reloaded["backend"].asValueMap()?.get("kind"))
    }

    @Test
    fun `re-kinding the runtime model does not leak the declared queue`() {
        val runtime = runtimeOf(canonicalProfile(true))
        val backend = runtime["backend"].asValueMap()!!
        /* What AndroidProfileConfigController does when the user picks 43284. */
        backend["kind"] = "cve_2026_43284"
        backend["steps"] = "umh"
        val document = NativeProfileDocument.from(
            release = runtime["release"] as String,
            route = null,
            value = { null },
            text = { path -> runtime.getValueAt(path) as? String },
            bool = { path -> runtime.getValueAt(path) as? Boolean },
            raw = { path -> runtime.getValueAt(path) },
        )
        assertEquals(BackendKind.Cve2026_43284.wire.toUInt(), document.backendKind)
        assertEquals("the 43499 queue must not ride a 43284 document", null, document.queueRoute)
        assertEquals(null, document.stepQueue)
        assertEquals(null, document.experimental)
    }

    @Test
    fun `the runtime model is never mistaken for the canonical document shape`() {
        /* Regression guard (v1 save path): the App persists the runtime model and
         * re-validates it on load, so [ProfileLayout.isCanonical] must say NO for
         * it in both shapes. An owner-qualified `backend.<id>` key here makes the
         * runtime model look canonical and a saved profile (root-level `cred`)
         * then fails the strict canonical validator. */
        for (profile in listOf(canonicalProfile(true), canonicalProfile(false))) {
            val runtime = runtimeOf(profile)
            assertFalse(
                "the runtime model must not look canonical: " +
                    runtime["backend"].asValueMap()?.keys,
                ProfileLayout.isCanonical(runtime),
            )
            /* And the flat carrier is what keeps it that way. */
            assertTrue(
                "queue selections ride flat keys under backend",
                runtime["backend"].asValueMap()?.keys?.none { it.startsWith("cve_2026_") } == true,
            )
        }
    }

    @Test
    fun `an undeclared queue adds no bytes`() {
        val adapted = NativeProfileGlkv3Adapter.adapt(documentOf(canonicalProfile(false)))
        /* M5: with no declaration the backend owner section may be absent entirely;
         * what the wire must never carry is a selection key. */
        val owner = adapted.sections.firstOrNull { it.name == "backend.cve_2026_43499" }
        assertTrue(
            "no selection key may ride without a declaration",
            owner == null ||
                owner.entries.none { it.key == "queue" || it.key == "route" || it.key == "experimental" },
        )
        assertEquals(Glkv3Golden.NO_SELECTION_HEX, hex(Glkv3Encoder.encode(adapted)))
    }

    /** Named lookup: a missing entry reports the wire path it expected. */
    private fun Glkv3Document.valueOf(sectionName: String, key: String): Glkv3Value {
        val section = sections.firstOrNull { it.name == sectionName }
        val entry = section?.entries?.firstOrNull { it.key == key }
        assertTrue("the wire must carry $sectionName.$key", entry != null)
        return entry!!.value
    }

    private fun hex(bytes: ByteArray): String =
        bytes.joinToString("") { "%02x".format(it.toInt() and 0xFF) }
}
