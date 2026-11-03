package com.ghostlock.app.backend.backends.cve43499.owned

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * G07 `KernelsuGuard` 单测（task-63）：/proc/modules、/sys/module、/data/adb 三类
 * positive signal 的 fail-closed，以及全 absent/unreadable 的放行（附读数）。
 */
class KernelsuGuardTest {

    @Test
    fun `proc modules kernelsu line is loaded`() {
        val result = guard(modules = "kernelsu 123 1 - Live 0x0\nzzz 1 0 - Live 0x0\n")
        assertTrue(result is KernelsuGuardResult.Loaded)
        assertEquals("proc-modules", (result as KernelsuGuardResult.Loaded).signal)
        assertEquals("loaded", result.evidence["procModules"])
    }

    @Test
    fun `ksu sys module is loaded`() {
        val result = guard(sysModules = setOf("ksu"))
        assertTrue(result is KernelsuGuardResult.Loaded)
        assertEquals("sys-module", (result as KernelsuGuardResult.Loaded).signal)
        assertEquals("loaded", result.evidence["sysModule"])
    }

    @Test
    fun `data adb ksu path is loaded`() {
        val result = guard(paths = setOf("/data/adb/ksud"))
        assertTrue(result is KernelsuGuardResult.Loaded)
        assertEquals("adb-paths", (result as KernelsuGuardResult.Loaded).signal)
        assertTrue(result.evidence["adbPaths"]?.contains("/data/adb/ksud") == true)
    }

    @Test
    fun `multiple signals join in stable order`() {
        val result = guard(
            modules = "kernelsu 1 1 - Live 0x0\n",
            sysModules = setOf("kernelsu"),
            paths = setOf("/data/adb/ksu", "/data/adb/ksud"),
        )
        assertTrue(result is KernelsuGuardResult.Loaded)
        assertEquals(
            "proc-modules+sys-module+adb-paths",
            (result as KernelsuGuardResult.Loaded).signal,
        )
    }

    @Test
    fun `clean device is absent with evidence`() {
        val result = guard(modules = "module_a 1 0 - Live 0x0\n")
        assertTrue(result is KernelsuGuardResult.Absent)
        assertEquals(
            "absent",
            (result as KernelsuGuardResult.Absent).evidence["procModules"],
        )
    }

    @Test
    fun `unreadable proc modules alone is not loaded`() {
        /* app 域 Enforcing 下 /proc/modules 常不可读：只记录 unreadable，不判 loaded。 */
        val result = guard(modules = null)
        assertTrue(result is KernelsuGuardResult.Absent)
        assertEquals(
            "unreadable",
            (result as KernelsuGuardResult.Absent).evidence["procModules"],
        )
    }

    /**
     * @param modules /proc/modules 文本（null=不可读）。
     * @param sysModules 存在的 /sys/module 名集合。
     * @param paths 存在的 /data/adb 路径集合。
     * @return guard 结果。
     */
    private fun guard(
        modules: String? = "",
        sysModules: Set<String> = emptySet(),
        paths: Set<String> = emptySet(),
    ): KernelsuGuardResult = KernelsuGuard(
        procModulesReader = { modules },
        sysModuleExists = { name -> sysModules.contains(name) },
        pathExists = { path -> paths.contains(path) },
    ).check()
}
