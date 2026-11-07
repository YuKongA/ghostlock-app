package com.ghostlock.app.data.diagnostics

import android.content.Context
import android.os.Build
import android.system.Os
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.domain.diagnostics.DiagnosticReportPort
import com.ghostlock.app.data.run.copyLogPrefix
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.LogFilePrefix
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.initialLogRecords
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.StageResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.apache.commons.compress.archivers.tar.TarArchiveEntry
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream
import org.apache.commons.compress.compressors.gzip.GzipCompressorOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.PrintWriter
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.util.concurrent.TimeUnit

/**
 * Android adapter that packages GhostLock runtime and non-privileged system diagnostics.
 *
 * @param context Application-capable context used for cache ownership.
 * @param runState Typed application-scoped session snapshot supplier.
 * @param runLog Exact flushed GLLOG2 private-file prefix supplier owned by the sole log hub.
 * @note Collection does not invoke privileged shells or read manager-private paths and never mutates attack state.
 */
class AndroidDiagnosticReportBuilder(
    context: Context,
    private val runState: () -> RunSessionState,
    private val runLog: suspend () -> LogFilePrefix?,
    private val formatter: LogDocumentFormatter,
) : DiagnosticReportPort {
    private val applicationContext = context.applicationContext

    /** Creates the report on the IO dispatcher and returns its cache artifact. */
    override suspend fun create(): File = withContext(Dispatchers.IO) {
        createBlocking()
    }

    /** Builds one deterministic-layout report directory and archives it as tar.gz. */
    private suspend fun createBlocking(): File {
        val timestamp = LocalDateTime.now().format(REPORT_TIME_FORMAT)
        val reportDirectory = File(applicationContext.cacheDir, "diagnostic-report-$timestamp")
        if (reportDirectory.exists()) reportDirectory.deleteRecursively()
        check(reportDirectory.mkdirs()) { "cannot create diagnostic report directory" }

        writeBasicInfo(File(reportDirectory, "basic.txt"))
        writeRunReport(File(reportDirectory, "run-report.txt"), runState())
        writeRuntimeLog(File(reportDirectory, "ghostlock-run.log"))
        collectLogcat(File(reportDirectory, "logcat.txt"))
        PROC_FILES.forEach { source ->
            copyReadableFile(File(source), File(reportDirectory, source.removePrefix("/").replace('/', '_')))
        }

        val target = File(applicationContext.cacheDir, "GhostLock_diagnostic_$timestamp.tar.gz")
        if (target.exists()) check(target.delete()) { "cannot replace diagnostic report" }
        archive(reportDirectory, target)
        reportDirectory.deleteRecursively()
        return target
    }

    /** Writes the typed session and six-layer report without interpreting terminal text. */
    private fun writeRunReport(target: File, session: RunSessionState) {
        PrintWriter(target).use { writer ->
            writer.field("BackendKey", session.backendKey ?: "NONE")
            writer.field("BackendLabel", session.backendLabel)
            writer.field("SessionPhase", session.phase.name)
            writer.field("StartAllowed", session.startAllowed.toString())
            val report = session.report
            writer.field("ReportPresent", (report != null).toString())
            if (report != null) writer.writeReport(report)
        }
    }

    /** Writes one immutable six-layer report in a line-oriented diagnostic format. */
    private fun PrintWriter.writeReport(report: RunReport) {
        field("RunCapability", report.runCapability.name)
        field("DiagnosticOutcome", report.diagnosticOutcome.name)
        stage("BackendPrimitive", report.backendPrimitive)
        stage("RootScriptDispatch", report.rootScriptDispatch)
        report.payloadItems.forEachIndexed { index, item ->
            field("Payload.$index.ItemId", item.itemId)
            field("Payload.$index.State", item.state.name)
            field("Payload.$index.Reason", item.reason.name)
            field("Payload.$index.ExitKind", item.exitKind.name)
            field("Payload.$index.ExitValue", item.exitValue.toString())
        }
        stage("PayloadOverall", report.payloadOverall)
        report.finishItems.forEachIndexed { index, item ->
            field("Finish.$index.ItemId", item.itemId)
            field("Finish.$index.State", item.state.name)
            field("Finish.$index.Reason", item.reason.name)
            field("Finish.$index.ExitKind", item.exitKind.name)
            field("Finish.$index.ExitValue", item.exitValue.toString())
        }
        stage("FinishOverall", report.finishOverall)
        stage("BackendCleanup", report.backendCleanup)
        field("Terminal", report.terminal.name)
        field("ProductSuccess", report.isProductSuccess.toString())
    }

    /** Writes one complete typed stage result. */
    private fun PrintWriter.stage(prefix: String, result: StageResult) {
        field("$prefix.State", result.state.name)
        field("$prefix.Reason", result.reason.name)
        field("$prefix.ExitKind", result.exitKind.name)
        field("$prefix.ExitValue", result.exitValue.toString())
    }

    /** Writes one single-line field while preventing diagnostic line injection. */
    private fun PrintWriter.field(name: String, value: String) {
        val oneLine = value.map { character ->
            if (character == '\r' || character == '\n' || character == '\t') ' ' else character
        }.joinToString("")
        println("$name: $oneLine")
    }

    /** Writes stable app, Android, kernel, and SELinux identity without manager-specific fields. */
    private fun writeBasicInfo(target: File) {
        PrintWriter(target).use { writer ->
            writer.println("ApplicationId: ${BuildConfig.APPLICATION_ID}")
            writer.println("VersionName: ${BuildConfig.VERSION_NAME}")
            writer.println("VersionCode: ${BuildConfig.VERSION_CODE}")
            writer.println("Brand: ${Build.BRAND}")
            writer.println("Model: ${Build.MODEL}")
            writer.println("Product: ${Build.PRODUCT}")
            writer.println("Manufacturer: ${Build.MANUFACTURER}")
            writer.println("SDK: ${Build.VERSION.SDK_INT}")
            writer.println("Fingerprint: ${Build.FINGERPRINT}")
            writer.println("Device: ${Build.DEVICE}")
            val selinuxDirectory = File("/sys/fs/selinux")
            val enforceValue = runCatching { File(selinuxDirectory, "enforce").readText().trim() }
                .getOrDefault("unknown")
            writer.println("SELinuxEnabled: ${selinuxDirectory.isDirectory}")
            writer.println("SELinuxEnforceValue: $enforceValue")
            val uname = Os.uname()
            writer.println("KernelRelease: ${uname.release}")
            writer.println("KernelVersion: ${uname.version}")
            writer.println("Machine: ${uname.machine}")
            writer.println("Nodename: ${uname.nodename}")
            writer.println("Sysname: ${uname.sysname}")
        }
    }

    /** Copies the same exact flushed private-file prefix used by manual sharing. */
    private suspend fun writeRuntimeLog(target: File) {
        val prefix = runLog()
        if (prefix != null) {
            copyLogPrefix(prefix, target)
        } else {
            target.outputStream().buffered().use { output ->
                formatter.formatHeader(null).forEach(output::write)
                val session = runState()
                val config = LogFormatConfig(session.showTimestamp)
                initialLogRecords(session.minimumLogLevel).forEach { record ->
                    output.write(formatter.formatRecord(record, config))
                }
                formatter.formatTrailer(session.phase).forEach(output::write)
            }
        }
    }

    /** Captures bounded logcat output without requiring elevated privileges. */
    private fun collectLogcat(target: File) {
        val process = runCatching {
            ProcessBuilder("logcat", "-b", "all", "-v", "threadtime", "-d")
                .redirectErrorStream(true)
                .redirectOutput(target)
                .start()
        }.getOrNull() ?: return
        if (!process.waitFor(LOGCAT_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            process.destroyForcibly()
        }
    }

    /** Copies a readable regular file and ignores an unavailable diagnostic source. */
    private fun copyReadableFile(source: File, target: File) {
        if (!source.isFile || !source.canRead()) return
        runCatching {
            source.inputStream().use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            }
        }
    }

    /** Archives every report file under a relative, traversal-free path. */
    private fun archive(sourceDirectory: File, target: File) {
        FileOutputStream(target).use { fileOutput ->
            GzipCompressorOutputStream(fileOutput).use { gzipOutput ->
                TarArchiveOutputStream(gzipOutput).use { tarOutput ->
                    tarOutput.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX)
                    sourceDirectory.walkTopDown().filter(File::isFile).forEach { file ->
                        val relative = file.relativeTo(sourceDirectory).invariantSeparatorsPath
                        val entry = TarArchiveEntry(file, relative)
                        tarOutput.putArchiveEntry(entry)
                        file.inputStream().use { input -> input.copyTo(tarOutput) }
                        tarOutput.closeArchiveEntry()
                    }
                    tarOutput.finish()
                }
            }
        }
    }

    private companion object {
        val REPORT_TIME_FORMAT: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd_HH_mm_ss")
        const val LOGCAT_TIMEOUT_SECONDS = 10L
        val PROC_FILES = listOf(
            "/proc/version",
            "/proc/cmdline",
            "/proc/meminfo",
            "/proc/cpuinfo",
            "/proc/modules",
            "/proc/bootconfig",
        )
    }
}
