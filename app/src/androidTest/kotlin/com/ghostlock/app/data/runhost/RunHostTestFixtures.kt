package com.ghostlock.app.data.runhost

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import android.os.ParcelFileDescriptor
import android.os.Process
import android.os.SystemClock
import android.system.Os
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.composition.backend.BackendCatalogProjection
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunInputArtifactParcel
import com.ghostlock.app.data.plugin.AndroidBuiltinPluginAssets
import com.ghostlock.app.data.plugin.AndroidPluginCatalogStore
import com.ghostlock.app.data.plugin.BuiltinPluginSeeder
import com.ghostlock.app.data.plugin.PluginObjectSource
import java.io.Closeable
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.TimeUnit
import rikka.shizuku.Shizuku
import rikka.shizuku.ShizukuRemoteProcess

/** Owned Diagnose request fixture for typed RunHost instrumentation cases. */
internal class RunHostTestFixture private constructor(
    val request: RunHostRequest,
    private val profile: File,
    private val catalogRoot: File,
) : Closeable {
    /** Closes application-owned descriptors and deletes the private source file. */
    override fun close() {
        runCatching { request.pluginArchiveFd.close() }
        request.artifacts.forEach { artifact -> runCatching { artifact.fd.close() } }
        runCatching { profile.delete() }
        runCatching {
            catalogRoot.walkTopDown().filter(File::isDirectory).forEach { directory ->
                directory.setReadable(true, true)
                directory.setWritable(true, true)
                directory.setExecutable(true, true)
            }
            catalogRoot.deleteRecursively()
        }
    }

    companion object {
        /** Creates one profile-only diagnostic request in the target app cache. */
        fun create(context: Context): RunHostTestFixture {
            val runId = UUID.randomUUID().toString().replace("-", "")
            val catalogRoot = File(context.cacheDir, "$runId-plugin-catalog")
            val store = AndroidPluginCatalogStore(catalogRoot)
            val seed = BuiltinPluginSeeder(
                assets = AndroidBuiltinPluginAssets(context.assets),
                store = store,
                apkVersionCode = 1,
            ).reconcile()
            val pluginRef = requireNotNull(
                BackendCatalogProjection.project(
                    seed.snapshot,
                    Build.VERSION.SDK_INT,
                    Process.is64Bit(),
                    Os.uname().machine,
                ).resolve("DIAGNOSE"),
            ).objectRef
            val pluginArchive = PluginObjectSource(store).open(pluginRef)
            val profile = File(context.cacheDir, "$runId-profile-v3.json")
            val bytes = "{\"backend_key\":\"DIAGNOSE\",\"schema_version\":3}\n"
                .toByteArray(Charsets.UTF_8)
            profile.writeBytes(bytes)
            val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
                .joinToString(separator = "") { byte ->
                    (byte.toInt() and 0xff).toString(16).padStart(2, '0')
                }
            val artifact = RunInputArtifactParcel().also { parcel ->
                parcel.logicalId = "profile"
                parcel.kind = RunHostArtifactKind.PROFILE.wireValue
                parcel.byteCount = bytes.size.toLong()
                parcel.sha256 = digest
                parcel.fd = ParcelFileDescriptor.open(profile, ParcelFileDescriptor.MODE_READ_ONLY)
            }
            val request = try {
                RunHostRequest().also { parcel ->
                    parcel.runId = runId
                    parcel.generation = 1L
                    parcel.backendKey = "DIAGNOSE"
                    parcel.pluginArchiveFd = pluginArchive.duplicate()
                    parcel.pluginContentSha256 = pluginRef.pluginContentSha256.value
                    parcel.profileArtifactId = "profile"
                    parcel.payloadItems = emptyList()
                    parcel.finishItems = emptyList()
                    parcel.artifacts = listOf(artifact)
                }
            } finally {
                pluginArchive.close()
            }
            return RunHostTestFixture(request, profile, catalogRoot)
        }
    }
}

/** Exact target-process mappings captured while one pluginized RunHost is still alive. */
internal data class RunHostMapsEvidence(
    val pid: Int,
    val diagnoseMapping: String,
    val jnaMapping: String,
)

/** Narrow device-test probe for the two plugin-owned DSOs and post-run PID absence. */
internal object RunHostProcessMapsProbe {
    /** Captures the exact App-UID `:flowhost` mappings for this request. */
    fun captureApp(
        context: Context,
        request: RunHostRequest,
    ): RunHostMapsEvidence {
        requireMainProcessDoesNotMapPluginLibraries()
        val expectedDirectory = appNativeDirectory(context, request)
        val processName = context.packageName + ":flowhost"
        val activityManager = context.getSystemService(ActivityManager::class.java)
        val matches = activityManager.runningAppProcesses.orEmpty()
            .filter { process -> process.processName == processName }
            .mapNotNull { process -> mapsEvidenceOrNull(process.pid, expectedDirectory) }
        require(matches.size == 1) {
            "expected one $processName mapping $expectedDirectory, found ${matches.size}"
        }
        return matches.single()
    }

    /** Captures the exact shell-UID UserService mappings for this request through Shizuku. */
    fun captureShizuku(
        context: Context,
        request: RunHostRequest,
    ): RunHostMapsEvidence {
        requireMainProcessDoesNotMapPluginLibraries()
        val expectedDirectory = shizukuNativeDirectory(context, request)
        val result = runShizuku(
            arrayOf(
                "/system/bin/sh",
                "-c",
                FIND_MAPPINGS_SCRIPT,
                "ghostlock-runhost-maps",
                expectedDirectory,
            ),
        )
        require(result.exitCode == 0) {
            "Shizuku maps probe failed: exit=${result.exitCode}; stderr=${result.stderr.take(256)}"
        }
        val lines = result.stdout.lineSequence().filter(String::isNotBlank).toList()
        require(lines.size == 3) { "Shizuku maps probe returned ${lines.size} lines" }
        val evidence = RunHostMapsEvidence(
            pid = lines[0].toInt(),
            diagnoseMapping = lines[1],
            jnaMapping = lines[2],
        )
        require(evidence.diagnoseMapping.contains("$expectedDirectory/libghostlock_diagnose.so"))
        require(evidence.jnaMapping.contains("$expectedDirectory/libjnidispatch.so"))
        return evidence
    }

    /** Waits briefly for the already-observed App-UID process to leave `/proc`. */
    fun awaitAppProcessGone(pid: Int): Boolean = awaitProcessGone {
        !File("/proc/$pid").exists()
    }

    /** Waits briefly for the already-observed shell-UID process to leave `/proc`. */
    fun awaitShizukuProcessGone(pid: Int): Boolean = awaitProcessGone {
        runCatching {
            runShizuku(
                arrayOf(
                    "/system/bin/sh",
                    "-c",
                    PID_GONE_SCRIPT,
                    "ghostlock-runhost-pid-gone",
                    pid.toString(),
                ),
            ).exitCode == 0
        }.getOrDefault(false)
    }

    /** Returns exact maps evidence only when both DSOs use this run's extracted directory. */
    private fun mapsEvidenceOrNull(
        pid: Int,
        expectedDirectory: String,
    ): RunHostMapsEvidence? = runCatching {
        val lines = File("/proc/$pid/maps").readLines()
        val diagnose = lines.firstOrNull { line ->
            line.contains("$expectedDirectory/libghostlock_diagnose.so")
        } ?: return null
        val jna = lines.firstOrNull { line ->
            line.contains("$expectedDirectory/libjnidispatch.so")
        } ?: return null
        RunHostMapsEvidence(pid, diagnose, jna)
    }.getOrNull()

    /** Enforces that instrumentation/main never became a fallback native owner. */
    private fun requireMainProcessDoesNotMapPluginLibraries() {
        val maps = File("/proc/self/maps").readText()
        require(!maps.contains("libghostlock_diagnose.so")) {
            "main process mapped libghostlock_diagnose.so"
        }
        require(!maps.contains("libjnidispatch.so")) {
            "main process mapped libjnidispatch.so"
        }
    }

    private fun appNativeDirectory(
        context: Context,
        request: RunHostRequest,
    ): String = File(
        context.noBackupFilesDir,
        "runhost/${request.runId}-${request.generation}/plugin/" +
            "00-${request.pluginContentSha256.take(CONTENT_ROOT_PREFIX_HEX)}/lib/arm64-v8a",
    ).absolutePath

    private fun shizukuNativeDirectory(
        context: Context,
        request: RunHostRequest,
    ): String = "/data/local/tmp/${context.packageName}-runhost/" +
        "${request.runId}-${request.generation}/plugin/" +
        "00-${request.pluginContentSha256.take(CONTENT_ROOT_PREFIX_HEX)}/lib/arm64-v8a"

    private inline fun awaitProcessGone(predicate: () -> Boolean): Boolean {
        val deadline = SystemClock.elapsedRealtime() + PROCESS_GONE_TIMEOUT_MS
        do {
            if (predicate()) return true
            SystemClock.sleep(PROCESS_GONE_POLL_MS)
        } while (SystemClock.elapsedRealtime() < deadline)
        return predicate()
    }

    /** Executes one bounded shell probe through the already-authorized Shizuku service. */
    private fun runShizuku(command: Array<String>): ShellProbeResult {
        val method = Shizuku::class.java.getDeclaredMethod(
            "newProcess",
            Array<String>::class.java,
            Array<String>::class.java,
            String::class.java,
        ).apply { isAccessible = true }
        val process = method.invoke(null, command, null, null) as ShizukuRemoteProcess
        check(process.waitFor(PROBE_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            "Shizuku shell probe timed out"
        }
        return ShellProbeResult(
            exitCode = process.exitValue(),
            stdout = process.inputStream.bufferedReader().use { it.readText() },
            stderr = process.errorStream.bufferedReader().use { it.readText() },
        )
    }

    private data class ShellProbeResult(
        val exitCode: Int,
        val stdout: String,
        val stderr: String,
    )

    private const val CONTENT_ROOT_PREFIX_HEX = 16
    private const val PROCESS_GONE_TIMEOUT_MS = 3_000L
    private const val PROCESS_GONE_POLL_MS = 50L
    private const val PROBE_TIMEOUT_SECONDS = 3L
    private const val PID_GONE_SCRIPT = "[ ! -e \"/proc/\$1\" ]"

    private val FIND_MAPPINGS_SCRIPT = listOf(
        "set -eu",
        "expected=\$1",
        "found=",
        "diagnose=",
        "jna=",
        "for maps in /proc/[0-9]*/maps; do",
        "  [ -r \"\$maps\" ] || continue",
        "  d=\$(grep -F -m 1 -- \"\$expected/libghostlock_diagnose.so\" \"\$maps\" 2>/dev/null) || continue",
        "  j=\$(grep -F -m 1 -- \"\$expected/libjnidispatch.so\" \"\$maps\" 2>/dev/null) || continue",
        "  [ -z \"\$found\" ] || exit 3",
        "  found=\${maps#/proc/}",
        "  found=\${found%/maps}",
        "  diagnose=\$d",
        "  jna=\$j",
        "done",
        "[ -n \"\$found\" ] || exit 4",
        "printf '%s\\n%s\\n%s\\n' \"\$found\" \"\$diagnose\" \"\$jna\"",
    ).joinToString("\n")
}
