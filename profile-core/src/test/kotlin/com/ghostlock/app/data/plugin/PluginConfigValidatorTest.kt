package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** P1 validation gate: unknown keys, types, required values, bounds, host sets. */
class PluginConfigValidatorTest {

    private fun descriptor(
        requiredCaps: String = "kernel_read",
        rejected: Boolean = false,
        requiredParam: String? = null,
    ): PluginDescriptor {
        val params = buildString {
            append("param\tdemo.plugin\tarm_delay_us\tuint\t1\t200\thold\n")
            append("param\tdemo.plugin\tsymbol\tstr\t0\t-\tname\n")
            if (requiredParam != null) {
                append("param\tdemo.plugin\t").append(requiredParam)
                    .append("\tstr\t1\t-\tmust be supplied\n")
            }
        }
        val reject = if (rejected) "reject\tdemo.plugin\treserved trigger\n" else ""
        return PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpost_terminal\n" +
                "host_caps\tkernel_read,alias\n" +
                "plugin\tdemo.plugin\t1.0\t1\t10\t" + "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" +
                "\tpost_terminal\t" + requiredCaps + "\n" +
                params + reject,
        )
    }

    private fun paths(errors: List<PluginConfigError>): List<String> = errors.map { it.path }

    @Test
    fun `a clean configuration has no errors`() {
        assertEquals(
            emptyList<PluginConfigError>(),
            PluginConfigValidator.validate(
                descriptor(),
                enabled = true,
                stage = "post_terminal",
                overrides = mapOf("symbol" to PluginValue.Str("task_defex_enforce")),
            ),
        )
    }

    @Test
    fun `unknown parameter names are rejected`() {
        val errors = PluginConfigValidator.validate(
            descriptor(),
            enabled = false,
            stage = null,
            overrides = mapOf("mystery" to PluginValue.UInt(1u)),
        )
        assertEquals(listOf("plugin.demo.plugin.params.mystery"), paths(errors))
        assertTrue(errors.single().reason.contains("no such parameter"))
    }

    @Test
    fun `a type mismatch is rejected`() {
        val errors = PluginConfigValidator.validate(
            descriptor(),
            enabled = false,
            stage = null,
            overrides = mapOf("arm_delay_us" to PluginValue.Str("200")),
        )
        assertEquals(listOf("plugin.demo.plugin.params.arm_delay_us"), paths(errors))
        assertTrue(errors.single().reason.contains("expected uint"))
    }

    @Test
    fun `a required parameter without a default must be supplied`() {
        val missing = PluginConfigValidator.validate(
            descriptor(requiredParam = "token"),
            enabled = false,
            stage = null,
            overrides = emptyMap(),
        )
        assertEquals(listOf("plugin.demo.plugin.params.token"), paths(missing))
        val supplied = PluginConfigValidator.validate(
            descriptor(requiredParam = "token"),
            enabled = false,
            stage = null,
            overrides = mapOf("token" to PluginValue.Str("abc")),
        )
        assertEquals(emptyList<PluginConfigError>(), supplied)
    }

    @Test
    fun `strings are bounded by the GLKv3 limit`() {
        val tooLong = "x".repeat(PluginConfigValidator.MAX_STRING_BYTES + 1)
        val errors = PluginConfigValidator.validate(
            descriptor(),
            enabled = false,
            stage = null,
            overrides = mapOf("symbol" to PluginValue.Str(tooLong)),
        )
        assertEquals(listOf("plugin.demo.plugin.params.symbol"), paths(errors))
        assertTrue(errors.single().reason.contains("256"))
    }

    @Test
    fun `an enabled plugin must fit the host capability and stage sets`() {
        val missingCap = PluginConfigValidator.validate(
            descriptor(requiredCaps = "kernel_read,kernel_hook"),
            enabled = true,
            stage = "post_terminal",
            overrides = emptyMap(),
        )
        assertEquals(listOf("plugin.demo.plugin.required_caps"), paths(missingCap))
        val disabled = PluginConfigValidator.validate(
            descriptor(requiredCaps = "kernel_read,kernel_hook"),
            enabled = false,
            stage = "post_terminal",
            overrides = emptyMap(),
        )
        assertEquals(emptyList<PluginConfigError>(), disabled)
    }

    @Test
    fun `an undeclared stage and a rejected module are errors`() {
        val stage = PluginConfigValidator.validate(
            descriptor(),
            enabled = false,
            stage = "pre_spawn",
            overrides = emptyMap(),
        )
        assertEquals(listOf("plugin.demo.plugin.stage"), paths(stage))
        val rejected = PluginConfigValidator.validate(
            descriptor(rejected = true),
            enabled = false,
            stage = null,
            overrides = emptyMap(),
        )
        assertEquals(listOf("plugin.demo.plugin"), paths(rejected))
    }
}
