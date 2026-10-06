package com.ghostlock.app.data.profile

import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * M5 regression: the MERGE path must not drop the declared queue.
 *
 * Two callers feed [NativeProfileDocument.from] two shapes:
 *  - the Gradle exporter hands the MERGED CANONICAL document, whose queue lives
 *    in the declaration `available.<id>.{route,queue}` (this test), and
 *  - the App hands the RUNTIME model, whose queue rides the carrier
 *    `backend.queue_selection.<id>.{...}` (see QueueWireCarriageTest).
 *
 * Both must emit the SAME queue. Before the declaration fallback the exporter
 * path emitted none, so a migrated profile silently lost its step sequence.
 */
class MergedQueueCarriageTest {
    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"

    /** The M3/M5 asset shape: `route` + `queue`, and NO combination token. */
    private fun canonical(): ValueMap = valueMapOf(
        "schema_version" to 3,
        "release" to release,
        "kernel_major" to 5,
        "available" to valueMapOf(
            "cve_2026_43499" to valueMapOf(
                "route" to "multicast_waiter",
                "queue" to listOf(
                    valueMapOf("step" to "w1"),
                    valueMapOf("step" to "w2"),
                    valueMapOf("step" to "w3"),
                ),
            ),
        ),
        "backend" to valueMapOf(
            "cve_2026_43499" to valueMapOf(
                "abi" to valueMapOf("task_struct" to valueMapOf("prio" to 132)),
            ),
        ),
    )

    private fun documentOf(merged: ValueMap): NativeProfileDocument =
        NativeProfileDocument.from(
            release = release,
            route = "multicast_waiter",
            value = { null },
            text = { path -> merged.getValueAt(path) as? String },
            bool = { path -> merged.getValueAt(path) as? Boolean },
            raw = { path -> merged.getValueAt(path) },
        )

    @Test
    fun `a merged canonical profile still carries its declared queue`() {
        val merged = ProfileMerger.resolveMerged(
            deviceRelease = release,
            builtin = canonical(),
            imported = null,
            overrides = null,
            tuningExecution = null,
            pair = CpuPairView(0, 1),
            routePresets = emptyMap(),
        )
        /* The merge is a pure deep merge: the declaration must survive it. */
        assertEquals(
            "multicast_waiter",
            merged["available"].asValueMap()?.get("cve_2026_43499").asValueMap()?.get("route"),
        )
        val document = documentOf(merged)
        assertEquals(
            "the merged profile must carry the declared queue",
            listOf("w1", "w2", "w3"),
            document.stepQueue?.map { it.step },
        )
        assertEquals("multicast_waiter", document.queueRoute)
        assertEquals(null, document.experimental)
    }

    @Test
    fun `an undeclared selection still adds nothing`() {
        val bare = valueMapOf(
            "schema_version" to 3,
            "release" to release,
            "kernel_major" to 5,
            "backend" to valueMapOf(
                "cve_2026_43499" to valueMapOf(
                    "abi" to valueMapOf("task_struct" to valueMapOf("prio" to 132)),
                ),
            ),
        )
        val document = documentOf(bare)
        assertNull(document.stepQueue)
        assertNull(document.queueRoute)
        assertNull(document.experimental)
    }
}
