package com.ghostlock.app.data

import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * HOCON v3 fail-closed guards (native b55708a8 + 23958eb0). The v3 shape was never
 * released, so the pre-refactor owners are simply REJECTED — there is no migration
 * for them (the only migration point stays `LegacyProfileConverter` for v1).
 *
 * Every case is a canonical document (wrapped in `ghostlock { }`, so the layout
 * takes the canonical path) that must fail with its dotted path echoed.
 */
class ProfileLayoutV3RejectionTest {

    private fun canonical(vararg pairs: Pair<String, Any?>) =
        valueMapOf("ghostlock" to valueMapOf(*pairs))

    private fun rejects(fragment: String, document: Map<String, Any?>) {
        val error = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.normalize(document as ValueMap)
        }
        assertTrue(
            "error must name the offending path ($fragment): " + error.message,
            error.message.orEmpty().contains(fragment),
        )
    }

    @Test
    fun `the deleted common owner is rejected`() = rejects(
        "common",
        canonical(
            "schema_version" to 3,
            "release" to "6.1.145-android14-11-maybe-dirty",
            "common" to valueMapOf("kernel_major" to 6),
        ),
    )

    @Test
    fun `the deleted platform owner is rejected`() = rejects(
        "platform",
        canonical(
            "schema_version" to 3,
            "release" to "6.1.145-android14-11-maybe-dirty",
            "platform" to valueMapOf("abi" to valueMapOf("kernel" to valueMapOf())),
        ),
    )

    @Test
    fun `the replaced selection block is rejected`() = rejects(
        "selection",
        canonical(
            "schema_version" to 3,
            "release" to "6.1.145-android14-11-maybe-dirty",
            "selection" to valueMapOf("backend" to "cve_2026_43499", "terminal" to "root_child"),
        ),
    )

    @Test
    fun `a kernel scalar inside a section is rejected`() = rejects(
        "kernel_major",
        canonical(
            "schema_version" to 3,
            "release" to "6.1.145-android14-11-maybe-dirty",
            "backend" to valueMapOf(
                "cve_2026_43499" to valueMapOf("kernel_major" to 6),
            ),
        ),
    )

    @Test
    fun `the native-side conventions are rejected in a profile`() {
        /* kmi / lkm_path / carrier_path are native conventions (derived from the
         * release, $GHOSTLOCK_HOME, or the device's vendor library). */
        for (key in listOf("kmi", "lkm_path", "carrier_path")) {
            rejects(
                key,
                canonical(
                    "schema_version" to 3,
                    "release" to "6.1.145-android14-11-maybe-dirty",
                    "backend" to valueMapOf(
                        "cve_2026_43284" to valueMapOf(key to 1),
                    ),
                ),
            )
        }
    }

    @Test
    fun `the pre-refactor flat 43284 execution keys are rejected`() {
        /* They live under backend.cve_2026_43284.execution.* now. */
        for (key in listOf("wait_timeout_ms", "module_poll_attempts", "late_load_args")) {
            rejects(
                key,
                canonical(
                    "schema_version" to 3,
                    "release" to "6.1.145-android14-11-maybe-dirty",
                    "backend" to valueMapOf(
                        "cve_2026_43284" to valueMapOf(key to 1),
                    ),
                ),
            )
        }
    }

    @Test
    fun `the new shape is accepted`() {
        /* Control: the same document with the new shape parses. */
        val flat = ProfileLayout.normalize(
            canonical(
                "schema_version" to 3,
                "release" to "6.1.145-android14-11-maybe-dirty",
                "kernel_major" to 6,
                "available" to valueMapOf("cve_2026_43499" to valueMapOf("route" to "multicast_waiter", "queue" to listOf(valueMapOf("step" to "w1"), valueMapOf("step" to "w2"), valueMapOf("step" to "w3")))),
                "backend" to valueMapOf(
                    "cve_2026_43499" to valueMapOf(
                        "steps" to "mcast_rootchild",
                        "abi" to valueMapOf("kernel" to valueMapOf("kernel_phys_load" to 1)),
                    ),
                ),
            ),
        )
        assertTrue(flat.containsKey("kernel_major"))
    }
}
