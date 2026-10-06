package com.ghostlock.app.ui

import com.ghostlock.app.R
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginParamType
import com.ghostlock.app.data.plugin.PluginProbe
import com.ghostlock.app.data.plugin.PluginValue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * batch ② projections: the detail header, the read-only extractor values and the
 * check report. Pure functions only — no Compose, no device.
 */
class PluginCheckReportTest {

    private val sha = "b".repeat(64)

    /**
     * Words distinct from the English defaults: a summary must take them from
     * [PluginTexts] instead of spelling a literal of its own.
     */
    private val words = PluginTexts(
        required = "REQ",
        unresolved = "UNRES",
        defaultLadder = "LADDER",
    )

    private fun entry(
        enabled: Boolean = true,
        stage: String? = "post_terminal",
    ) = PluginManifestEntry(
        id = "demo.plugin",
        version = "1.0",
        abiVersion = 1u,
        sha256 = sha,
        modulePath = "demo.plugin/1.0/demo.plugin.so",
        enabled = enabled,
        stage = stage,
        importedAtMs = 7L,
    )

    private fun descriptor(
        availability: String = "stage_availability\t43499:pre_terminal;43284:post_terminal\n",
        caps: String = "kernel_read",
        withExtract: Boolean = true,
    ) = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpre_spawn,post_spawn,pre_terminal,post_terminal\n" +
            "host_caps\tkernel_read,alias\n" +
            availability +
            "plugin\tdemo.plugin\t1.0\t1\t80\t" + sha + "\tpost_terminal\t" + caps + "\n" +
            "param\tdemo.plugin\tthreshold\tuint\t0\t200\thold\n" +
            (if (withExtract) "extract\tdemo.plugin\toffset\tuint\t1\t-\tfrom the boot image\n" else ""),
    )

    @Test
    fun `the header carries the full digest, the path and this run's intent`() {
        val header = pluginHeaderRows(entry(), descriptor(), "/data/x/demo.plugin.so", selected = true)
        fun data(rows: List<PluginHeaderRow>, labelRes: Int): String =
            (rows.first { it.labelRes == labelRes }.value as PluginHeaderValue.Data).text
        fun words(rows: List<PluginHeaderRow>, labelRes: Int): PluginHeaderValue.Words =
            rows.first { it.labelRes == labelRes }.value as PluginHeaderValue.Words

        assertEquals("demo.plugin", data(header, R.string.plugin_header_id))
        assertEquals("1.0", data(header, R.string.plugin_header_version))
        assertEquals("1", data(header, R.string.plugin_header_abi))
        /* The size is a page word ("%1$s bytes"): the number stays data. */
        val size = words(header, R.string.plugin_header_size)
        assertEquals(R.string.plugin_header_size_bytes, size.resId)
        assertEquals(listOf("80"), size.args)
        /* The FULL digest, not a prefix: it is what gets compared externally. */
        assertEquals(sha, data(header, R.string.plugin_header_sha256))
        assertEquals(64, data(header, R.string.plugin_header_sha256).length)
        assertEquals("/data/x/demo.plugin.so", data(header, R.string.plugin_header_path))
        assertEquals("post_terminal", data(header, R.string.plugin_header_stage))
        assertEquals(R.string.plugin_header_yes, words(header, R.string.plugin_header_enabled).resId)
        assertEquals(R.string.plugin_header_loads, words(header, R.string.plugin_header_this_run).resId)
        assertTrue(header.any { it.labelRes == R.string.plugin_header_host_caps })

        /* Without a descriptor the two value words come out the other way. */
        val idle = pluginHeaderRows(entry(enabled = false), null, "-", selected = false)
        assertEquals(R.string.plugin_header_no, words(idle, R.string.plugin_header_enabled).resId)
        assertEquals(
            R.string.plugin_header_not_loaded,
            words(idle, R.string.plugin_header_this_run).resId,
        )
    }

    @Test
    fun `extract rows are read-only and show unresolved values as such`() {
        val rows = pluginExtractRows(descriptor(), mapOf("offset" to PluginValue.UInt(4096u)))
        assertEquals(1, rows.size)
        val row = rows.single()
        assertEquals("offset", row.name)
        assertEquals(PluginParamType.UInt, row.type)
        assertTrue(row.required)
        assertEquals("4096", row.value)
        assertTrue(pluginExtractSummary(row, words).contains("4096"))

        val unresolved = pluginExtractRows(descriptor(), emptyMap()).single()
        assertNull(unresolved.value)
        assertTrue(pluginExtractSummary(unresolved, words).contains("UNRES"))
    }

    @Test
    fun `the report separates blocking errors, warnings and context`() {
        /* A described, selected, fully-specified plugin: only context. */
        val clean = pluginIssueRows(
            descriptor(withExtract = false),
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43284,
            selected = true,
        )
        assertTrue(clean.all { it.level == PluginIssueLevel.Info })
        assertEquals(R.string.plugin_issue_will_load, clean.single().resId)

        /* The selected backend cannot run this stage (probe matrix). */
        val offBackend = pluginIssueRows(
            descriptor(withExtract = false),
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43499,
            selected = true,
        )
        val warning = offBackend.single { it.level == PluginIssueLevel.Warn }
        assertEquals(R.string.plugin_issue_stage_unavailable, warning.resId)
        assertTrue(warning.args.contains("post_terminal"))
        assertTrue(warning.args.contains("cve_2026_43499"))

        /* Missing capabilities warn; an unresolved extractor value warns. */
        val warnings = pluginIssueRows(
            descriptor(caps = "kernel_read,kernel_write", withExtract = true),
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43284,
            selected = true,
        ).filter { it.level == PluginIssueLevel.Warn }
        assertTrue(
            warnings.any {
                it.resId == R.string.plugin_issue_missing_caps &&
                    it.args.any { arg -> arg.toString().contains("kernel_write") }
            },
        )
        assertTrue(
            warnings.any {
                it.resId == R.string.plugin_issue_extract_unresolved &&
                    it.args.any { arg -> arg.toString().contains("offset") }
            },
        )

        /* A descriptor the host would refuse is an error, not a warning. */
        val rejected = PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpost_terminal\n" +
                "host_caps\tkernel_read\n" +
                "plugin\tdemo.plugin\t1.0\t1\t80\t" + sha + "\tpost_terminal\tkernel_read\n" +
                "reject\tdemo.plugin\treserved capability\n",
        )
        val errors = pluginIssueRows(
            rejected,
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43499,
            selected = true,
        ).filter { it.level == PluginIssueLevel.Error }
        assertTrue(
            errors.any {
                it.resId == R.string.plugin_issue_host_rejects &&
                    it.args.any { arg -> arg.toString().contains("reserved capability") }
            },
        )

        /* No descriptor at all: an error (the document build would fail). */
        val undescribed = pluginIssueRows(
            null,
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43499,
            selected = true,
        )
        assertEquals(PluginIssueLevel.Error, undescribed.single().level)
        assertEquals(R.string.plugin_issue_no_descriptor, undescribed.single().resId)

        /* Disabled or unselected is context, never an error. */
        val disabled = pluginIssueRows(
            descriptor(withExtract = false),
            entry(enabled = false),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43499,
            selected = false,
        )
        assertEquals(
            R.string.plugin_issue_disabled,
            disabled.single { it.level == PluginIssueLevel.Info }.resId,
        )
        val unselected = pluginIssueRows(
            descriptor(withExtract = false),
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43499,
            selected = false,
        )
        assertEquals(
            R.string.plugin_issue_unselected,
            unselected.single { it.level == PluginIssueLevel.Info }.resId,
        )
    }

    private fun specDescriptor(required: Boolean = true) = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read\n" +
            "plugin\tdemo.plugin\t1.0\t1\t80\t" + sha + "\tpost_terminal\tkernel_read\n" +
            "spec\tdemo.plugin\tspec_offset\tuint\t" + (if (required) "1" else "0") +
            "\tdisasm,kallsyms\tsym:task_defex\t-\tbytes:0x??e8e8e8\t1\t-\t4\t-\timage\t-\tdoc\n",
    )

    @Test
    fun `declared extractions show their method chain and stay read-only`() {
        val descriptor = specDescriptor()
        val unresolved = pluginSpecRows(descriptor, emptyMap()).single()
        assertEquals("spec_offset", unresolved.name)
        assertEquals(listOf("disasm", "kallsyms"), unresolved.methods)
        assertTrue(unresolved.required)
        assertNull(unresolved.value)
        val summary = pluginSpecSummary(unresolved, words)
        assertTrue(summary.contains("disasm → kallsyms"))
        assertTrue(summary.contains("anchor=sym:task_defex"))
        assertTrue(summary.contains("pattern=bytes:0x??e8e8e8"))
        assertTrue(summary.contains("UNRES"))

        /* A \`-\` method list is the native default ladder, named as such. */
        val defaultLadder = PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpost_terminal\n" +
                "host_caps\tkernel_read\n" +
                "plugin\tdemo.plugin\t1.0\t1\t80\t" + sha + "\tpost_terminal\tkernel_read\n" +
                "spec\tdemo.plugin\tlate\tstr\t0\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\n",
        )
        val row = pluginSpecRows(defaultLadder, mapOf("late" to PluginValue.Str("0x4000"))).single()
        assertEquals("0x4000", row.value)
        assertTrue(pluginSpecSummary(row, words).contains("LADDER"))

        /* The default ladder is a WORD: the unresolved line carries a nested
         * resource, so the page resolves it in the device locale. */
        val unresolvedDefault = pluginIssueRows(
            defaultLadder,
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43499,
            selected = true,
        ).single { it.resId == R.string.plugin_issue_spec_unresolved }
        assertEquals(PluginLine(R.string.plugin_text_default_ladder), unresolvedDefault.args[1])
    }

    @Test
    fun `an unresolved required extraction blocks, an optional one warns`() {
        fun issues(required: Boolean, value: PluginValue?) = pluginIssueRows(
            descriptor = specDescriptor(required),
            entry = entry(),
            overrides = emptyMap(),
            extracts = if (value == null) emptyMap() else mapOf("spec_offset" to value),
            selectedBackend = BackendKind.Cve2026_43499,
            selected = true,
        )
        /* The probe matrix says 43499 runs pre_terminal only, so the stage also
         * warns; the extraction itself is what is under test here. */
        val blocked = issues(required = true, value = null)
        assertTrue(
            blocked.any {
                it.level == PluginIssueLevel.Error &&
                    it.resId == R.string.plugin_issue_spec_unresolved &&
                    it.args.contains("spec_offset")
            },
        )
        val warned = issues(required = false, value = null)
        assertTrue(warned.none { it.level == PluginIssueLevel.Error })
        assertTrue(
            warned.any {
                it.level == PluginIssueLevel.Warn &&
                    it.resId == R.string.plugin_issue_spec_unresolved &&
                    it.args.contains("spec_offset")
            },
        )
        val resolved = issues(required = true, value = PluginValue.UInt(7u))
        assertTrue(resolved.none { it.args.contains("spec_offset") })
    }

    @Test
    fun `a required parameter without a value blocks the plugin`() {
        val strict = PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpost_terminal\n" +
                "host_caps\tkernel_read\n" +
                "plugin\tdemo.plugin\t1.0\t1\t80\t" + sha + "\tpost_terminal\tkernel_read\n" +
                "param\tdemo.plugin\ttoken\tstr\t1\t-\tmust be set\n",
        )
        val issues = pluginIssueRows(
            strict,
            entry(),
            emptyMap(),
            emptyMap(),
            BackendKind.Cve2026_43499,
            selected = true,
        )
        val error = issues.single { it.level == PluginIssueLevel.Error }
        assertEquals(R.string.plugin_issue_field_error, error.resId)
        assertTrue(error.args.any { arg -> arg.toString().contains("token") })
        /* And the row itself is then not run-usable, which the run summary uses. */
        val rows = pluginRows(listOf(entry()), mapOf("demo.plugin" to strict), null, null)
        assertEquals(listOf("demo.plugin"), selectedPluginErrors(rows))
    }
}
