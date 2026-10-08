package com.ghostlock.app.data.profile

import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.AvailablePriority
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.copyValue
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.mutableChild
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The DECLARATION decides the document shape (design 2.9-1 / M2):
 * `available.<backend>` names the backends a profile may run, `applyNormalize`
 * projects the declared one into the runtime model as `backend.kind`, and the
 * adapter emits the wire accordingly.
 *
 * Fixtures are built IN MEMORY (no asset, no file name, no comment text), so an
 * asset rename or a profile that starts declaring 43499 cannot invalidate these
 * guards.
 */
class DeclarationDrivesDocumentShapeTest {

    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"
    private val route = "multicast_waiter"

    private fun queueOf(vararg steps: String): List<Any?> =
        steps.map { valueMapOf("step" to it) }

    /**
     * A canonical profile declaring the given path(s).
     *
     * The DECLARED default is the priority authority (design 2.8.A /
     * AvailablePriority): no priority means token order, which would silently
     * turn a "43499 is the default" fixture into a 43284 fixture. The order is
     * therefore stated explicitly through priority, never through map order.
     */
    private fun canonical(include43499: Boolean): ValueMap = valueMapOf(
        "schema_version" to 3,
        "release" to release,
        "kernel_major" to 5,
        "available" to valueMapOf(
            "cve_2026_43284" to valueMapOf(
                /* 43284 is the default only when 43499 is not declared. */
                "priority" to if (include43499) 2 else 1,
                "queue" to queueOf("pagecache_write"),
            ),
        ).also { available ->
            if (include43499) {
                available["cve_2026_43499"] = valueMapOf(
                    "priority" to 1,
                    "route" to route,
                    "queue" to queueOf("w1", "w2", "w3"),
                )
            }
        },
        "backend" to valueMapOf(
            "cve_2026_43499" to valueMapOf(
                "abi" to valueMapOf(
                    "task_struct" to valueMapOf("prio" to 124),
                    "offset" to valueMapOf("init_task" to 47529984L),
                    "cred" to valueMapOf("caps_offset" to 0x88),
                ),
            ),
        ),
    )

    private fun normalized(map: ValueMap): ValueMap {
        ProfileLayout.applyNormalize(map)
        return map
    }

    /**
     * Self-diagnostics for the declaration-order cases (kept permanently: the
     * failure message must explain the ORDER authority by itself). It prints the
     * three things that decide which backend a document selects:
     *   1. the canonical `available` after canonicalize (verbatim),
     *   2. AvailablePriority.orderedBackends on that map (the declared order),
     *   3. the runtime projection: backend.kind and the carrier keys.
     */
    private fun orderDiagnostics(canonical: ValueMap): String {
        val available = canonical["available"].asValueMap()
        val ordered = if (available == null) emptyList() else AvailablePriority.orderedBackends(available)
        val runtime = canonical.copyValue().asValueMap() ?: canonical
        ProfileLayout.applyNormalize(runtime)
        val carrier = runtime["backend"].asValueMap()
            ?.get(ProfileLayout.QueueSelectionKey).asValueMap()
            ?.keys?.filterIsInstance<String>().orEmpty()
        return " | avail=" + available + " | ordered=" + ordered +
            " | runtime.kind=" + (runtime["backend"].asValueMap()?.get("kind")) +
            " | carrier=" + carrier
    }

    private fun kindOf(map: ValueMap): String? =
        map["backend"].asValueMap()?.get("kind") as? String

    private fun documentOf(map: ValueMap): NativeProfileDocument =
        NativeProfileDocument.from(
            release = release,
            route = route,
            raw = { path -> map.getValueAt(path) },
            value = { path -> ProfileResolver.nativeValue(map, route, path) },
            text = { path -> ProfileResolver.nativeText(map, path) },
            bool = { path -> ProfileResolver.nativeBool(map, path) },
        )

    private fun wireRouteOf(map: ValueMap): String? =
        NativeProfileGlkv3Adapter.adapt(documentOf(map)).route

    /** Wire entry keys of one section, or null when the section is absent. */
    private fun wireEntriesOf(map: ValueMap, sectionName: String): List<String>? =
        NativeProfileGlkv3Adapter.adapt(documentOf(map))
            .sections.firstOrNull { it.name == sectionName }
            ?.entries?.map { it.key }

    @Test
    fun `a 43284-only profile is route-less by design`() {
        val runtime = normalized(canonical(include43499 = false))
        assertEquals("cve_2026_43284", kindOf(runtime))
        assertNull(
            "43284 has no route axis: the wire must not carry a root route",
            wireRouteOf(runtime),
        )
    }

    @Test
    fun `declaring 43499 again restores the wire route`() {
        val runtime = normalized(canonical(include43499 = true))
        assertEquals(
            "the declared default decides the document backend" +
                orderDiagnostics(canonical(include43499 = true)),
            "cve_2026_43499",
            kindOf(runtime),
        )
        assertEquals("a declared 43499 document must carry its route", route, wireRouteOf(runtime))
    }

    @Test
    fun `the selected 43284 document carries its declared queue`() {
        val runtime = normalized(canonical(include43499 = false))
        assertEquals("cve_2026_43284", kindOf(runtime))
        assertEquals(1, documentOf(runtime).stepQueue?.size)
        val entries = wireEntriesOf(runtime, "backend.cve_2026_43284")
        assertTrue("the 43284 section must ride, got: " + entries, entries != null)
        assertTrue("it must carry the declared queue, got: " + entries, entries!!.contains("queue"))
        assertNull(
            "a 43284 document must not carry the 43499 section",
            wireEntriesOf(runtime, "backend.cve_2026_43499"),
        )
    }

    @Test
    fun `a declared 43499 document carries its own queue`() {
        val runtime = normalized(canonical(include43499 = true))
        assertEquals(
            "the declared default decides the document backend" +
                orderDiagnostics(canonical(include43499 = true)),
            "cve_2026_43499",
            kindOf(runtime),
        )
        val entries = wireEntriesOf(runtime, "backend.cve_2026_43499")
        assertTrue("the 43499 section must ride, got: " + entries, entries != null)
        assertTrue("it must carry the declared queue, got: " + entries, entries!!.contains("queue"))
    }

    @Test
    fun `an explicit 43499 preference wins and carries no queue without a declaration`() {
        /* The App injects the explicit preference into backend.kind. A profile that
         * declares ONLY 43284 has no 43499 queue to carry: the SHAPE follows the
         * preference, the PAYLOAD follows the declaration. */
        val runtime = normalized(canonical(include43499 = false))
        runtime.mutableChild("backend")["kind"] = "cve_2026_43499"
        val adapted = NativeProfileGlkv3Adapter.adapt(documentOf(runtime))
        assertEquals("the explicit preference must win over the declaration", "cve_2026_43499", adapted.backend)
        val owner = adapted.sections.firstOrNull { it.name == "backend.cve_2026_43499" }
        assertTrue(
            "no declaration means no queue may ride, got: " + owner?.entries?.map { it.key },
            owner?.entries?.none { it.key == "queue" } != false,
        )
        assertNull(
            "a 43499 document must not carry the 43284 section",
            wireEntriesOf(runtime, "backend.cve_2026_43284"),
        )
    }

    @Test
    fun `an imported layer keeps its declared queue across the double normalization`() {
        /* The import path normalizes TWICE (UserProfileStore.loadEntry). The
         * imported 43499 declaration must survive the round trip, merge with a
         * builtin that declares only 43284, and still reach the wire. */
        val imported = normalized(canonical(include43499 = true))
        ProfileLayout.applyNormalize(imported)
        assertEquals(
            "the round trip must keep the declared queue",
            listOf("w1", "w2", "w3"),
            documentOf(imported).stepQueue?.map { it.step },
        )
        val merged = ProfileMerger.resolveMerged(
            deviceRelease = release,
            builtin = normalized(canonical(include43499 = false)),
            imported = imported,
            overrides = null,
            tuningExecution = null,
            pair = CpuPairView(0, 1),
            routePresets = emptyMap(),
        )
        assertEquals("the imported layer selects 43499", "cve_2026_43499", kindOf(merged))
        assertEquals(listOf("w1", "w2", "w3"), documentOf(merged).stepQueue?.map { it.step })
        val entries = wireEntriesOf(merged, "backend.cve_2026_43499")
        assertTrue("the bare 43499 section must ride, got: " + entries, entries != null)
        assertTrue("it must carry the declared queue, got: " + entries, entries!!.contains("queue"))
    }
}
