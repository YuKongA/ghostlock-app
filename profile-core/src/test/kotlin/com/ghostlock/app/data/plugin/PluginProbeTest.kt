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

    @Test
    fun `parses the documented rows and headers`() {
        val descriptor = PluginProbe.parse(fixture())
        assertEquals("demo.plugin", descriptor.id)
        assertEquals("1.2.0", descriptor.version)
        assertEquals(1, descriptor.abiVersion)
        assertEquals(4096L, descriptor.size)
        assertEquals(sha, descriptor.sha256)
        assertEquals(setOf("post_terminal"), descriptor.stages)
        assertEquals(setOf("kernel_read", "kernel_write"), descriptor.requiredCaps)
        assertEquals(1, descriptor.hooks.size)
        assertEquals("on_stage", descriptor.hooks.single().trigger)
        assertEquals(10, descriptor.hooks.single().priority)
        assertEquals(1, descriptor.hostAbiVersion)
        assertEquals("countermeasures", descriptor.countermeasuresRoot)
        assertEquals(4, descriptor.hostStages.size)
        assertEquals(4, descriptor.hostCaps.size)
        assertTrue(descriptor.usable)
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
