package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** P1 probe TSV parser: frozen format (contract-design §3.14.7.2), fail-closed. */
class PluginProbeTest {

    private val sha = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

    private fun header(
        root: String = "countermeasures",
        hostAbi: String = "1",
    ): String = "host_abi\t" + hostAbi + "\n" +
        "countermeasures_root\t" + root + "\n" +
        "host_stages\tpre_spawn,post_spawn,pre_terminal,post_terminal\n" +
        "host_caps\tkernel_read,kernel_write,alias,child_task\n"

    private fun fixture(
        pluginRow: String = "plugin\tdemo.plugin\t1.2.0\t1\t4096\t" + sha +
            "\tpost_terminal\tkernel_read,kernel_write",
        extraRows: String = "",
        headerBlock: String = header(),
    ): String = headerBlock + pluginRow + "\n" +
        "hook\tdemo.plugin\ton_stage\tpost_terminal\t10\tguard.hook\n" +
        "param\tdemo.plugin\tarm_delay_us\tuint\t1\t200\thold before arming\n" +
        "param\tdemo.plugin\tretries\tint\t0\t5\tretry budget\n" +
        "param\tdemo.plugin\tflag\tbool\t0\t-\tdebug switch\n" +
        "param\tdemo.plugin\tsymbol\tstr\t0\ttask_defex_enforce\tdefex symbol\n" +
        "extract\tdemo.plugin\toffset\tuint\t1\t-\tfrom the boot image\n" +
        extraRows

    /** Frozen 16-column spec row (plugin-extract-spec-design §10). */
    @Suppress("LongParameterList")
    private fun specRow(
        name: String = "spec_offset",
        type: String = "uint",
        required: String = "1",
        methods: String = "disasm,profile",
        anchor: String = "sym:task_defex_enforce",
        scope: String = "-",
        pattern: String = "-",
        hit: String = "-",
        capture: String = "-",
        width: String = "-",
        signed: String = "-",
        base: String = "-",
        maxScan: String = "-",
        doc: String = "doc",
    ): String = listOf(
        "spec", "demo.plugin", name, type, required, methods, anchor, scope, pattern,
        hit, capture, width, signed, base, maxScan, doc,
    ).joinToString("\t")

    @Test
    fun `parses the documented rows and headers`() {
        val descriptor = PluginProbe.parse(fixture())
        assertEquals("demo.plugin", descriptor.id)
        assertEquals("1.2.0", descriptor.version)
        assertEquals(1u, descriptor.abiVersion)
        assertEquals(4096u, descriptor.size)
        assertEquals(sha, descriptor.sha256)
        assertEquals(setOf("post_terminal"), descriptor.stages)
        assertEquals(setOf("kernel_read", "kernel_write"), descriptor.requiredCaps)
        assertEquals(1, descriptor.hooks.size)
        assertEquals("on_stage", descriptor.hooks.single().trigger)
        assertEquals(10u, descriptor.hooks.single().priority)
        assertEquals(1u, descriptor.hostAbiVersion)
        assertEquals("countermeasures", descriptor.countermeasuresRoot)
        assertEquals(4, descriptor.hostStages.size)
        assertEquals(4, descriptor.hostCaps.size)
        assertTrue(descriptor.usable)
    }

    @Test
    fun `the per-backend stage matrix is parsed and stays optional`() {
        val matrix = "stage_availability\t43499:pre_terminal;43284:post_terminal\n"
        val described = PluginProbe.parse(fixture(headerBlock = header() + matrix))
        assertEquals(listOf("pre_terminal"), described.stageAvailability["43499"])
        assertEquals(listOf("post_terminal"), described.stageAvailability["43284"])
        /* Two stages in one group stay in the order the probe wrote them. */
        val multi = PluginProbe.parse(
            fixture(headerBlock = header() + "stage_availability\t43499:pre_spawn,pre_terminal\n"),
        )
        assertEquals(listOf("pre_spawn", "pre_terminal"), multi.stageAvailability["43499"])
        /* A capture taken before the header exists parses, with no matrix. */
        assertTrue(PluginProbe.parse(fixture()).stageAvailability.isEmpty())
        assertTrue(
            PluginProbe.parse(fixture(headerBlock = header() + "stage_availability\t-\n"))
                .stageAvailability.isEmpty(),
        )

        fun rejects(value: String) {
            org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                PluginProbe.parse(
                    fixture(headerBlock = header() + "stage_availability\t" + value + "\n"),
                )
            }
        }
        rejects("43499")
        rejects(":pre_terminal")
        rejects("43499:")
        rejects("43499:pre_terminal;43499:post_terminal")
        rejects("43499:pre_terminal,pre_terminal")
        rejects("43499:pre_terminal;")
    }

    /**
     * D2: the ABI types are uint32, so the accepted domain is the unsigned one —
     * a value above Int.MAX_VALUE is legal and a negative value is not.
     */
    @Test
    fun `the ABI numeric domains are unsigned 32-bit like the native formatter`() {
        val big = PluginProbe.parse(
            fixture(
                pluginRow = "plugin\tdemo.plugin\t1.2.0\t4294967295\t4294967295\t" + sha +
                    "\tpost_terminal\tkernel_read",
                extraRows = "hook\tdemo.plugin\ton_stage\tpost_terminal\t3000000000\tlate.hook\n",
                headerBlock = header(hostAbi = "4294967295"),
            ),
        )
        assertEquals(4_294_967_295u, big.abiVersion)
        assertEquals(4_294_967_295u, big.size)
        assertEquals(4_294_967_295u, big.hostAbiVersion)
        assertEquals(3_000_000_000u, big.hooks.first { it.name == "late.hook" }.priority)
        assertEquals(10u, big.hooks.first { it.name == "guard.hook" }.priority)

        fun rejectsRow(row: String) {
            org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                PluginProbe.parse(fixture(extraRows = row + "\n"))
            }
        }
        /* Negative values are rejected in every uint32 column. */
        rejectsRow("hook\tdemo.plugin\ton_stage\tpost_terminal\t-1\tguard.hook")
        /* Columns 3 and 4 are abi_version and size. */
        for (column in listOf(3, 4)) {
            val parts = mutableListOf(
                "plugin", "demo.plugin", "1.2.0", "1", "4096", sha, "post_terminal", "kernel_read",
            )
            parts[column] = "-1"
            org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                PluginProbe.parse(fixture(pluginRow = parts.joinToString("\t")))
            }
        }
        /* Values beyond uint32 are rejected too, not wrapped. */
        org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
            PluginProbe.parse(fixture(headerBlock = header(hostAbi = "-1")))
        }
        org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
            PluginProbe.parse(fixture(headerBlock = header(hostAbi = "4294967296")))
        }
    }

    /**
     * D3: in a REQUIRED column `-` means the producer had nothing to write, so it
     * is a missing value — never a parameter literally named "-".
     */
    @Test
    fun `a dash in a required column is a missing value and is refused`() {
        fun rejectsRow(row: String) {
            val error = org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                PluginProbe.parse(fixture(extraRows = row + "\n"))
            }
            assertTrue(error.message!!.contains("missing"))
        }
        rejectsRow("param\tdemo.plugin\t-\tuint\t0\t-\tdoc")
        rejectsRow("extract\tdemo.plugin\t-\tuint\t1\t-\tdoc")
        rejectsRow("param\tdemo.plugin\t\tuint\t0\t-\tdoc")
        rejectsRow("hook\tdemo.plugin\ton_stage\tpost_terminal\t10\t-")
        /* An optional column keeps its documented meaning. */
        val optional = PluginProbe.parse(
            fixture(extraRows = "param\tdemo.plugin\tnameless_default\tuint\t0\t-\tdoc\n"),
        )
        assertEquals(
            null,
            optional.params.first { it.name == "nameless_default" }.defaultValue,
        )
    }

    /**
     * D7 consumer side: a name registration would refuse must not be accepted
     * here either — at most 64 BYTES, and no control character.
     */
    @Test
    fun `names carry the loader bounds of 64 bytes and no control characters`() {
        fun rejectsRow(row: String) {
            org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                PluginProbe.parse(fixture(extraRows = row + "\n"))
            }
        }
        /* Exactly 64 bytes is accepted. */
        val long64 = "n".repeat(64)
        val accepted = PluginProbe.parse(
            fixture(extraRows = "param\tdemo.plugin\t" + long64 + "\tuint\t0\t-\tdoc\n"),
        )
        assertTrue(accepted.params.any { it.name == long64 })
        /* 65 bytes is not, in any name column. */
        rejectsRow("param\tdemo.plugin\t" + "n".repeat(65) + "\tuint\t0\t-\tdoc")
        rejectsRow("extract\tdemo.plugin\t" + "n".repeat(65) + "\tuint\t1\t-\tdoc")
        rejectsRow("hook\tdemo.plugin\ton_stage\tpost_terminal\t10\t" + "h".repeat(65))
        /* Bytes, not characters: 33 two-byte characters are 66 bytes. */
        rejectsRow("param\tdemo.plugin\t" + "\u00e9".repeat(33) + "\tuint\t0\t-\tdoc")
        /* Control characters (0x00-0x1F, 0x7F) are refused. */
        rejectsRow("param\tdemo.plugin\tbad\u0001name\tuint\t0\t-\tdoc")
        rejectsRow("extract\tdemo.plugin\tbad\u0002name\tuint\t1\t-\tdoc")
        rejectsRow("hook\tdemo.plugin\ton_stage\tpost_terminal\t10\tbad\u007fname")
    }

    /**
     * P2 (frozen 16-column form, plugin-extract-spec-design §10): the plugin
     * declares what the extractor must resolve and how. Additive — the
     * 7-column extract rows above keep their behaviour unchanged.
     */
    @Test
    fun `extract specs parse with their declared methods and stay optional`() {
        val row = specRow(
            type = "int",
            methods = "disasm,profile",
            anchor = "sym:task_defex_enforce",
            scope = "anchor",
            pattern = "bytes:0x??e8",
            hit = "2",
            capture = "1.imm:0",
            width = "4",
            signed = "1",
            base = "anchor",
            maxScan = "0x4000",
        )
        val spec = PluginProbe.parse(fixture(extraRows = row + "\n")).specs.single()
        assertEquals("spec_offset", spec.name)
        assertEquals(PluginParamType.Int, spec.type)
        assertTrue(spec.required)
        assertEquals(listOf("disasm", "profile"), spec.methods)
        assertEquals("sym:task_defex_enforce", spec.anchor)
        assertEquals("anchor", spec.scope)
        assertEquals("bytes:0x??e8", spec.pattern)
        assertEquals(2, spec.hit)
        assertEquals("1.imm:0", spec.capture)
        assertEquals("4", spec.width)
        assertEquals(true, spec.signed)
        assertEquals("anchor", spec.base)
        assertEquals(0x4000L, spec.maxScan)
        assertEquals("doc", spec.doc)

        /* Every `-` column stays NULL: the defaults are native's authority, and
         * Kotlin deliberately does not replicate the default table. */
        val defaulted = PluginProbe.parse(
            fixture(extraRows = specRow(required = "0", methods = "-", anchor = "-") + "\n"),
        ).specs.single()
        assertFalse(defaulted.required)
        assertTrue(defaulted.methods.isEmpty())
        assertNull(defaulted.anchor)
        assertNull(defaulted.scope)
        assertNull(defaulted.pattern)
        assertNull(defaulted.hit)
        assertNull(defaulted.capture)
        assertNull(defaulted.width)
        assertNull(defaulted.signed)
        assertNull(defaulted.base)
        assertNull(defaulted.maxScan)

        /* A well-formed but not-yet-frozen capture kind is ACCEPTED: the kind
         * set is still converging (extractor-rs §3), so Kotlin checks shape. */
        val converging = PluginProbe.parse(
            fixture(extraRows = specRow(methods = "disasm", capture = "pcoff:0") + "\n"),
        ).specs.single()
        assertEquals("pcoff:0", converging.capture)
    }

    @Test
    fun `a malformed extract spec is refused column by column`() {
        fun rejects(row: String) {
            org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                PluginProbe.parse(fixture(extraRows = row + "\n"))
            }
        }
        /* Columns: 15 instead of 16. */
        rejects(
            listOf("spec", "demo.plugin", "offset", "uint", "1", "disasm", "sym:x")
                .joinToString("\t"),
        )
        rejects(specRow(methods = "disasm,magic"))
        rejects(specRow(methods = "disasm,disasm"))
        rejects(specRow(methods = "disasm", anchor = "-"))
        rejects(specRow(anchor = "magic:x"))
        rejects(specRow(anchor = "pc:zz"))
        rejects(specRow(scope = "whole"))
        rejects(specRow(pattern = "regex:.*"))
        rejects(specRow(pattern = "bytes:abc"))
        rejects(specRow(pattern = "bytes:0xzz"))
        rejects(specRow(pattern = "insn:a;b;c;d"))
        rejects(specRow(hit = "0"))
        rejects(specRow(hit = "65"))
        rejects(specRow(capture = "imm"))
        rejects(specRow(capture = "imm:x"))
        rejects(specRow(width = "3"))
        rejects(specRow(signed = "2"))
        rejects(specRow(base = "kernel"))
        rejects(specRow(maxScan = "0"))
        rejects(specRow(maxScan = "0x200000"))
        rejects(specRow(type = "u64"))
        rejects(specRow(required = "2"))
        /* Frozen r5 cross-column rules and the frozen capture set. */
        rejects(specRow(type = "uint", signed = "1"))
        rejects(specRow(type = "int", signed = "0"))
        rejects(specRow(type = "int", width = "auto", signed = "1"))
        rejects(specRow(capture = "symbol_va:0"))
        /* The output namespace is shared with the legacy extract rows. */
        rejects(specRow(name = "offset"))
    }

    @Test
    fun `a probe without a home reports a null root`() {
        val descriptor = PluginProbe.parse(fixture(headerBlock = header(root = "-")))
        assertNull(descriptor.countermeasuresRoot)
    }

    @Test
    fun `parses typed parameter defaults`() {
        val params = PluginProbe.parse(fixture()).paramsByName
        assertEquals(PluginParamType.UInt, params.getValue("arm_delay_us").type)
        assertEquals(PluginValue.UInt(200u), params.getValue("arm_delay_us").defaultValue)
        assertTrue(params.getValue("arm_delay_us").required)
        assertEquals(PluginValue.Int(5), params.getValue("retries").defaultValue)
        assertNull(params.getValue("flag").defaultValue)
        assertEquals(PluginValue.Str("task_defex_enforce"), params.getValue("symbol").defaultValue)
    }

    @Test
    fun `extract rows are separate and keep a missing default`() {
        val descriptor = PluginProbe.parse(fixture())
        val extract = descriptor.extract.single()
        assertEquals("offset", extract.name)
        assertTrue(extract.required)
        assertNull(extract.defaultValue)
        assertNull(descriptor.paramsByName["offset"])
    }

    @Test
    fun `reject rows make the descriptor unusable`() {
        val descriptor = PluginProbe.parse(
            fixture(extraRows = "reject\tdemo.plugin\trequires reserved capability kernel_hook\n"),
        )
        assertFalse(descriptor.usable)
        assertEquals(listOf("requires reserved capability kernel_hook"), descriptor.rejects)
    }

    @Test
    fun `a reject row without a resolvable id is accepted`() {
        val descriptor = PluginProbe.parse(fixture(extraRows = "reject\t-\tcannot parse path\n"))
        assertEquals(listOf("cannot parse path"), descriptor.rejects)
    }

    @Test
    fun `duplicate plugin rows fail closed`() {
        val text = fixture() + "plugin\tdemo.plugin\t9\t1\t1\t" + sha +
            "\tpost_terminal\tkernel_read\n"
        val error = assertThrows(IllegalArgumentException::class.java) { PluginProbe.parse(text) }
        assertTrue(error.message!!.contains("duplicate plugin row"))
    }

    @Test
    fun `unknown parameter type fails closed with the line`() {
        val text = fixture(extraRows = "param\tdemo.plugin\tbad\tfloat\t0\t-\tnope\n")
        val error = assertThrows(IllegalArgumentException::class.java) { PluginProbe.parse(text) }
        assertTrue(error.message!!.contains("unknown parameter type"))
        assertTrue(error.message!!.contains("float"))
    }

    @Test
    fun `required must be 0 or 1`() {
        val text = fixture(extraRows = "param\tdemo.plugin\tbad\tuint\ttrue\t-\tnope\n")
        val error = assertThrows(IllegalArgumentException::class.java) { PluginProbe.parse(text) }
        assertTrue(error.message!!.contains("required must be 0 or 1"))
    }

    @Test
    fun `a row of another plugin fails closed`() {
        val error = assertThrows(IllegalArgumentException::class.java) {
            PluginProbe.parse(fixture(extraRows = "param\tother.plugin\tx\tuint\t0\t-\tnope\n"))
        }
        assertTrue(error.message!!.contains("another plugin"))
    }

    @Test
    fun `the header keys are mandatory and must come first`() {
        val missingRoot = assertThrows(IllegalArgumentException::class.java) {
            PluginProbe.parse(
                "host_abi\t1\nhost_stages\tpost_terminal\nhost_caps\tkernel_read\n" +
                    "plugin\tdemo.plugin\t1\t1\t8\t" + sha + "\tpost_terminal\tkernel_read\n",
            )
        }
        assertTrue(missingRoot.message!!.contains("countermeasures_root"))

        val lateHeader = assertThrows(IllegalArgumentException::class.java) {
            PluginProbe.parse(fixture() + "host_abi\t2\n")
        }
        assertTrue(lateHeader.message!!.contains("after the description block"))
    }

    @Test
    fun `an unknown header key fails closed`() {
        val error = assertThrows(IllegalArgumentException::class.java) {
            PluginProbe.parse(fixture(headerBlock = header() + "mystery\t1\n"))
        }
        assertTrue(error.message!!.contains("unknown header key"))
    }

    @Test
    fun `a missing plugin row fails closed`() {
        val error = assertThrows(IllegalArgumentException::class.java) { PluginProbe.parse(header()) }
        assertTrue(error.message!!.contains("no plugin row"))
    }

    @Test
    fun `rejections reads the failed-probe reasons without parsing`() {
        /* The failed path emits reject rows only, with a non-zero exit; parse()
         * stays strict, this helper never throws. */
        assertEquals(
            listOf("HashMismatch", "reserved capability"),
            PluginProbe.rejections(
                "reject\t-\tHashMismatch\n" +
                    "reject\tdemo.plugin\treserved capability\n",
            ),
        )
        assertEquals(emptyList<String>(), PluginProbe.rejections(""))
        assertEquals(emptyList<String>(), PluginProbe.rejections("reject\t-\n"))
        assertThrows(IllegalArgumentException::class.java) {
            PluginProbe.parse("reject\t-\tHashMismatch\n")
        }
    }
}
