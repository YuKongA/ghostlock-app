package com.ghostlock.app.ui

import com.ghostlock.app.R
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginProbe
import com.ghostlock.app.data.plugin.PluginValue
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * P0 guard: EVERY line the plugin and payload pages render is a resource, and a
 * resource id is never 0 — rendering 0 crashed a real device with
 * `Resources$NotFoundException: String resource ID #0x0`.
 *
 * The value types refuse 0 at construction; this test walks the projections
 * instead, so a NEW line added without a resource fails here rather than on the
 * device. The branches asserted at the end are the interesting ones: if a
 * projection stops producing them, the guard would pass vacuously.
 */
class MessageResIdGuardTest {

    private val sha = "c".repeat(64)

    private fun entry(
        id: String = "demo.plugin",
        enabled: Boolean = true,
        stage: String? = "post_terminal",
    ) = PluginManifestEntry(
        id = id,
        version = "1.0",
        abiVersion = 1u,
        sha256 = sha,
        modulePath = id + "/1.0/" + id + ".so",
        enabled = enabled,
        stage = stage,
        importedAtMs = 7L,
    )

    private fun descriptor(
        id: String = "demo.plugin",
        availability: String = "stage_availability\t43499:pre_terminal\n",
        caps: String = "kernel_read,kernel_write",
    ) = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read\n" +
            availability +
            "plugin\t" + id + "\t1.0\t1\t80\t" + sha + "\tpost_terminal\t" + caps + "\n" +
            "extract\t" + id + "\toffset\tuint\t1\t-\tdoc\n" +
            "spec\t" + id + "\tspec_offset\tuint\t0\t" +
            "-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\n",
    )

    private fun rejected(id: String = "demo.plugin") = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read\n" +
            "plugin\t" + id + "\t1.0\t1\t80\t" + sha + "\tpost_terminal\tkernel_read\n" +
            "reject\t" + id + "\treserved capability\n",
    )

    private fun checkLine(line: PluginLine) {
        assertTrue("a page line has no resource: " + line, line.resId != 0)
        line.args.filterIsInstance<PluginLine>().forEach { checkLine(it) }
    }

    @Test
    fun `every plugin report line names a real resource`() {
        val issues = listOf(
            /* Warns: missing caps, unresolved extract, unresolved spec, stage. */
            pluginIssueRows(
                descriptor(),
                entry(),
                mapOf("mystery" to PluginValue.UInt(1u)),
                emptyMap(),
                BackendKind.Cve2026_43499,
                selected = true,
            ),
            /* Disabled, then unselected: context lines. */
            pluginIssueRows(
                descriptor(),
                entry(enabled = false),
                emptyMap(),
                emptyMap(),
                BackendKind.Cve2026_43499,
                selected = false,
            ),
            pluginIssueRows(
                descriptor(),
                entry(),
                emptyMap(),
                emptyMap(),
                BackendKind.Cve2026_43499,
                selected = false,
            ),
            /* The probe describes another module, and the host would refuse one. */
            pluginIssueRows(
                descriptor(id = "other.plugin"),
                entry(),
                emptyMap(),
                emptyMap(),
                BackendKind.Cve2026_43499,
                selected = true,
            ),
            pluginIssueRows(
                rejected(),
                entry(),
                emptyMap(),
                emptyMap(),
                BackendKind.Cve2026_43499,
                selected = true,
            ),
            /* No descriptor at all: generic, and with the probe's own reason. */
            pluginIssueRows(
                null,
                entry(),
                emptyMap(),
                emptyMap(),
                BackendKind.Cve2026_43499,
                selected = true,
            ),
            pluginIssueRows(
                null,
                entry(),
                emptyMap(),
                emptyMap(),
                BackendKind.Cve2026_43499,
                selected = true,
                describeFailure = "the module file is missing: /data/x/demo.plugin.so",
            ),
        ).flatten()

        issues.forEach { issue ->
            assertTrue("an issue row has no resource: " + issue, issue.resId != 0)
            issue.args.filterIsInstance<PluginLine>().forEach { checkLine(it) }
        }
        assertTrue(issues.any { it.resId == R.string.plugin_issue_stage_unavailable })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_missing_caps })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_extract_unresolved })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_spec_unresolved })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_field_error })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_id_mismatch })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_host_rejects })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_disabled })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_unselected })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_will_load })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_no_descriptor })
        assertTrue(issues.any { it.resId == R.string.plugin_issue_describe_failed })
    }

    @Test
    fun `every header label and word names a real resource`() {
        val rows = pluginHeaderRows(entry(), descriptor(), "/data/x/demo.plugin.so", selected = true) +
            pluginHeaderRows(entry(enabled = false, stage = null), null, "-", selected = false)
        rows.forEach { row ->
            assertTrue("a header label has no resource: " + row, row.labelRes != 0)
            (row.value as? PluginHeaderValue.Words)?.let {
                assertTrue("a header word has no resource: " + it, it.resId != 0)
            }
        }
    }

    @Test
    fun `every plugin row line names a real resource`() {
        val rows = pluginRows(
            entries = listOf(entry(), entry(id = "bad.plugin", stage = null)),
            descriptors = mapOf("demo.plugin" to descriptor()),
            selectedBackend = BackendKind.Cve2026_43499,
            runSelection = null,
            describeFailures = mapOf("bad.plugin" to "the module file is missing"),
        )
        rows.forEach { row ->
            row.blockedReason?.let { checkLine(it) }
            row.stageNote?.let { checkLine(it) }
            row.summary.forEach { checkLine(it) }
        }
        assertTrue(rows.any { it.stageNote != null })
        assertTrue(rows.any { it.blockedReason?.resId == R.string.plugin_issue_describe_failed })
    }

    @Test
    fun `every payload check line names a real resource`() {
        val drafts = listOf(
            PayloadDraft(),
            PayloadDraft(tier = PayloadTier.Exec, execCommand = "id", execSha256 = "not-a-hash"),
            PayloadDraft(tier = PayloadTier.Script),
            PayloadDraft(
                tier = PayloadTier.Ko,
                koEntries = List(PAYLOAD_MAX_KO + 1) { PluginKoEntryForTest(it) },
            ),
        )
        drafts.flatMap { payloadMessages(it) }.forEach {
            assertTrue("a payload line has no resource: " + it, it.resId != 0)
        }
    }

    private fun PluginKoEntryForTest(index: Int) =
        PayloadKoEntry(name = "k" + index + ".ko", path = "ko/k" + index + ".ko", sha256 = null)
}
