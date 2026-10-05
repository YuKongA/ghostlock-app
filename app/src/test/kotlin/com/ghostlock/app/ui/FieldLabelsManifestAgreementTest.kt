package com.ghostlock.app.ui

import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-10 A4: every localized field label must name a field the App can show.
 *
 * The label keys are the EDITOR's logical paths, while the manifest
 * (`profile-manifest-v3.tsv`) carries owner-qualified WIRE paths, so a label is
 * accepted when a declared path equals it or ends with `.<key>`. The keys that
 * legitimately cannot match are enumerated below WITH THEIR REASON, and the test
 * fails if such an entry becomes unnecessary (stale) — the same
 * exemption-plus-stale-check pattern the native include firewall uses.
 *
 * No runtime behaviour is involved: this only guards the label table.
 */
class FieldLabelsManifestAgreementTest {

    /**
     * Label keys that are intentionally not wire paths.
     *
     * Group 1 — the editor synthesizes them: the profile carries
     * `execution.recommended_cpus.*`, and the screen shows the CPUs actually
     * pinned for this run.
     *
     * Group 2 — a logical path folded into a different wire section:
     * `kernelsnitch.collisions` is written as
     * `backend.cve_2026_43499.kernel.kernelsnitch_collisions`,
     * `kernelsnitch.mm_struct_sz` as `...kernel.mm_struct_sz`, and the two
     * `route.<route>.compact_waiter` labels both name the single wire field
     * `...kernel.compact_waiter` (labelled per route branch in the editor).
     *
     * Group 3 — HOCON-only route tuning that the current v3 schema does not
     * declare (the manifest has no `execution.routes.*`); the labels stay so an
     * older profile that still carries the section remains readable.
     */
    private val logicalOnlyLabels: Map<String, String> = mapOf(
        "execution.selected_cpus.main" to "synthesized from execution.recommended_cpus.main",
        "execution.selected_cpus.consumer" to "synthesized from execution.recommended_cpus.consumer",
        "kernelsnitch.collisions" to "wire path is kernel.kernelsnitch_collisions",
        "kernelsnitch.mm_struct_sz" to "wire path is kernel.mm_struct_sz",
        "route.multicast_waiter.compact_waiter" to "wire path is kernel.compact_waiter",
        "route.tcp_zerocopy.compact_waiter" to "wire path is kernel.compact_waiter",
        "execution.routes.select_stack.consumer_burst_calls" to "HOCON-only route tuning",
        "execution.routes.select_stack.consumer_max_calls" to "HOCON-only route tuning",
        "execution.routes.select_stack.enter_delay_us" to "HOCON-only route tuning",
        "execution.routes.select_stack.timeout_us" to "HOCON-only route tuning",
        "execution.routes.tcp_zerocopy.arm_sequence" to "HOCON-only route tuning",
        "execution.routes.tcp_zerocopy.attempts" to "HOCON-only route tuning",
        "execution.routes.tcp_zerocopy.post_receive_hold_iterations" to "HOCON-only route tuning",
    )

    @Test
    fun `every label names a declared field or a documented logical path`() {
        assertTrue("the label table is empty", fieldLabels.isNotEmpty())
        val declared = NativeProfileGlkv3Adapter.declaredTypeNames().keys
        val unmatched = fieldLabels.keys.filter { key ->
            declared.none { path -> path == key || path.endsWith("." + key) }
        }
        val undocumented = unmatched.filterNot { it in logicalOnlyLabels }
        assertTrue(
            "field labels with no declared manifest path and no documented reason: " +
                undocumented.sorted(),
            undocumented.isEmpty(),
        )
        val stale = logicalOnlyLabels.keys.filterNot { it in unmatched }
        assertTrue(
            "stale logical-path exemptions (the manifest now declares them): " + stale.sorted(),
            stale.isEmpty(),
        )
    }
}
