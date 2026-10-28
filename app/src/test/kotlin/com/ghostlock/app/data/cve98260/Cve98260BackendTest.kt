package com.ghostlock.app.data.cve98260

import com.ghostlock.app.backend.cve98260.markers.Cve98260Markers
import com.ghostlock.app.backend.cve98260.profile.Cve98260Params
import com.ghostlock.app.backend.cve98260.profile.Cve98260Profile
import com.ghostlock.app.backend.cve98260.profile.Cve98260ProfileException
import com.ghostlock.app.core.preflight.Cve98260Preflight
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * B2 单测：98260 profile/参数编解码/marker/预检 + 跨端参数顺序对拍 + JNI 静态守卫。
 *
 * 这些是**离线白盒**用例；不触 Android/JNI/真机。真机门禁 G0–G13 仍 pending。
 */
class Cve98260BackendTest {
    /** 合法 HOCON（与 debug 资产同结构；数值为上游候选取值）。 */
    private fun validProfileText(): String = """
        release = "6.12.69-android16-6-g242e1a07f878-ab15865446-4k"
        kernel_major = 6
        page_size = 4096
        cpu = 2
        slab_order = 1
        sockbuf = 8192
        slide_max = "0x40000000"
        base_min = "0xffffff8000000000"
        base_max = "0xffffffc000000000"
        static {
          schema_version = 1
          timer_site = "0xffffffc0803718f4"
          timer_site_low21 = "0x1718f4"
          copy_process_low21 = "0xe94a4"
        }
        sizes { timer_alloc = 0x100, cred_req = 0xb8, cred_alloc = 0xc0, task_alloc_bytes = 0x1440 }
        offsets { cred_uid = 0x8, task_cred = 0x900, selinux_blob = 0x8 }
        symbols {
          selinux_state = "0xffffffc082888890"
          panic_on_warn = "0xffffffc0825dade0"
          root_user = "0xffffffc082602170"
          init_user_ns = "0xffffffc082601ef8"
          init_ucounts = "0xffffffc082603b08"
          init_groups = "0xffffffc082603638"
        }
        caps { fullcap = "0x1ffffffffff", selinux_sid_kernel = 1 }
        ksu {
          manager_package = "me.weishu.kernelsu"
          kmi = "android16-6.12"
          late_load_args = ["late-load", "--kmi", "android16-6.12", "--allow-shell",
                            "--package-name", "me.weishu.kernelsu"]
          ksud_fallbacks = ["/data/local/tmp/ksud", "/data/adb/ksu/bin/ksud"]
        }
    """.trimIndent()

    @Test
    fun profileParsesAndMatchesDevice() {
        val profile = Cve98260ProfileConfig.parse(validProfileText())
        assertEquals(Cve98260Profile.PARAM_COUNT, profile.numeric.size)
        assertTrue(profile.matchesDevice(profile.release))
        assertFalse(profile.matchesDevice("6.12.70-other"))
        assertTrue(profile.lateLoadArgs.contains("--allow-shell"))
    }

    @Test
    fun profileMissingKeyIsNamedFailure() {
        val text = validProfileText().replace("page_size = 4096", "")
        try {
            Cve98260ProfileConfig.parse(text)
            error("expected exception")
        } catch (problem: Cve98260ProfileException) {
            assertTrue(problem.reason.startsWith("cve98260-profile:"))
        }
    }

    @Test
    fun profileBaseWindowOrderIsRejected() {
        val text = validProfileText()
            .replace("base_min = \"0xffffff8000000000\"", "base_min = \"0xffffffd000000000\"")
        try {
            Cve98260ProfileConfig.parse(text)
            error("expected exception")
        } catch (problem: Cve98260ProfileException) {
            assertEquals("cve98260-profile:base-window", problem.reason)
        }
    }

    @Test
    fun profileLow21MismatchIsRejected() {
        val text = validProfileText().replace("timer_site_low21 = \"0x1718f4\"", "timer_site_low21 = \"0x1718f5\"")
        try {
            Cve98260ProfileConfig.parse(text)
            error("expected exception")
        } catch (problem: Cve98260ProfileException) {
            assertEquals("cve98260-profile:timer-site", problem.reason)
        }
    }

    @Test
    fun paramArgRoundTripsThroughServiceDecoder() {
        val profile = Cve98260ProfileConfig.parse(validProfileText())
        val decoded = Cve98260Params.decode(profile.toParamArg())
        assertEquals(profile.numeric, decoded.numeric)
        assertEquals(profile.managerPackage, decoded.managerPackage)
        assertEquals(profile.lateLoadArgs, decoded.lateLoadArgs)
        assertEquals(profile.ksudFallbacks, decoded.ksudFallbacks)
    }

    @Test
    fun paramDecoderRejectsMissingAllowShell() {
        val profile = Cve98260ProfileConfig.parse(validProfileText())
        val arg = profile.toParamArg().replace("--allow-shell:", "--shell:")
        try {
            Cve98260Params.decode(arg)
            error("expected exception")
        } catch (problem: Cve98260ProfileException) {
            assertEquals("cve98260-params:missing-allow-shell", problem.reason)
        }
    }

    @Test
    fun markersFormStableSnapshot() {
        val lines = listOf(
            "GLK98260_ENV pid=123 uid=2000 domain=u:r:shell:s0 release=6.12.69-x",
            "GLK98260_PREFLIGHT=ok",
            "GLK98260_ARMED=1",
            "GLK98260_SELINUX_OFF=1",
            "GLK98260_PANIC_ON_WARN=0",
            "GLK98260_FAKE_CRED=1",
            "GLK98260_FAKE_UID=1",
            "KSU_LOADED",
        )
        val snapshot = Cve98260Markers.parse(lines)
        assertTrue(snapshot.envPresent)
        assertEquals(123, snapshot.envPid)
        assertEquals(2000, snapshot.envUid)
        assertEquals("u:r:shell:s0", snapshot.envDomain)
        assertTrue(snapshot.preflightOk)
        assertTrue(snapshot.armed)
        assertTrue(snapshot.ksuLoaded)
        assertTrue(snapshot.helperSucceeded())
        assertFalse(snapshot.mustReboot())
    }

    @Test
    fun markersParkRequiresReboot() {
        val snapshot = Cve98260Markers.parse(
            listOf("GLK98260_ARMED=1", "GLK98260_PARKED=fake cred page held"),
        )
        assertTrue(snapshot.mustReboot())
        assertTrue(Cve98260Markers.requiresReboot(snapshot, exitCode = null))
    }

    @Test
    fun markersHalfLineIsIgnored() {
        val snapshot = Cve98260Markers.parse(listOf("GLK98260_ARM", "garbage line"))
        assertFalse(snapshot.armed)
        assertFalse(snapshot.envPresent)
    }

    @Test
    fun appPreflightReportsAllReasons() {
        val result = Cve98260Preflight.evaluateApp(
            Cve98260Preflight.AppFacts(
                profileParsed = false,
                releaseMatchesDevice = false,
                payloadReady = false,
                storeBlocked = true,
                bootIdKnown = false,
            ),
        )
        assertFalse(result.ok)
        assertTrue(result.reasons.contains("cve98260:preflight:profile-missing"))
        assertTrue(result.reasons.contains("cve98260:preflight:must-reboot"))
    }

    @Test
    fun journalInstalledAllowsReinstallButLaunchedBlocks() {
        fun facts(state: Cve98260Preflight.JournalState): Cve98260Preflight.ServiceFacts =
            Cve98260Preflight.ServiceFacts(
                selfUid = 2000,
                selfDomain = "u:r:shell:s0",
                tracefsWritable = true,
                slabOrderActual = 1,
                slabOrderExpected = 1,
                pageSizeActual = 4096,
                pageSizeExpected = 4096,
                cpuCount = 8,
                cpuExpected = 2,
                ksuLoaded = false,
                managerLibksudPresent = true,
                bootId = "boot-a",
                journalState = state,
                journalBootId = "boot-a",
                currentBootId = "boot-a",
                appStoreBlocked = false,
            )
        assertFalse(Cve98260Preflight.evaluateService(facts(Cve98260Preflight.JournalState.INSTALLED)).reasons
            .contains("cve98260:preflight:must-reboot"))
        assertTrue(Cve98260Preflight.evaluateService(facts(Cve98260Preflight.JournalState.LAUNCHED)).reasons
            .contains("cve98260:preflight:must-reboot"))
        assertFalse(Cve98260Preflight.evaluateService(
            facts(Cve98260Preflight.JournalState.LAUNCHED).copy(
                journalBootId = "boot-old", bootId = "boot-a", currentBootId = "boot-a",
            ),
        ).reasons.contains("cve98260:preflight:must-reboot"))
    }

    @Test
    fun kotlinParamOrderMatchesCHeaderAuthority() {
        val header = readRepoFile("app/src/main/cpp/gl98260/params.h")
        val structStart = header.indexOf("struct glk98260_params {")
        assertTrue("params.h must define struct glk98260_params", structStart >= 0)
        val structEnd = header.indexOf("};", structStart)
        val block = header.substring(structStart, structEnd)
        val fields = Regex("unsigned long long (\\w+);").findAll(block)
            .map { it.groupValues[1] }.toList()
        assertEquals(Cve98260Profile.PARAM_NAMES, fields)
        assertTrue(header.contains("#define GLK98260_PARAM_COUNT 26U"))
    }

    @Test
    fun jniMechanicalPlaneHasNoKillSignalOrFork() {
        val source = readRepoFile("app/src/main/cpp/gl98260/jni98260.c")
        /* 静态守卫只审代码（注释里的禁项说明不算调用）。 */
        val code = source
            .replace(Regex("/\\*.*?\\*/", RegexOption.DOT_MATCHES_ALL), "")
            .replace(Regex("//[^\n]*"), "")
        for (forbidden in listOf("kill(", "signal(", "fork(", "system(", "execve(")) {
            assertFalse("jni98260.c must not call $forbidden", code.contains(forbidden))
        }
        assertTrue(source.contains("POSIX_SPAWN_SETSID"))
        assertTrue(source.contains("spawnStatic"))
        assertTrue(source.contains("waitExit"))
    }

    private fun readRepoFile(relative: String): String {
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("not found: $relative")
    }
}
