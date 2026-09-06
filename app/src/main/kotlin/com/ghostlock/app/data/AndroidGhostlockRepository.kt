package com.ghostlock.app.data

import android.annotation.SuppressLint
import android.content.ContentValues
import android.content.Context
import android.os.Build
import android.provider.MediaStore
import android.system.Os
import androidx.core.content.edit
import androidx.core.net.toUri
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.KernelOffsets
import com.ghostlock.app.domain.model.KernelSnapshot
import com.ghostlock.app.domain.model.OffsetCandidate
import com.ghostlock.app.domain.model.OffsetImportResult
import com.ghostlock.app.domain.model.ParseResult
import com.ghostlock.app.domain.model.SupportedKernels
import com.ghostlock.app.domain.repository.GhostlockRepository
import com.ghostlock.app.domain.usecase.OffsetMatching
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import org.json.JSONTokener
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.charset.StandardCharsets
import java.util.Locale
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong

/** Android implementation of the domain repository. All platform I/O lives here. */
class AndroidGhostlockRepository(context: Context) : GhostlockRepository {
    private companion object {
        const val OffsetsFileName = "offsets.json"
        const val KsuLogName = ".ghostlock_ksu.log"
        const val ExtractBinaryName = "libextract.so"
    }

    private val appContext = context.applicationContext
    private val filesDir: File = appContext.filesDir
    private val offsetsFile get() = File(filesDir, OffsetsFileName)
    private val cpuPairs = mutableListOf<CpuPair>()
    private val cpuPairLabels = mutableListOf<String>()
    private var selectedCpuPair = 0
    private var pendingParsedEntries: JSONArray? = null

    init {
        buildCpuPairs()
        restoreCpuPair()
    }

    override suspend fun snapshot(): KernelSnapshot = KernelSnapshot(
        deviceName = resolveDeviceName(),
        kernelRelease = System.getProperty("os.version", "unknown").orEmpty(),
        socName = resolveSocName(),
        kernelSupported = isKernelSupported(),
        cpuPairs = cpuPairs.toList(),
        cpuPairLabels = cpuPairLabels.toList(),
        selectedCpuPair = selectedCpuPair,
    )

    override fun selectCpuPair(index: Int) {
        if (index !in cpuPairs.indices) return
        selectedCpuPair = index
        appContext.getSharedPreferences("ghostlock_prefs", Context.MODE_PRIVATE)
            .edit {
                putString("cpu_pair", cpuPairs[index].toString())
            }
    }

    override suspend fun exportCandidates(): List<OffsetCandidate> {
        val entries = readOffsetsFile(offsetsFile) ?: return emptyList()
        val current = System.getProperty("os.version", "")
        return (0 until entries.length()).asSequence()
            .mapNotNull { entries.optJSONObject(it) }
            .map { entry: JSONObject -> entry.optString("release", "") to entry }
            .filter { (release, entry) ->
                release.isNotEmpty() &&
                        !(SupportedKernels.BUILTIN.containsKey(release) && matchesBuiltin(entry))
            }
            .distinctBy { it.first }
            .sortedWith(compareBy<Pair<String, JSONObject>> { if (it.first == current) 0 else 1 }.thenBy { it.first })
            .map { (release, entry) -> OffsetCandidate(release, entry.toString(2)) }
            .toList()
    }

    override suspend fun importOffsets(json: String): OffsetImportResult = mergeImported(json, overwrite = false)

    override suspend fun confirmImport(json: String): OffsetImportResult = mergeImported(json, overwrite = true)

    private fun mergeImported(json: String, overwrite: Boolean): OffsetImportResult {
        return try {
            val imported = parseEntries(json) ?: return OffsetImportResult.Failed("not a valid offsets.json")
            val existing = readOffsetsFile(offsetsFile) ?: JSONArray()
            val fresh = JSONArray()
            val skipped = mutableListOf<String>()
            val differingBuiltins = mutableListOf<String>()
            for (index in 0 until imported.length()) {
                val entry = imported.optJSONObject(index) ?: continue
                val release = entry.optString("release", "")
                if (release.isEmpty()) continue
                if (release in SupportedKernels.BUILTIN) {
                    if (matchesBuiltin(entry)) {
                        skipped += release
                    } else {
                        differingBuiltins += release
                        fresh.put(entry)
                    }
                } else {
                    fresh.put(entry)
                }
            }
            if (fresh.length() == 0) return OffsetImportResult.AlreadyPresent

            val overlaps = overlappingReleases(existing, fresh)
            val replaced = (overlaps + differingBuiltins).distinct()
            if (!overwrite && replaced.isNotEmpty()) {
                return OffsetImportResult.RequiresOverwrite(replaced)
            }
            mergeAndSave(existing, fresh, overwrite)
            OffsetImportResult.Imported(freshReleases(fresh))
        } catch (error: CancellationException) {
            throw error
        } catch (error: Exception) {
            OffsetImportResult.Failed(error.message ?: "import failed")
        }
    }

    override suspend fun parseSource(input: String, xblPath: String?, overwrite: Boolean, onLog: (String) -> Unit): ParseResult {
        val parsedFile = File(filesDir, "offsets_parse.tmp")
        return try {
            if (overwrite) {
                val pending = pendingParsedEntries
                if (pending != null) {
                    pendingParsedEntries = null
                    val existing = readOffsetsFile(offsetsFile) ?: JSONArray()
                    mergeAndSave(existing, pending, overwrite = true)
                    return ParseResult.Parsed(freshReleases(pending))
                }
            }
            val binary = File(appContext.applicationInfo.nativeLibraryDir, ExtractBinaryName)
            if (!binary.isFile) return ParseResult.Failed(1, "missing native binary: ${binary.absolutePath}")
            parsedFile.delete()
            val args = buildList {
                add(input)
                if (xblPath != null) {
                    add("--xbl-config")
                    add(xblPath)
                }
                addAll(listOf("--format", "json", "--out", parsedFile.absolutePath, "--work-dir", filesDir.absolutePath))
            }
            onLog("extract: $input")
            val code = runProcess(
                ProcessBuilder(listOf(binary.absolutePath) + args)
                    .directory(filesDir)
                    .redirectErrorStream(true)
                    .apply {
                        environment()["GHOSTLOCK_HOME"] = filesDir.absolutePath
                        environment()["TMPDIR"] = filesDir.absolutePath
                        environment()["HOME"] = filesDir.absolutePath
                    },
                onLog = onLog,
                timeoutSeconds = 1800,
            )
            onLog("extract exit code=$code")
            if (code != 0 || !parsedFile.isFile) return ParseResult.Failed(code)
            val fresh = parseEntries(parsedFile.readText()) ?: return ParseResult.Failed(code, "invalid extractor output")
            val existing = readOffsetsFile(offsetsFile) ?: JSONArray()
            val filtered = JSONArray()
            val skipped = mutableListOf<String>()
            val differingBuiltins = mutableListOf<String>()
            for (index in 0 until fresh.length()) {
                val entry = fresh.optJSONObject(index) ?: continue
                val release = entry.optString("release", "")
                if (release in SupportedKernels.BUILTIN) {
                    if (matchesBuiltin(entry)) skipped += release
                    else {
                        differingBuiltins += release
                        filtered.put(entry)
                    }
                } else {
                    filtered.put(entry)
                }
            }
            if (filtered.length() == 0) return ParseResult.AlreadyPresent
            val replaced = if (overwrite) emptyList() else differingBuiltins.distinct()
            if (replaced.isNotEmpty()) {
                pendingParsedEntries = filtered
                return ParseResult.RequiresOverwrite(replaced)
            }
            mergeAndSave(existing, filtered, overwrite = true)
            ParseResult.Parsed(freshReleases(filtered))
        } catch (error: CancellationException) {
            throw error
        } catch (error: Exception) {
            ParseResult.Failed(1, error.message)
        } finally {
            parsedFile.delete()
        }
    }

    override suspend fun runExploit(pair: CpuPair, onLog: (String) -> Unit): Int {
        val workDir = filesDir
        return try {
            val binary = File(appContext.applicationInfo.nativeLibraryDir, "libghostlock.so")
            require(binary.isFile) { "missing native binary: ${binary.absolutePath}" }
            if (prepareKsud(workDir, onLog) != null) onLog("ksud ready") else onLog("warning: ksud not found")
            val ksuLog = File(workDir, KsuLogName)
            ksuLog.delete()
            val offset = AtomicLong()
            val tailer = Thread {
                try {
                    while (!Thread.currentThread().isInterrupted) {
                        tailKsuLog(ksuLog, offset, onLog)
                        Thread.sleep(200)
                    }
                } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                }
            }.apply {
                name = "ksu-log-tailer"
                isDaemon = true
                start()
            }
            val command = ProcessBuilder(binary.absolutePath)
                .directory(workDir)
                .redirectErrorStream(true)
                .apply {
                    environment()["GHOSTLOCK_HOME"] = workDir.absolutePath
                    environment()["TMPDIR"] = workDir.absolutePath
                    environment()["HOME"] = workDir.absolutePath
                    if (pair.primary != 0 || pair.consumer != 1) {
                        environment()["GHOSTLOCK_CORE"] = pair.primary.toString()
                        environment()["GHOSTLOCK_CONSUMER_CORE"] = pair.consumer.toString()
                    }
                }
            try {
                runProcess(command, onLog = onLog)
            } finally {
                withContext(Dispatchers.IO) {
                    tailer.interrupt()
                    tailer.join(1000)
                    tailKsuLog(ksuLog, offset, onLog)
                }
            }
        } catch (error: CancellationException) {
            throw error
        } catch (error: Exception) {
            onLog("error: ${error::class.simpleName}: ${error.message}")
            1
        }
    }

    override suspend fun readDocument(uri: String): String = appContext.contentResolver
        .openInputStream(uri.toUri())
        ?.bufferedReader()
        ?.use { it.readText() }
        ?: throw IOException("cannot open $uri")

    override suspend fun cacheDocument(uri: String, fileName: String): String {
        val target = File(filesDir, fileName)
        appContext.contentResolver.openInputStream(uri.toUri())?.use { input ->
            target.outputStream().use(input::copyTo)
        } ?: throw IOException("cannot open $uri")
        return target.absolutePath
    }

    override suspend fun publishOffsets(candidate: OffsetCandidate): String {
        val safeRelease = candidate.release.replace(Regex("[^A-Za-z0-9._-]"), "_")
        val values = ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, "offsets-$safeRelease.json")
            put(MediaStore.Downloads.MIME_TYPE, "application/json")
        }
        val uri = appContext.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
            ?: throw IOException("cannot create download entry")
        appContext.contentResolver.openOutputStream(uri)?.use { output ->
            output.write("[${candidate.json}]".toByteArray(StandardCharsets.UTF_8))
        } ?: throw IOException("cannot open download entry")
        return uri.toString()
    }

    override fun close() {
        synchronized(processes) {
            processes.forEach(Process::destroyForcibly)
            processes.clear()
        }
    }

    private fun parseEntries(text: String): JSONArray? {
        if (text.isBlank()) return null
        return try {
            when (val value = JSONTokener(text).nextValue()) {
                is JSONArray -> value
                is JSONObject -> JSONArray().put(value)
                else -> null
            }
        } catch (_: Exception) {
            null
        }
    }

    private fun readOffsetsFile(file: File): JSONArray? = if (file.isFile) parseEntries(file.readText()) else null

    private fun overlappingReleases(existing: JSONArray, imported: JSONArray): List<String> {
        val known = (0 until existing.length()).mapNotNull { existing.optJSONObject(it)?.optString("release") }.toSet()
        return (0 until imported.length()).mapNotNull { imported.optJSONObject(it)?.optString("release") }
            .filter { it in known }
            .distinct()
    }

    private fun mergeAndSave(existing: JSONArray, imported: JSONArray, overwrite: Boolean) {
        val importedByRelease = (0 until imported.length()).mapNotNull { imported.optJSONObject(it) }
            .associateBy { it.optString("release", "") }
        val known = (0 until existing.length()).mapNotNull { existing.optJSONObject(it)?.optString("release") }.toSet()
        val merged = JSONArray()
        for (index in 0 until existing.length()) {
            val entry = existing.optJSONObject(index) ?: continue
            val release = entry.optString("release", "")
            if (overwrite && release in importedByRelease) continue
            merged.put(entry)
        }
        for (index in 0 until imported.length()) {
            val entry = imported.optJSONObject(index) ?: continue
            if (!overwrite && entry.optString("release", "") in known) continue
            merged.put(entry)
        }
        offsetsFile.writeText(merged.toString(2), StandardCharsets.UTF_8)
    }

    private fun freshReleases(entries: JSONArray): List<String> =
        (0 until entries.length()).mapNotNull { entries.optJSONObject(it)?.optString("release") }.distinct()

    private fun matchesBuiltin(entry: JSONObject): Boolean = OffsetMatching.matchesBuiltin(toKernelOffsets(entry), SupportedKernels.BUILTIN)

    private fun toKernelOffsets(entry: JSONObject): KernelOffsets = KernelOffsets(
        release = entry.optString("release", ""),
        scalars = scalarFields.associateWith { if (entry.has(it) && !entry.isNull(it)) entry.optLong(it) else null },
        symbols = objectFields(entry.optJSONObject("symbols")),
        structFields = objectFields(entry.optJSONObject("struct_fields")),
    )

    private fun objectFields(value: JSONObject?): Map<String, Long?> = value?.keys()?.asSequence()
        ?.associateWith { key -> if (value.isNull(key)) null else value.optLong(key) }
        ?: emptyMap()

    private val scalarFields = listOf("pselect_waiter_shift", "compact_waiter", "mm_struct_sz", "kernel_phys_load")

    private fun isKernelSupported(): Boolean {
        val version = System.getProperty("os.version", "").orEmpty()
        return version in SupportedKernels.UNAMES || importedOffsetsMatch(version)
    }

    private fun importedOffsetsMatch(version: String): Boolean {
        val entries = readOffsetsFile(offsetsFile) ?: return false
        return (0 until entries.length()).any { entries.optJSONObject(it)?.optString("release") == version }
    }

    private fun buildCpuPairs() {
        cpuPairs.clear()
        cpuPairLabels.clear()
        val online = parseCpuList(readSysFile("/sys/devices/system/cpu/online"))
        online.groupBy { readMaxFreq(it) }
            .filterKeys { it > 0 }
            .toSortedMap(compareByDescending { it })
            .forEach { (freq, cluster) ->
                cluster.sorted().chunked(2).filter { it.size == 2 }.forEach { pair ->
                    cpuPairs += CpuPair(pair[0], pair[1])
                    cpuPairLabels += "${pair[0]},${pair[1]} · ${formatFreq(freq)}"
                }
            }
        if (CpuPair(0, 1) !in cpuPairs) {
            cpuPairs += CpuPair(0, 1)
            val freq = readMaxFreq(0)
            cpuPairLabels += "0,1" + if (freq > 0) " · ${formatFreq(freq)}" else ""
        }
    }

    private fun restoreCpuPair() {
        val saved = appContext.getSharedPreferences("ghostlock_prefs", Context.MODE_PRIVATE)
            .getString("cpu_pair", null) ?: return
        val pair = saved.split(',').mapNotNull { it.trim().toIntOrNull() }
        if (pair.size == 2) cpuPairs.indexOf(CpuPair(pair[0], pair[1])).takeIf { it >= 0 }?.let { selectedCpuPair = it }
    }

    private fun parseCpuList(value: String): List<Int> = value.split(',').flatMap { part ->
        val range = part.trim().split('-').mapNotNull { it.toIntOrNull() }
        when (range.size) {
            1 -> range
            2 -> (range[0]..range[1]).toList()
            else -> emptyList()
        }
    }

    private fun readMaxFreq(cpu: Int): Long = readSysFile("/sys/devices/system/cpu/cpu$cpu/cpufreq/cpuinfo_max_freq").toLongOrNull() ?: -1L

    private fun formatFreq(khz: Long): String =
        if (khz >= 1_000_000L) "%.2f GHz".format(Locale.ROOT, khz / 1_000_000.0) else "%.0f MHz".format(Locale.ROOT, khz / 1000.0)

    private fun readSysFile(path: String): String = File(path).takeIf { it.isFile }?.useLines { it.firstOrNull()?.trim().orEmpty() } ?: ""

    @SuppressLint("PrivateApi")
    private fun systemProperty(key: String): String = try {
        val properties = Class.forName("android.os.SystemProperties")
        properties.getMethod("get", String::class.java).invoke(null, key) as? String ?: ""
    } catch (_: Throwable) {
        ""
    }

    private fun validDeviceName(value: String?): String? =
        value?.trim()?.takeIf { it.isNotEmpty() && !it.contains("unknown", true) && !it.contains("null", true) }

    private fun resolveDeviceName(): String {
        val manufacturer = Build.MANUFACTURER.orEmpty()
        val marketName = when (manufacturer.lowercase(Locale.ROOT)) {
            "xiaomi" -> firstValidProperty("ro.product.marketname")
            "oppo", "oneplus", "realme", "oplus" -> {
                val cn = Locale.getDefault().country.equals("CN", true)
                firstValidProperty(
                    *(if (cn) arrayOf(
                        "ro.vendor.oplus.market.name",
                        "ro.vendor.oplus.market.enname"
                    ) else arrayOf("ro.vendor.oplus.market.enname", "ro.vendor.oplus.market.name"))
                )
            }

            "vivo" -> firstValidProperty("ro.vivo.market.name")
            "honor", "huawei" -> firstValidProperty("ro.config.marketing_name")
            "zte", "nubia" -> firstValidProperty("ro.vendor.product.ztename")
            else -> null
        }
        return marketName ?: listOfNotNull(
            manufacturer,
            Build.BRAND.orEmpty().takeIf { !it.equals(manufacturer, true) },
            Build.MODEL.orEmpty()
        )
            .filter { it.isNotBlank() }
            .joinToString(" ")
    }

    private fun resolveSocName(): String = listOf(
        systemProperty("ro.soc.manufacturer"),
        systemProperty("ro.soc.model"),
    )
        .mapNotNull(::validDeviceName)
        .joinToString(" ")
        .ifBlank { "unknown" }

    private fun firstValidProperty(vararg keys: String): String? =
        keys.asSequence().firstNotNullOfOrNull { validDeviceName(systemProperty(it)) }

    private fun prepareKsud(workDir: File, onLog: (String) -> Unit): File? {
        val packages = listOf("me.weishu.kernelsu.pr", "me.weishu.kernelsu", "com.resukisu.resukisu", "com.kowx712.supermanager")
        var installed = false
        for (packageName in packages) {
            val appInfo = runCatching { appContext.packageManager.getApplicationInfo(packageName, 0) }.getOrNull() ?: continue
            installed = true
            val source = File(appInfo.nativeLibraryDir, "libksud.so")
            if (!source.isFile) continue
            val output = File(workDir, "ksud")
            runCatching {
                source.inputStream().use { input -> output.outputStream().use { input.copyTo(it) } }
                runCatching { Os.chmod(output.absolutePath, 448) }
                return output
            }.onFailure { onLog("copy ksud failed: ${it.message}") }
        }
        if (!installed) onLog("KernelSU/ReSukiSU/KowSU app not installed")
        return null
    }

    private suspend fun runProcess(
        builder: ProcessBuilder,
        onLog: (String) -> Unit = {},
        timeoutSeconds: Long = 300,
    ): Int = runInterruptible {
        val process = builder.start()
        synchronized(processes) { processes += process }
        val reader = Thread {
            try {
                process.inputStream.bufferedReader(StandardCharsets.UTF_8).useLines { lines -> lines.forEach(onLog) }
            } catch (_: IOException) { }
        }.apply {
            name = "process-output-reader"
            isDaemon = true
        }
        try {
            reader.start()
            val finished = process.waitFor(timeoutSeconds, TimeUnit.SECONDS)
            if (!finished) {
                process.destroy()
                if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly()
            }
            joinReader(reader)
            if (finished) process.exitValue() else -1
        } finally {
            if (process.isAlive) process.destroyForcibly()
            reader.interrupt()
            runCatching { process.inputStream.close() }
            joinReader(reader)
            synchronized(processes) { processes -= process }
        }
    }

    private fun joinReader(reader: Thread) {
        try {
            reader.join(3000)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    private fun tailKsuLog(logFile: File, offset: AtomicLong, onLog: (String) -> Unit) {
        if (!logFile.isFile) return
        synchronized(offset) {
            runCatching {
                RandomAccessFile(logFile, "r").use { file ->
                    val position = offset.get().takeIf { it <= file.length() } ?: 0L
                    file.seek(position)
                    var lastComplete = position
                    val pending = StringBuilder()
                    while (true) {
                        val byte = file.read()
                        if (byte == -1) break
                        if (byte == '\n'.code) {
                            if (pending.isNotEmpty()) onLog(pending.toString())
                            pending.clear()
                            lastComplete = file.filePointer
                        } else {
                            pending.append(byte.toChar())
                        }
                    }
                    offset.set(lastComplete)
                }
            }
        }
    }

    private val processes = mutableSetOf<Process>()

}
