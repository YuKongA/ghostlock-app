package com.ghostlock.app.backend.cve43284glued

import android.content.pm.PackageManager
import android.os.Bundle
import android.os.SystemClock
import android.system.Os
import android.system.OsConstants
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.core.ExecutableHostProtocol
import com.ghostlock.app.backend.core.ExecutableRunFileStager
import com.ghostlock.app.shizuku.ShizukuBackendClient
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku
import java.io.ByteArrayOutputStream
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.TimeUnit

/**
 * Exercises the approved 43284 M0 marker-only ELF through the two existing launchers.
 *
 * @note This test bypasses production Flow dispatch and never opens or executes Payload/Finish.
 */
@RunWith(AndroidJUnit4::class)
class M0MarkerGateTest {
    /** Runs the fixture ELF as the app UID and retains no run state after verified worker exit. */
    @Test
    fun appLauncherCompletesRootReadyAndSingleReleaseMarkers() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue("M0 fixture build flag is absent", BuildConfig.M0_MARKER)
        val runDirectory = createAppRunDirectory(context.cacheDir)
        val binary = packagedBinary(context)
        val worker = File(runDirectory, "m0-worker")
        val payload = File(runDirectory, "payload")
        val finish = File(runDirectory, "finish.sh")
        val sentinel = File(runDirectory, "m0-execution-sentinel")
        val canary = "#!/system/bin/sh\nprintf executed > ./m0-execution-sentinel\n".toByteArray()
        val payloadHash = sha256(canary)
        val finishHash = sha256(canary)
        var runtime: AndroidIpSecRuntime? = null
        var safeToDelete = false

        try {
            ExecutableRunFileStager.stage(runDirectory, "m0-worker", binary.readBytes())
            Os.chmod(worker.absolutePath, OWNER_RWX)
            ExecutableRunFileStager.stage(runDirectory, "payload", canary)
            ExecutableRunFileStager.stage(runDirectory, "finish.sh", canary)
            runtime = AndroidIpSecRuntime.create(context)
            val command = ArrayList<String>(runtime.arguments.size + 1).apply {
                add(binary.absolutePath)
                addAll(runtime.arguments)
            }
            val logFile = File(runDirectory, "run.log")
            val child = ProcessBuilder(command)
                .directory(runDirectory)
                .redirectErrorStream(true)
                .redirectOutput(logFile)
                .apply { environment()["GHOSTLOCK_HOME"] = runDirectory.absolutePath }
                .start()
            val completed = child.waitFor(60, TimeUnit.SECONDS)
            if (!completed) {
                child.destroyForcibly()
                child.waitFor(5, TimeUnit.SECONDS)
            }
            runtime.close()
            runtime = null
            assertTrue("M0 ELF did not exit within 60 seconds; run retained", completed)
            assertEquals("M0 ELF exit", 0, child.exitValue())

            val output = logFile.readText()
            val preflightLine = output.lineSequence().firstOrNull { it.startsWith("M0_PREFLIGHT_OK=") }
            assertTrue("M0 carrier-capacity preflight evidence missing", preflightLine != null)
            val preflightEvidence = checkNotNull(preflightLine)
            assertTrue("M0 padded KO length mismatch: $preflightEvidence", preflightEvidence.contains("ko_padded=12512"))
            assertTrue("M0 tail offset mismatch: $preflightEvidence", preflightEvidence.contains("offset=12496"))
            assertTrue("M0 tail read length mismatch: $preflightEvidence", preflightEvidence.contains("returned_bytes=16"))
            assertTrue("M0 carrier identity mismatch: $preflightEvidence",
                preflightEvidence.contains("path=/vendor/lib64/libbinderdebug.so"))
            assertTrue("M0 tail fingerprint missing: $preflightEvidence",
                Regex("(?:^|\\s)tail_hex=[0-9a-f]{32}(?:\\s|$)").containsMatchIn(preflightEvidence))
            val readyFile = File(runDirectory, "ROOT_READY")
            assertTrue("ROOT_READY missing; run retained", readyFile.isFile)
            val readyText = readyFile.readText()
            val ready = readyText.lineSequence().filter { '=' in it }
                .associate { it.substringBefore('=') to it.substringAfter('=') }
            assertEquals("1", ready["version"])
            assertEquals(runDirectory.name, ready["run_id"])
            assertEquals(Os.getuid().toString(), ready["launcher_uid"])
            assertEquals("0", ready["worker_uid"])
            assertEquals("0", ready["worker_euid"])
            assertEquals("u:r:init:s0", ready["worker_context"])
            assertTrue(ready["worker_pid"].orEmpty().toInt() > 1)
            assertEquals("0700", Os.stat(runDirectory.absolutePath).st_mode.toPermissionString())
            assertEquals("0600", Os.stat(readyFile.absolutePath).st_mode.toPermissionString())
            assertEquals(Os.getuid(), Os.stat(readyFile.absolutePath).st_uid)
            assertTrue("RELEASE_FINISH was not consumed", !File(runDirectory, "RELEASE_FINISH").exists())
            assertEquals(payloadHash, sha256(payload.readBytes()))
            assertEquals(finishHash, sha256(finish.readBytes()))
            assertFalse("payload/finish sentinel was created", sentinel.exists())
            assertTrue("M0 release evidence missing: $output", output.contains("M0_RELEASE_FINISH_CONSUMED=1"))
            assertTrue("M0 worker-exit evidence missing: $output", output.contains("M0_WORKER_EXITED=1"))
            assertTrue("SELinux mutation evidence missing: $output", output.contains("M0_SELINUX_CHANGED=0"))
            assertFalse("root worker PID remains visible", File("/proc/${ready["worker_pid"]}").exists())
            safeToDelete = true
            Log.i(
                TAG,
                "route=APP run_dir=${runDirectory.absolutePath} launcher_uid=${Os.getuid()} " +
                    "worker_pid=${ready["worker_pid"]} worker_uid=0 worker_context=${ready["worker_context"]} " +
                    "run_dir_context=${ready["run_dir_context"]} ready_sha256=${sha256(readyText.toByteArray())} " +
                    "preflight=${preflightLine} payload_sha256=$payloadHash finish_sha256=$finishHash release_consumed=true worker_exited=true " +
                    "host_exit=${child.exitValue()} canary_unchanged=true sentinel_absent=true",
            )
        } finally {
            runCatching { runtime?.close() }
            if (safeToDelete) {
                assertTrue("exact App M0 run directory cleanup failed", runDirectory.deleteRecursively())
                assertFalse("exact App M0 run directory remains", runDirectory.exists())
            } else {
                Log.e(TAG, "M0_RUN_DIR_PRESERVED=${runDirectory.absolutePath}")
            }
        }
    }

    /**
     * Runs through the common Shizuku UserService and leaves its exact runDir for host audit.
     *
     * @note Mechanical change: JUnit requires this blocking test method to return JVM void.
     */
    @Test
    fun shizukuLauncherCompletesRootReadyAndSingleReleaseMarkers() {
        runBlocking {
            val context = InstrumentationRegistry.getInstrumentation().targetContext
            assertTrue("M0 fixture build flag is absent", BuildConfig.M0_MARKER)
            val binderReachable = runCatching { Shizuku.pingBinder() }.getOrDefault(false)
            val permissionGranted = binderReachable && runCatching {
                Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
            }.getOrDefault(false)
            val reasons = ShizukuBackendClient(
                context,
                ShizukuBackendHosts.CVE_43284_GLUED,
            ).availabilityReasons()
            assertTrue("Shizuku preflight failed: $reasons", binderReachable && permissionGranted && reasons.isEmpty())
            assertEquals("Shizuku UID", 2000, Shizuku.getUid())
            assertTrue("Shizuku context is not shell: ${Shizuku.getSELinuxContext()}",
                Shizuku.getSELinuxContext()?.contains("u:r:shell:s0") == true)

            val binary = packagedBinary(context)
            val canary = "#!/system/bin/sh\nprintf executed > ./m0-execution-sentinel\n".toByteArray()
            val client = ShizukuBackendClient(context, ShizukuBackendHosts.CVE_43284_GLUED)
            val config = Bundle().apply {
                putStringArrayList(ExecutableHostProtocol.KEY_ARGUMENTS, arrayListOf())
                putStringArrayList(ExecutableHostProtocol.KEY_FILE_NAMES, arrayListOf("payload", "finish.sh"))
                putByteArray(ExecutableHostProtocol.fileKey("payload"), canary)
                putByteArray(ExecutableHostProtocol.fileKey("finish.sh"), canary)
            }
            var runDirectory = ""
            var disconnected = false
            try {
                assertEquals("Shizuku host prepare", 0, client.prepare(config, ByteArray(0)) {})
                val hostPid = client.start()
                assertTrue("Shizuku host did not return child PID: $hostPid", hostPid > 0)
                val output = ByteArrayOutputStream()
                var offset = 0L
                val deadline = SystemClock.elapsedRealtime() + 75_000L
                var state = ExecutableHostProtocol.STATE_RUNNING
                while (state == ExecutableHostProtocol.STATE_RUNNING && SystemClock.elapsedRealtime() < deadline) {
                    val chunk = client.readLog(offset, 32 * 1024)
                    if (chunk.isNotEmpty()) {
                        output.write(chunk)
                        offset += chunk.size.toLong()
                    }
                    state = client.state()
                    if (state == ExecutableHostProtocol.STATE_RUNNING) delay(200L)
                }
                assertEquals("M0 host terminal state", ExecutableHostProtocol.STATE_TERMINAL, state)
                val finalChunk = client.readLog(offset, 32 * 1024)
                if (finalChunk.isNotEmpty()) output.write(finalChunk)
                val hostEvidence = String(client.verify(), Charsets.UTF_8)
                runDirectory = Regex("(?m)^run_dir=(.+)$").find(hostEvidence)?.groupValues?.get(1).orEmpty()
                assertTrue("Shizuku run directory missing: $hostEvidence", runDirectory.isNotBlank())
                val log = output.toString(Charsets.UTF_8.name())
                assertTrue("M0 ROOT_READY evidence missing: $log", log.contains("M0_ROOT_READY=run:"))
                assertTrue("M0 release evidence missing: $log", log.contains("M0_RELEASE_FINISH_CONSUMED=1"))
                assertTrue("M0 worker exit evidence missing: $log", log.contains("M0_WORKER_EXITED=1"))
                assertTrue("SELinux mutation evidence missing: $log", log.contains("M0_SELINUX_CHANGED=0"))
                assertTrue("M0 host exit is not zero: $hostEvidence", hostEvidence.contains("exit=0"))
                assertTrue("Shizuku run id is not 32 lowercase hex: $runDirectory",
                    runDirectory.substringAfterLast('/').matches(Regex("[0-9a-f]{32}")))
                Log.i(
                    TAG,
                    "route=SHIZUKU launcher_uid=${Shizuku.getUid()} launcher_context=${Shizuku.getSELinuxContext()} " +
                        "run_dir=$runDirectory elf_sha256=${sha256(binary.readBytes())} host_pid=$hostPid " +
                        "host_evidence=${hostEvidence.replace('\n', '|')} log=${log.replace('\n', '|')} " +
                        "canary_sha256=${sha256(canary)} release_consumed=true worker_exited=true",
                )
            } finally {
                client.disconnect(remove = true)
                disconnected = true
                if (runDirectory.isNotBlank()) {
                    Log.i(TAG, "M0_SHIZUKU_RUN_DIR_RETAINED_FOR_AUDIT=$runDirectory")
                }
                assertTrue("Shizuku UserService removal callback did not complete", disconnected)
            }
        }
    }

    /** Creates one fresh app-owned 32-hex run directory with owner-only permissions. */
    private fun createAppRunDirectory(cacheDirectory: File): File {
        val root = File(cacheDirectory, "m0-marker")
        check(root.isDirectory || root.mkdirs()) { "M0 app run root unavailable" }
        Os.chmod(root.absolutePath, OWNER_RWX)
        val runDirectory = File(root, UUID.randomUUID().toString().replace("-", ""))
        check(runDirectory.mkdir()) { "M0 app run directory unavailable" }
        Os.chmod(runDirectory.absolutePath, OWNER_RWX)
        return runDirectory
    }

    /** Resolves the single packaged 43284 executable and rejects a missing/non-executable file. */
    private fun packagedBinary(context: android.content.Context): File =
        File(context.applicationInfo.nativeLibraryDir, "lib43284Glued.so").also {
            check(it.isFile && it.canExecute()) { "M0 packaged 43284 ELF unavailable" }
        }

    /** Computes SHA-256 using an in-memory digest and emits lowercase hexadecimal. */
    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes).joinToString("") { "%02x".format(it.toInt() and 0xff) }

    /** Formats the low permission bits as four-digit octal for stable marker evidence. */
    private fun Int.toPermissionString(): String =
        "0" + ((this and 0x1ff).toString(8).padStart(3, '0'))

    private companion object {
        private const val TAG = "M0MarkerGate"
        private val OWNER_RWX = OsConstants.S_IRUSR or OsConstants.S_IWUSR or OsConstants.S_IXUSR
    }
}
