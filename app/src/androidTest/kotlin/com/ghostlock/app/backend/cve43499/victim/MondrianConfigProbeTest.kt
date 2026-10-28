package com.ghostlock.app.backend.cve43499.victim

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.data.cve43499.DebugPrimitiveKind
import com.ghostlock.app.data.cve43499.DebugVictimTransport
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * E0 配置回显真机探针（task-58；mondrian 5.10 debug 配置验证）。
 *
 * 只读：记录设备身份/selected asset/解析后的关键字段/设备派生几何/E0 环境快照；
 * **不写内核、不 W1、不 perf、不 spawn helper**。E4 transport 由
 * [JniVictimMechanismProbeTest] 另行覆盖。
 *
 * @note debug androidTest；报告写入 app filesDir `mondrian-config-probe.txt` 并打 logcat。
 */
@RunWith(AndroidJUnit4::class)
class MondrianConfigProbeTest {

    @Test
    fun e0ConfigEcho() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val lines = mutableListOf<String>()

        fun record(line: String) {
            lines += line
            android.util.Log.i(TAG, line)
        }

        val report = File(context.filesDir, REPORT_FILE)
        try {
            val identity = A301SoDebugConfig.detectDeviceIdentity()
            val asset = A301SoDebugConfig.selectAssetPath(identity)
            record(
                "E0|device|device=${identity.device}|model=${identity.model}" +
                    "|fingerprint=${identity.fingerprint}|kernel_release=${identity.kernelRelease}",
            )
            record("E0|build|release=${android.os.Build.VERSION.RELEASE}|sdk=${android.os.Build.VERSION.SDK_INT}")
            record(
                "E0|env|uid=${android.os.Process.myUid()}" +
                    "|selinux_enforce=${readProc("/sys/fs/selinux/enforce")}" +
                    "|selinux_self=${readProc("/proc/self/attr/current").trimEnd('\u0000')}" +
                    "|paranoid=${readProc("/proc/sys/kernel/perf_event_paranoid")}" +
                    "|seccomp=${statusField("/proc/self/status", "Seccomp:")}" +
                    "|page_size=${android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE)}",
            )
            val modules = runCatching { File("/proc/modules").readLines() }.getOrDefault(emptyList())
            val ksuModules = modules.filter {
                it.contains("kernelsu", true) || it.contains("ksud", true)
            }
            val ksuSysModules = runCatching {
                File("/sys/module").list()?.filter {
                    it.contains("ksu", true) || it.contains("kernelsu", true)
                }.orEmpty()
            }.getOrDefault(emptyList())
            val ksuUserspace = listOf("/data/adb/ksu", "/data/adb/modules", "/data/adb/ksud")
                .filter { runCatching { File(it).exists() }.getOrDefault(false) }
            record(
                "E0|ksu|kernel_module_lines=${ksuModules.size}|sys_module_ksu=$ksuSysModules" +
                    "|userspace_paths=$ksuUserspace|modules_total=${modules.size}",
            )
            assertTrue("E0|KSU 内核模块可见，拒绝", ksuModules.isEmpty() && ksuSysModules.isEmpty())

            record("E0|selected_asset=$asset")
            assertEquals(
                "E0|mondrian 设备必须选择 debug-mondrian.conf",
                A301SoDebugConfig.ASSET_PATH_MONDRIAN,
                asset,
            )
            val debug = A301SoDebugConfig.load(context)
            assertEquals("5.10.252-gki-gabcbe4b2bc10", debug.release)
            assertEquals(5, debug.kernelMajor)
            record(
                "E0|profile|release=${debug.release}|kernel_major=${debug.kernelMajor}" +
                    "|primitive=${debug.injection.primitive}|victim=${debug.injection.victim}" +
                    "|perf_enabled=${debug.perf?.enabled}",
            )
            assertEquals(DebugPrimitiveKind.MULTICAST, debug.injection.primitive)
            assertEquals(DebugVictimTransport.PROCESS, debug.injection.victim)
            assertEquals(0, debug.perf?.enabled ?: -1)
            record(
                "E0|route|waiter_off=${debug.route.waiterOff}|buffer=${debug.route.bufferSize}" +
                    "|task_off=${debug.route.taskOffset}|lock_off=${debug.route.lockOffset}" +
                    "|compact=${debug.route.compactWaiter}",
            )
            assertEquals(96, debug.route.waiterOff)
            assertEquals(264, debug.route.bufferSize)
            assertEquals(48, debug.route.taskOffset)
            assertEquals(56, debug.route.lockOffset)
            assertEquals(1, debug.route.compactWaiter)
            record(
                "E0|task_struct|pi_lock=${debug.taskStruct.piLock}|real_cred=${debug.taskStruct.realCred}" +
                    "|cred=${debug.taskStruct.cred}|comm=${debug.taskStruct.comm}" +
                    "|tasks=${debug.taskStruct.tasks}|seccomp=${debug.taskStruct.seccomp}" +
                    "|pid=${debug.taskStruct.pid}",
            )
            assertEquals(2156L, debug.taskStruct.piLock)
            assertEquals(1912L, debug.taskStruct.realCred)
            assertEquals(1920L, debug.taskStruct.cred)
            assertEquals(1936L, debug.taskStruct.comm)
            assertEquals(1224L, debug.taskStruct.tasks)
            assertEquals(2120L, debug.taskStruct.seccomp)
            record(
                "E0|offsets|init_task=${debug.offsets.initTask}|init_cred=${debug.offsets.initCred}" +
                    "|empty_zero_page=${debug.offsets.emptyZeroPage}|enforcing=${debug.offsets.selinuxEnforcing}" +
                    "|blob_sizes=${debug.offsets.selinuxBlobSizes}" +
                    "|security_hook_heads=${debug.offsets.securityHookHeads}" +
                    "|slide_loggers=${debug.offsets.slideLoggers01}|slide_boot_id=${debug.offsets.slideBootId}",
            )
            assertEquals(40_542_656L, debug.offsets.initTask)
            assertEquals(41_846_440L, debug.offsets.selinuxEnforcing)
            record(
                "E0|cred|ref_count=${debug.cred.refCount}|ref2_offset=${debug.cred.ref2Offset}" +
                    "|ref3_offset=${debug.cred.ref3Offset}|usage_offset=${debug.cred.usageOffset}",
            )
            assertEquals(3L, debug.cred.refCount)
            assertNull(debug.cred.ref3Offset)
            record(
                "E0|kernelsnitch|collisions=${debug.kernelsnitch.collisions}" +
                    "|mm_struct_sz=${debug.kernelsnitch.mmStructSz}",
            )
            assertEquals(960L, debug.kernelsnitch.mmStructSz)
            record(
                "E0|generator|payload_lock=${debug.generator.payload.lockOff}" +
                    "|payload_fake_task=${debug.generator.payload.fakeTaskOff}" +
                    "|write_value_offset=${debug.generator.payload.writeValueOffset}" +
                    "|page_accept_bit=${debug.generator.payload.pageAcceptBit}" +
                    "|mcast_frame=${debug.generator.multicast.frameSize}",
            )
            assertEquals(3712L, debug.generator.payload.lockOff)
            assertEquals(4736L, debug.generator.payload.fakeTaskOff)
            assertEquals(256L, debug.generator.payload.writeValueOffset)
            assertEquals(16L, debug.generator.payload.pageAcceptBit)
            assertEquals(264L, debug.generator.multicast.frameSize)
            val execution = A301SoDebugConfig.executionParameters(debug)
            record(
                "E0|execution|allocation=${execution.allocationSize}|mm_struct=${execution.mmStructSize}" +
                    "|objects_per_slab=${execution.objectsPerSlab}" +
                    "|max_objects_per_slab=${execution.maxObjectsPerSlab}" +
                    "|prepare=${execution.prepareObjectCount}|spray=${execution.sprayObjectCount}",
            )
            assertEquals(960L, execution.mmStructSize)
            assertEquals(34, execution.objectsPerSlab)
            assertTrue(execution.maxObjectsPerSlab >= 34L)
            assertTrue(
                "E0|floor 几何必须满足 legacy 向量",
                34L * 960L <= execution.allocationSize &&
                    (34L + 1L) * 960L > execution.allocationSize,
            )
        } finally {
            report.writeText(lines.joinToString("\n"))
        }
    }

    /**
     * @param path /proc 文件。
     * @return trim 内容；不可读返回 "unreadable"。
     */
    private fun readProc(path: String): String =
        runCatching { File(path).readText().trim() }.getOrDefault("unreadable")

    /**
     * @param path /proc 文件。 @param prefix 字段名。 @return 字段行；不可读返回空串。
     */
    private fun statusField(path: String, prefix: String): String =
        runCatching {
            File(path).readLines().firstOrNull { it.startsWith(prefix) }.orEmpty()
        }.getOrDefault("")

    private companion object {
        const val TAG = "E0-MONDRIAN-CONFIG"
        const val REPORT_FILE = "mondrian-config-probe.txt"
    }
}
