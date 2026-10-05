package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * P1 cross-language path agreement (frozen in contract-design §3.14.7.4).
 *
 * The countermeasures root and the module-path rule are exported by the native
 * probe header (`countermeasures_root`) and the wire contract; until the probe
 * lands this test pins the Kotlin side and the composition rule, and it is where
 * the header comparison is added (parse the probe output, assert
 * [PluginPaths.probeRootMatches]).
 */
class PluginPathAgreementTest {

    @Test
    fun `the countermeasures root matches the frozen contract value`() {
        assertEquals("countermeasures", PluginPaths.COUNTERMEASURES_ROOT)
    }

    /**
     * The device-captured probe stdout is the other half of the agreement: its
     * header must name the same root the App composes paths under, and the
     * module path it implies must round-trip through the Kotlin layout rules.
     */
    @Test
    fun `the device probe header agrees with the Kotlin root and layout`() {
        val golden = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("plugin-probe-golden.tsv"),
        ) { "missing plugin-probe-golden.tsv" }.bufferedReader().use { it.readText() }
        val descriptor = PluginProbe.parse(golden)
        assertEquals(PluginPaths.COUNTERMEASURES_ROOT, descriptor.countermeasuresRoot)
        assertEquals(
            PluginPaths.ProbeRootCheck.Match,
            PluginPaths.checkProbeRoot(descriptor.countermeasuresRoot),
        )
        val modulePath = PluginPaths.modulePath(descriptor.id, descriptor.version, "module.so")
        assertEquals("test.schema/1.2.3/module.so", modulePath)
        assertTrue(PluginPaths.isSafeModulePath(modulePath))
        assertEquals(
            "countermeasures/" + modulePath,
            PluginPaths.appRelativePath(descriptor.id, descriptor.version, "module.so"),
        )
    }

    @Test
    fun `the probe header root must equal the contract root exactly`() {
        /* The header carries the environment-independent directory NAME, so an
         * absolute path is a mismatch, not a match. */
        assertEquals(
            PluginPaths.ProbeRootCheck.Match,
            PluginPaths.checkProbeRoot("countermeasures"),
        )
        assertEquals(
            PluginPaths.ProbeRootCheck.Mismatch,
            PluginPaths.checkProbeRoot("/data/user/0/com.ghostlock.app/files/countermeasures"),
        )
        assertEquals(
            PluginPaths.ProbeRootCheck.Mismatch,
            PluginPaths.checkProbeRoot("/data/local/tmp/ghostlock-app/plugins"),
        )
        assertEquals(
            PluginPaths.ProbeRootCheck.Mismatch,
            PluginPaths.checkProbeRoot("countermeasuress"),
        )
        /* No GHOSTLOCK_HOME: nothing to compare, recorded as unverifiable. */
        assertEquals(
            PluginPaths.ProbeRootCheck.Unverifiable,
            PluginPaths.checkProbeRoot("-"),
        )
        assertEquals(
            PluginPaths.ProbeRootCheck.Unverifiable,
            PluginPaths.checkProbeRoot(null),
        )
        assertTrue(PluginPaths.probeRootMatches("countermeasures"))
        assertFalse(PluginPaths.probeRootMatches("/tmp/countermeasures"))
    }

    @Test
    fun `the on-disk layout adds the root while the wire path does not`() {
        assertEquals(
            "countermeasures/vivo.vr_guard/1.2.0/vivo_vr_guard.so",
            PluginPaths.appRelativePath("vivo.vr_guard", "1.2.0", "vivo_vr_guard.so"),
        )
        assertEquals(
            "vivo.vr_guard/1.2.0/vivo_vr_guard.so",
            PluginPaths.modulePath("vivo.vr_guard", "1.2.0", "vivo_vr_guard.so"),
        )
        assertTrue(PluginPaths.isSafeModulePath("vivo.vr_guard/1.2.0/vivo_vr_guard.so"))
    }

    @Test
    fun `absolute, escaping and over-long paths are rejected`() {
        assertFalse(PluginPaths.isSafeModulePath("/vivo.vr_guard/1.2.0/a.so"))
        assertFalse(PluginPaths.isSafeModulePath("vivo.vr_guard/../a.so"))
        assertFalse(PluginPaths.isSafeModulePath("../../etc/passwd"))
        assertFalse(PluginPaths.isSafeModulePath("countermeasures/a/1/a.so"))
        assertFalse(PluginPaths.isSafeModulePath("a/1/not-a-so"))
        assertFalse(PluginPaths.isSafeModulePath(""))
    }

    @Test
    fun `ids, versions and file names are bounded and separator-free`() {
        assertTrue(PluginPaths.isValidId("vivo.vr_guard"))
        assertFalse(PluginPaths.isValidId("Vivo"))
        assertFalse(PluginPaths.isValidId("../etc"))
        assertFalse(PluginPaths.isValidId(""))
        assertTrue(PluginPaths.isValidVersion("1.2.0-rc1"))
        assertFalse(PluginPaths.isValidVersion("1/2"))
        assertTrue(PluginPaths.isValidFileName("vivo_vr_guard.so"))
        assertFalse(PluginPaths.isValidFileName("guard.so.exe"))
        assertThrows(IllegalArgumentException::class.java) {
            PluginPaths.modulePath("../evil", "1", "a.so")
        }
    }
}
