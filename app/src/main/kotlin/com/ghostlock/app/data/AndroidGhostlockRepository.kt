package com.ghostlock.app.data

import com.ghostlock.app.BuildConfig
import android.annotation.SuppressLint
import android.content.ContentValues
import android.content.Context
import android.os.Build
import android.provider.MediaStore
import android.system.Os
import androidx.core.content.edit
import androidx.core.net.toUri
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.DebugSettings
import com.ghostlock.app.domain.model.KernelOffsets
import com.ghostlock.app.domain.model.KernelSnapshot
import com.ghostlock.app.domain.model.OffsetCandidate
import com.ghostlock.app.data.ota.OtaPayloadExtractor
import com.ghostlock.app.domain.model.OffsetImportResult
import com.ghostlock.app.domain.model.ParseResult
import com.ghostlock.app.domain.model.ProfileConfig
import com.ghostlock.app.domain.model.UserProfileFile
import com.ghostlock.app.domain.repository.GhostlockRepository
import com.ghostlock.app.domain.repository.ProfileConfigController
import com.ghostlock.app.domain.usecase.OffsetMatching
import com.ghostlock.app.shizuku.ShizukuExploitRunner
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext
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
        const val OffsetsFileName = "offsets.conf"
        const val LegacyOffsetsFileName = "offsets.json"
        const val UserProfilesDirectoryName = "user_profiles"
        const val EditSessionPreferences = "ghostlock_edit_session"
        const val ExtractBinaryName = "libextract.so"
        const val DefaultDebugLocation = "Download/ghostlock-debug-log"
        const val PrefDebugExportEnabled = "debug_export_enabled"
        const val PrefDebugExportLocation = "debug_export_location"
        const val PrefDebugKernelLogEnabled = "debug_kernel_log_enabled"
        const val PrefDebugProfileOverrides = "debug_profile_overrides"

        /* U01-S14: per-run KernelSU log name; the resolved path travels to
         * the native process via GHOSTLOCK_KSU_LOG. */
        fun ksuLogName(runStamp: Long) = "ghostlock-ksu-$runStamp.log"

        /* Every run's stdout/stderr is redirected here (export or not), so the
         * previous run can be inspected on the next start-up. */
        const val NativeLogFileName = ".ghostlock_native.log"
        const val LastRunFileName = ".ghostlock_last_run"
        const val W3SeccompFailureMarker = "W3 seccomp clear failed"
    }

    private val appContext = context.applicationContext
    private val filesDir: File = appContext.filesDir
    private val preferences get() =
        appContext.getSharedPreferences("ghostlock_prefs", Context.MODE_PRIVATE)
    private val offsetsFile get() = File(filesDir, OffsetsFileName)
    private val builtinProfiles = BuiltinProfileCatalog(appContext)
    private val assetConfigLoader = AssetConfigLoader(appContext)
    private val userProfileStore = UserProfileStore(
        directory = File(filesDir, UserProfilesDirectoryName),
        assetLoader = assetConfigLoader,
    )
    private val profileController = AndroidProfileConfigController(
        appContext,
        filesDir,
        userProfileStore,
        preferences,
    )
    private val cpuPairs = mutableListOf<CpuPair>()
    private val cpuPairLabels = mutableListOf<String>()
    private var selectedCpuPair = 0
    private var safeModeEnabled = false
    private var shizukuEnabled = false
    /** True once the user flipped the toggle; only then does it override the
     * profile suggestion (PROFILE-SUGGEST-01). */
    private var shizukuPreferenceSet = false
    private var pendingParsedDocument: PendingParsedDocument? = null
    private val shizukuRunner = ShizukuExploitRunner(appContext)

    init {
        buildCpuPairs()
        restoreCpuPair()
        restoreShizukuPreference()
        dropLegacyOffsetsCache()
        migrateLegacyOffsetsStore()
    }

    /** The old JSON offsets cache is not compatible and is discarded. */
    private fun dropLegacyOffsetsCache() {
        runCatching { File(filesDir, LegacyOffsetsFileName).delete() }
    }

    /**
     * Old builds kept imported offsets and parameter overrides in one HOCON
     * file. Split it: overrides move to preferences, the remaining entries
     * become verbatim user documents, then the legacy file is dropped.
     */
    private fun migrateLegacyOffsetsStore() {
        if (!offsetsFile.isFile) return
        runCatching {
            val entries = parseEntries(offsetsFile.readText()) ?: return@runCatching
            for (raw in entries) {
                val entry = raw.asValueMap() ?: continue
                val release = (entry["release"] as? String).orEmpty()
                if (release.isEmpty()) continue
                val overrides = valueMapOf()
                (entry.remove("execution") as? Map<*, *>)?.let { overrides["execution"] = it }
                (entry.remove("route") as? Map<*, *>)?.let { overrides["route"] = it }
                (entry.remove("fallback") as? Map<*, *>)?.let { overrides["fallback"] = it }
                if (overrides.isNotEmpty()) mergeLegacyOverrides(release, overrides)
                if (entry.size > 1) {
                    userProfileStore.save("$release.conf", HoconSupport.render(entry))
                }
            }
            offsetsFile.delete()
        }.onFailure {
            android.util.Log.e("GhostLock", "legacy offsets migration failed", it)
        }
    }

    private fun mergeLegacyOverrides(release: String, overrides: ValueMap) {
        val raw = preferences.getString(PrefDebugProfileOverrides, null)
        val all = runCatching { HoconSupport.parseValue(raw ?: "") }
            .getOrNull().asValueMap() ?: valueMapOf()
        deepMergeValues(all.mutableChild(release), overrides)
        preferences.edit { putString(PrefDebugProfileOverrides, HoconSupport.render(all)) }
    }

    override suspend fun snapshot(): KernelSnapshot {
        val release = System.getProperty("os.version", "unknown").orEmpty()
        /* PROFILE-SUGGEST-01: recommend_shizuku is a suggestion. It seeds the
         * toggle until the user makes an explicit choice, which then overrides
         * it in both directions. */
        val recommendShizuku = release in builtinProfiles.recommendShizuku ||
            importedOffsetsRecommendShizuku(release)
        val shizukuActive = if (shizukuPreferenceSet) shizukuEnabled else recommendShizuku
        return KernelSnapshot(
            deviceName = resolveDeviceName(),
            kernelRelease = release,
            socName = resolveSocName(),
            kernelSupported = isKernelSupported(),
            cpuPairs = cpuPairs.toList(),
            cpuPairLabels = cpuPairLabels.toList(),
            selectedCpuPair = selectedCpuPair,
            safeModeEnabled = safeModeEnabled,
            recommendShizuku = recommendShizuku,
            shizukuEnabled = shizukuActive,
            shizukuStatus = if (shizukuActive) shizukuRunner.status()
            else com.ghostlock.app.domain.model.ShizukuStatus.NOT_REQUIRED,
        )
    }

    override fun selectCpuPair(index: Int) {
        if (index !in cpuPairs.indices) return
        selectedCpuPair = index
        appContext.getSharedPreferences("ghostlock_prefs", Context.MODE_PRIVATE).edit {
                putString("cpu_pair", cpuPairs[index].toString())
            }
    }

    override fun setSafeModeEnabled(enabled: Boolean) {
        safeModeEnabled = enabled
    }

    override fun setShizukuEnabled(enabled: Boolean) {
        shizukuEnabled = enabled
        shizukuPreferenceSet = true
        appContext.getSharedPreferences("ghostlock_prefs", Context.MODE_PRIVATE)
            .edit {
                putBoolean("shizuku_enabled", enabled)
                putBoolean("shizuku_explicit", true)
            }
        if (enabled) shizukuRunner.requestPermission()
    }

    override fun profileController(): ProfileConfigController = profileController

    /* debug-ui: preferences for the hidden debug screen. */
    override suspend fun debugSettings(): DebugSettings = DebugSettings(
        exportEnabled = preferences.getBoolean(PrefDebugExportEnabled, true),
        exportLocation = normalizeDebugLocation(preferences.getString(PrefDebugExportLocation, null)),
        kernelLogEnabled = preferences.getBoolean(PrefDebugKernelLogEnabled, true),
    )

    override fun setDebugExportEnabled(enabled: Boolean) {
        preferences.edit { putBoolean(PrefDebugExportEnabled, enabled) }
    }

    override fun setDebugExportLocation(location: String) {
        preferences.edit { putString(PrefDebugExportLocation, normalizeDebugLocation(location)) }
    }

    override fun setDebugKernelLogEnabled(enabled: Boolean) {
        preferences.edit { putBoolean(PrefDebugKernelLogEnabled, enabled) }
    }

    private fun normalizeDebugLocation(value: String?): String {
        val cleaned = value.orEmpty().trim().trim('/').replace(Regex("/{2,}"), "/")
        val safe = cleaned.takeIf { candidate ->
            candidate.isNotEmpty() && candidate.split('/').none { it == ".." || it == "." }
        }
        return safe ?: DefaultDebugLocation
    }

    /* Profile configuration now lives in AndroidProfileConfigController: the
     * repository only wires it and forwards the native document. */

    override suspend fun exportCandidates(): List<OffsetCandidate> {
        val current = System.getProperty("os.version", "")
        val byRelease = linkedMapOf<String, ValueMap>()
        for (entry in userProfileStore.entries()) {
            val release = (entry["release"] as? String).orEmpty()
            if (release.isEmpty()) continue
            LegacyProfileConverter.convertValue(entry)
            if (builtinProfiles.builtin.containsKey(release) && matchesBuiltin(entry)) continue
            byRelease.putIfAbsent(release, entry)
        }
        return byRelease.entries
            .sortedWith(
                compareBy<Map.Entry<String, ValueMap>> { if (it.key == current) 0 else 1 }
                    .thenBy { it.key },
            )
            .map { (release, entry) -> OffsetCandidate(release, HoconSupport.render(entry)) }
    }

    override suspend fun importOffsets(documents: Map<String, String>): OffsetImportResult =
        mergeImported(documents, overwrite = false)

    override suspend fun confirmImport(documents: Map<String, String>): OffsetImportResult =
        mergeImported(documents, overwrite = true)

    private fun mergeImported(documents: Map<String, String>, overwrite: Boolean): OffsetImportResult {
        return try {
            val imported = parseImportDocuments(documents)
                ?: return OffsetImportResult.Failed("not a valid profile document")
            /* Unlike export candidates, imports are never compared against the
             * bundled tables: whatever the user picked lands in the store. */
            val fresh = imported.filter { (it["release"] as? String).orEmpty().isNotEmpty() }
            if (fresh.isEmpty()) return OffsetImportResult.AlreadyPresent

            val releases = freshReleases(fresh)
            val overlaps = releases.filter { userProfileStore.containsRelease(it) }
            if (!overwrite && overlaps.isNotEmpty()) {
                return OffsetImportResult.RequiresOverwrite(overlaps)
            }
            if (overwrite) dropReplacedDocuments(releases)
            for ((name, text) in documents) userProfileStore.save(name, text)
            OffsetImportResult.Imported(releases)
        } catch (error: CancellationException) {
            throw error
        } catch (error: UserProfileStore.MissingIncludes) {
            OffsetImportResult.MissingIncludes(error.files)
        } catch (error: Exception) {
            OffsetImportResult.Failed(error.message ?: "import failed")
        }
    }

    /** Confirmed overwrite: drop old documents fully superseded by [releases]. */
    private fun dropReplacedDocuments(releases: List<String>) {
        val incoming = releases.toSet()
        userProfileStore.list().forEach { stored ->
            if (stored.releases.isNotEmpty() && stored.releases.all { it in incoming }) {
                userProfileStore.delete(stored.name)
            }
        }
    }

    /**
     * Parses every picked document, resolving includes against the picked
     * files first (by full name or base name), then the bundled assets.
     * Anything unresolvable is reported so the user can pick it too; only
     * objects carrying a `release` become entries.
     */
    private fun parseImportDocuments(documents: Map<String, String>): List<ValueMap>? {
        val entries = mutableListOf<ValueMap>()
        documents.forEach { (_, text) ->
            entries += userProfileStore.parseEntries(text, extraDocuments = documents)
        }
        return entries.takeIf { it.isNotEmpty() }
    }

    override suspend fun parseSource(input: String, xblPath: String?, overwrite: Boolean, onLog: (String) -> Unit): ParseResult {
        val parsedFile = File(filesDir, "offsets_parse.tmp")
        var tempBootFile: File? = null
        var tempXblFile: File? = null
        return try {
            if (overwrite) {
                val pending = pendingParsedDocument
                if (pending != null) {
                    pendingParsedDocument = null
                    dropReplacedDocuments(pending.releases)
                    userProfileStore.save(pending.name, pending.text)
                    return ParseResult.Parsed(pending.releases)
                }
            }
            val binary = File(appContext.applicationInfo.nativeLibraryDir, ExtractBinaryName)
            if (!binary.isFile) return ParseResult.Failed(1, "missing native binary: ${binary.absolutePath}")

            /* Remote OTA URLs are resolved by the pure-Kotlin extractor so the
             * Android binary ships without the http stack; local files keep
             * going straight to the Rust extractor. */
            val isRemoteUrl = input.startsWith("http://", ignoreCase = true) ||
                input.startsWith("https://", ignoreCase = true)
            val (effectiveInput, effectiveXblPath) = if (isRemoteUrl) {
                val extracted = OtaPayloadExtractor.extractPartitions(
                    url = input,
                    workDir = filesDir,
                    onLog = onLog,
                )
                tempBootFile = extracted.bootFile
                tempXblFile = extracted.xblConfigFile
                Pair(extracted.bootFile.absolutePath, extracted.xblConfigFile?.absolutePath ?: xblPath)
            } else {
                Pair(input, xblPath)
            }

            parsedFile.delete()
            val args = buildList {
                add(effectiveInput)
                if (effectiveXblPath != null) {
                    add("--xbl-config")
                    add(effectiveXblPath)
                }
                addAll(listOf("--format", "json", "--out", parsedFile.absolutePath, "--work-dir", filesDir.absolutePath))
            }
            onLog("extract: $effectiveInput")
            val code = runProcess(
                ProcessBuilder(listOf(binary.absolutePath) + args).directory(filesDir).redirectErrorStream(true).apply {
                        environment()["GHOSTLOCK_HOME"] = filesDir.absolutePath
                        environment()["TMPDIR"] = filesDir.absolutePath
                        environment()["HOME"] = filesDir.absolutePath
                    },
                onLog = onLog,
                timeoutSeconds = 1800,
            )
            onLog("extract exit code=$code")
            if (code != 0 || !parsedFile.isFile) return ParseResult.Failed(code)
            val document = parsedFile.readText()
            val fresh = parseEntries(document) ?: return ParseResult.Failed(code, "invalid extractor output")
            /* Parsed reports are stored as-is, even when they match a bundled
             * profile; loading decides whether they take effect. */
            val filtered = fresh.mapNotNull { it.asValueMap() }
                .filter { (it["release"] as? String).orEmpty().isNotEmpty() }
            if (filtered.isEmpty()) return ParseResult.AlreadyPresent
            val releases = freshReleases(filtered)
            val overlaps = releases.filter { userProfileStore.containsRelease(it) }
            val name = parsedDocumentName(releases)
            if (!overwrite && overlaps.isNotEmpty()) {
                pendingParsedDocument = PendingParsedDocument(name, document, releases)
                return ParseResult.RequiresOverwrite(overlaps)
            }
            dropReplacedDocuments(releases)
            userProfileStore.save(name, document)
            ParseResult.Parsed(releases)
        } catch (error: CancellationException) {
            throw error
        } catch (error: Exception) {
            ParseResult.Failed(1, error.message)
        } finally {
            parsedFile.delete()
            tempBootFile?.delete()
            tempXblFile?.delete()
        }
    }

    override suspend fun runExploit(pair: CpuPair, onLog: (String) -> Unit): Int =
        withDebugAttackLog("direct", onLog) { archivedLog, debugDir ->
            runExploitBinary(pair, "libghostlock.so", archivedLog, debugDir)
        }.also { code -> recordLastRun(code, shizuku = false) }

    override suspend fun runExploitWithShizuku(pair: CpuPair, onLog: (String) -> Unit): Int {
        return withDebugAttackLog("shizuku", onLog) { archivedLog, debugDir ->
            val release = System.getProperty("os.version", "").orEmpty()
            val config = profileController.load(release, pair)
            val profileBlob = profileController.nativeDocument(config)
            when {
                !config.hasProfile || profileBlob == null -> {
                    archivedLog("error: profile is unavailable for $release")
                    1
                }

                config.invalidPaths.isNotEmpty() -> {
                    archivedLog(
                        "error: profile has ${config.invalidPaths.size} invalid field(s): " +
                            config.invalidPaths.take(6).joinToString(),
                    )
                    1
                }

                else -> shizukuRunner.run(pair, safeModeEnabled, profileBlob, debugDir, archivedLog)
            }
        }.also { code -> recordLastRun(code, shizuku = true) }
    }

    private suspend fun recordLastRun(code: Int, shizuku: Boolean) = withContext(Dispatchers.IO) {
        runCatching {
            File(filesDir, LastRunFileName).writeText(
                "$code ${if (shizuku) 1 else 0} ${System.currentTimeMillis()}\n",
                StandardCharsets.UTF_8,
            )
        }
    }

    override suspend fun lastRunW3SeccompHint(): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            val parts = File(filesDir, LastRunFileName)
                .takeIf { it.isFile }
                ?.readText()
                ?.trim()
                ?.split(' ')
                ?: return@runCatching false
            val code = parts.getOrNull(0)?.toIntOrNull() ?: return@runCatching false
            val ranWithShizuku = parts.getOrNull(1) == "1"
            if (code == 0 || ranWithShizuku) return@runCatching false
            val nativeLog = File(filesDir, NativeLogFileName)
            nativeLog.isFile && nativeLog.readText().contains(W3SeccompFailureMarker)
        }.getOrDefault(false)
    }

    private suspend fun withDebugAttackLog(
        entry: String,
        onLog: (String) -> Unit,
        run: suspend ((String) -> Unit, String?) -> Int,
    ): Int {
        val settings = debugSettings()
        if (!settings.exportEnabled) return run(onLog, null)
        val archive = DebugAttackLog.open(appContext, entry, settings.exportLocation)
        if (archive == null) {
            onLog("warning: cannot create ${settings.exportLocation} debug log")
            return run(onLog, null)
        }
        val archivedLog: (String) -> Unit = { line ->
            runCatching { archive.append(line) }
            onLog(line)
        }
        return try {
            archivedLog("debug log: ${archive.folderPath}/${archive.displayName}")
            archivedLog("debug dump dir: ${archive.folderFile.absolutePath}")
            run(archivedLog, if (settings.kernelLogEnabled) archive.folderFile.absolutePath else null)
        } finally {
            runCatching { archive.close() }
        }
    }

    override fun requestShizukuPermission() = shizukuRunner.requestPermission()

    override fun setShizukuStatusListener(listener: (() -> Unit)?) =
        shizukuRunner.setStatusListener(listener)

    private suspend fun runExploitBinary(
        pair: CpuPair,
        binaryName: String,
        onLog: (String) -> Unit,
        debugDir: String?,
    ): Int {
        val workDir = filesDir
        return try {
            val binary = File(appContext.applicationInfo.nativeLibraryDir, binaryName)
            require(binary.isFile) { "missing native binary: ${binary.absolutePath}" }
            if (prepareKsud(workDir, onLog) != null) onLog("ksud ready") else onLog("warning: ksud not found")
            // U01-S14: a per-run KernelSU log path so a previous run's markers
            // can never satisfy the handoff probe; passed to the native process.
            val ksuLog = File(workDir, ksuLogName(System.currentTimeMillis()))
            val nativeLog = File(workDir, NativeLogFileName)
            nativeLog.writeText("")
            val release = System.getProperty("os.version", "").orEmpty()
            val config = profileController.load(release, pair)
            if (config.invalidPaths.isNotEmpty()) {
                error(
                    "profile has ${config.invalidPaths.size} invalid field(s): " +
                        config.invalidPaths.take(6).joinToString(),
                )
            }
            val profileBlob = profileController.nativeDocument(config)
                ?: error("profile is unavailable for $release")
            val ksuOffset = AtomicLong()
            val nativeOffset = AtomicLong()
            // tag root-script lines so they cannot be read as the native stages'
            val ksuSink: (String) -> Unit = { onLog("[ksu] $it") }
            val tailer = Thread {
                try {
                    while (!Thread.currentThread().isInterrupted) {
                        tailKsuLog(nativeLog, nativeOffset, onLog)
                        tailKsuLog(ksuLog, ksuOffset, ksuSink)
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
            val command = ProcessBuilder(
                binary.absolutePath, "--ghostlock-app-call",
            )
                .directory(workDir)
                .redirectErrorStream(true)
                .redirectOutput(nativeLog)
                .apply {
                    environment()["GHOSTLOCK_HOME"] = workDir.absolutePath
                    environment()["TMPDIR"] = workDir.absolutePath
                    environment()["HOME"] = workDir.absolutePath
                    environment()["GHOSTLOCK_KSU_LOG"] = ksuLog.absolutePath
                    if (BuildConfig.DEBUG) environment()["GHOSTLOCK_VERBOSE_DEBUG"] = "1"
                    if (!debugDir.isNullOrEmpty()) environment()["GHOSTLOCK_DEBUG_DIR"] = debugDir
                    if (pair.primary != 0 || pair.consumer != 1) {
                        environment()["GHOSTLOCK_CORE"] = pair.primary.toString()
                        environment()["GHOSTLOCK_CONSUMER_CORE"] = pair.consumer.toString()
                    }
                    if (safeModeEnabled) environment()["GHOSTLOCK_DISABLE_MODULES"] = "1"
                    if (!tcpRouteEnabled) environment()["GHOSTLOCK_TCP_ROUTE"] = "0"
                }
            try {
                runProcess(command, onLog = {}, captureOutput = false, stdin = profileBlob)
            } finally {
                withContext(Dispatchers.IO) {
                    tailer.interrupt()
                    tailer.join(1000)
                    tailKsuLog(nativeLog, nativeOffset, onLog)
                    tailKsuLog(ksuLog, ksuOffset, ksuSink)
                }
            }
        } catch (error: CancellationException) {
            throw error
        } catch (error: Exception) {
            onLog("error: ${error::class.simpleName}: ${error.message}")
            1
        }
    }

    override suspend fun readDocument(uri: String): String =
        appContext.contentResolver.openInputStream(uri.toUri())?.bufferedReader()?.use { it.readText() } ?: throw IOException("cannot open $uri")

    override suspend fun cacheDocument(uri: String, fileName: String): String {
        val target = File(filesDir, fileName)
        appContext.contentResolver.openInputStream(uri.toUri())?.use { input ->
            target.outputStream().use(input::copyTo)
        } ?: throw IOException("cannot open $uri")
        return target.absolutePath
    }

    override suspend fun userProfiles(): List<UserProfileFile> =
        withContext(Dispatchers.IO) {
            userProfileStore.list().map { stored ->
                UserProfileFile(
                    name = stored.name,
                    releases = stored.releases,
                    importedAt = stored.importedAt,
                    sizeBytes = stored.sizeBytes,
                    parseError = stored.parseError,
                    version = stored.version,
                )
            }
        }

    override suspend fun deleteUserProfile(name: String): Boolean {
        val deleted = withContext(Dispatchers.IO) { userProfileStore.delete(name) }
        if (deleted) profileController.onUserProfileDeleted(name)
        return deleted
    }

    override suspend fun renameUserProfile(name: String, newName: String): String? {
        val renamed = withContext(Dispatchers.IO) { userProfileStore.rename(name, newName) }
        if (renamed != null) profileController.onUserProfileRenamed(name, renamed)
        return renamed
    }

    /** Renders a stored document as HOCON and shares it; returns the URI. */
    override suspend fun exportUserProfile(name: String): String {
        val hocon = withContext(Dispatchers.IO) { userProfileStore.exportHocon(name) }
            ?: throw IOException("cannot export $name")
        return publishOffsets(OffsetCandidate(name.substringBeforeLast('.'), hocon))
    }

    /**
     * Converts a stored legacy document into the current layout and stores the
     * result as a new user document; returns its name, or null on failure.
     */
    override suspend fun convertUserProfile(name: String): String? {
        val hocon = withContext(Dispatchers.IO) { userProfileStore.exportHocon(name) }
            ?: return null
        val base = name.substringBeforeLast('.')
        return withContext(Dispatchers.IO) {
            userProfileStore.save("$base-converted.conf", hocon)
        }
    }

    /* ---- editing session: isolated from the live attack controller ---- */

    private var editSession: AndroidProfileConfigController? = null
    private var editSessionTargetName: String? = null
    private var editSessionLive = false

    override suspend fun beginEditSession(name: String?): ProfileConfig? =
        withContext(Dispatchers.IO) {
            endEditSession()
            val release = System.getProperty("os.version", "").orEmpty()
            val pair = cpuPairs.getOrNull(selectedCpuPair) ?: return@withContext null
            val sessionPreferences = appContext.getSharedPreferences(
                EditSessionPreferences,
                Context.MODE_PRIVATE,
            )
            sessionPreferences.edit().clear().commit()
            editSessionLive = name != null && name == profileController.activeUserProfile()
            if (editSessionLive) {
                val overrides = profileController.overridesSnapshot(release)
                if (overrides.isNotEmpty()) {
                    /* The store keeps one entry per release, so wrap it back. */
                    sessionPreferences.edit(commit = true) {
                        putString(
                            AndroidProfileConfigController.PrefDebugProfileOverrides,
                            HoconSupport.render(valueMapOf(release to overrides)),
                        )
                    }
                }
            }
            val session = AndroidProfileConfigController(
                context = appContext,
                filesDir = filesDir,
                userProfiles = userProfileStore,
                preferences = sessionPreferences,
                forcedUserProfile = name,
                forcedBuiltinRelease = profileController.activeBuiltinRelease(),
            )
            editSession = session
            editSessionTargetName = name
            runCatching { session.load(release, pair) }.getOrNull()
        }

    override fun editSessionController(): ProfileConfigController? = editSession

    override fun editSessionIsLive(): Boolean = editSessionLive

    override fun editSessionTarget(): String? = editSessionTargetName

    override suspend fun saveEditSessionInPlace(): Boolean = withContext(Dispatchers.IO) {
        if (editSessionLive) return@withContext false
        val session = editSession ?: return@withContext false
        val target = editSessionTargetName ?: return@withContext false
        val release = System.getProperty("os.version", "").orEmpty()
        val pair = cpuPairs.getOrNull(selectedCpuPair) ?: return@withContext false
        val document = session.renderResolved(release, pair) ?: return@withContext false
        userProfileStore.overwrite(target, document)
    }

    override suspend fun commitEditSession(): Boolean = withContext(Dispatchers.IO) {
        if (!editSessionLive) return@withContext false
        val session = editSession ?: return@withContext false
        val release = System.getProperty("os.version", "").orEmpty()
        profileController.replaceOverrides(release, session.overridesSnapshot(release))
        val pair = cpuPairs.getOrNull(selectedCpuPair) ?: return@withContext false
        runCatching { profileController.load(release, pair) }.isSuccess
    }

    override suspend fun saveEditSessionAsNew(): String? = withContext(Dispatchers.IO) {
        val session = editSession ?: return@withContext null
        val release = System.getProperty("os.version", "").orEmpty()
        val pair = cpuPairs.getOrNull(selectedCpuPair) ?: return@withContext null
        val stem = (editSessionTargetName ?: release)
            .substringBeforeLast('.')
            .replace(Regex("[^A-Za-z0-9._-]"), "_")
        val name = "${stem.ifEmpty { "profile" }}-edited.conf"
        if (session.saveResolved(release, pair, name)) name else null
    }

    override suspend fun exportEditSession(): String = withContext(Dispatchers.IO) {
        val session = editSession ?: throw IOException("no editing session")
        val release = System.getProperty("os.version", "").orEmpty()
        val pair = cpuPairs.getOrNull(selectedCpuPair) ?: throw IOException("no cpu pair")
        val document = session.renderResolved(release, pair)
            ?: throw IOException("cannot render the edited profile")
        val stem = editSessionTargetName?.substringBeforeLast('.') ?: release
        publishOffsets(OffsetCandidate(stem.ifEmpty { release }, document))
    }

    override fun endEditSession() {
        editSession = null
        editSessionTargetName = null
        editSessionLive = false
    }

    override suspend fun publishOffsets(candidate: OffsetCandidate): String {
        val safeRelease = candidate.release.replace(Regex("[^A-Za-z0-9._-]"), "_")
        val values = ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, "offsets-$safeRelease.conf")
            put(MediaStore.Downloads.MIME_TYPE, "text/plain")
        }
        val uri = appContext.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) ?: throw IOException("cannot create download entry")
        appContext.contentResolver.openOutputStream(uri)?.use { output ->
            output.write(candidate.document.toByteArray(StandardCharsets.UTF_8))
        } ?: throw IOException("cannot open download entry")
        return uri.toString()
    }

    override fun close() {
        shizukuRunner.close()
        synchronized(processes) {
            processes.forEach(Process::destroyForcibly)
            processes.clear()
        }
    }

    /**
     * Legacy/parse helper: reads HOCON or JSON text into a list of entries.
     * Stored user documents are parsed by [UserProfileStore] instead.
     */
    private fun parseEntries(text: String): ValueList? {
        if (text.isBlank()) return null
        return try {
            when (val value = HoconSupport.parseValue(text)) {
                is List<*> -> value.asValueList()
                is Map<*, *> -> ValueList().apply { value.asValueMap()?.let(::add) }
                else -> null
            }
        } catch (_: Exception) {
            null
        }
    }

    private class PendingParsedDocument(
        val name: String,
        val text: String,
        val releases: List<String>,
    )

    private fun parsedDocumentName(releases: List<String>): String {
        val stem = releases.firstOrNull().orEmpty()
            .replace(Regex("[^A-Za-z0-9._-]"), "_")
            .ifEmpty { "parsed" }
        return "$stem.json"
    }

    private fun freshReleases(entries: List<*>): List<String> =
        entries.mapNotNull { (it.asValueMap()?.get("release") as? String) }.distinct()

    private fun matchesBuiltin(entry: ValueMap): Boolean =
        OffsetMatching.matchesBuiltin(toKernelOffsets(entry), builtinProfiles.builtin)

    private fun toKernelOffsets(entry: ValueMap): KernelOffsets {
        /* Remote/main-era offsets are normalised before comparison. */
        LegacyProfileConverter.convertValue(entry)
        return KernelOffsets(
            release = (entry["release"] as? String).orEmpty(),
            scalars = scalarFields.associateWith { scalarValue(entry, it) },
            symbols = namespacedFields(entry, "offset"),
            structFields = namespacedFields(entry, "task_struct") +
                namespacedFields(entry, "cred") +
                namespacedFields(entry, "kernelsnitch"),
        )
    }

    /** Namespace fields, keyed as `namespace.field` to match the catalogue. */
    private fun namespacedFields(entry: ValueMap, namespace: String): Map<String, Long?> {
        val group = entry[namespace].asValueMap() ?: return emptyMap()
        return group.entries.associate { (field, value) ->
            "$namespace.$field" to (value as? Number)?.toLong()
        }
    }

    /** Reads a numeric field, following route branches and legacy flat keys. */
    private fun scalarValue(entry: ValueMap, field: String): Long? {
        val candidates = when {
            field == "compact_waiter" -> listOf(
                "route.tcp_zerocopy.compact_waiter",
                "fallback.route.tcp_zerocopy.compact_waiter",
                "compact_waiter",
            )

            field == "pselect_waiter_shift" -> listOf(
                "route.select_stack.waiter_shift",
                "fallback.route.select_stack.waiter_shift",
                "pselect_waiter_shift",
            )

            field.startsWith("mcast.") -> {
                val suffix = field.removePrefix("mcast.")
                listOf(
                    "route.multicast_waiter.$suffix",
                    "fallback.route.multicast_waiter.$suffix",
                    "mcast.$suffix",
                    "mcast_$suffix",
                )
            }

            else -> listOf(field, field.replace('.', '_'))
        }
        for (candidate in candidates) {
            nestedValue(entry, candidate)?.let { return it }
        }
        return null
    }

    private fun nestedValue(entry: ValueMap, path: String): Long? =
        (entry.getValueAt(path) as? Number)?.toLong()

    private val scalarFields = listOf(
        "kernel_major", "recommend_shizuku", "kernel_phys_load",
        "pselect_waiter_shift", "mcast.waiter_off", "mcast.buffer_size",
        "mcast.task_offset", "mcast.lock_offset", "mcast.fake_lock_offset",
        "mcast.fake_task_offset", "mcast.lock_slots_offset", "mcast.lock_slot_count",
        "mcast.lock_slot_stride",
        "kernelsnitch.collisions", "compact_waiter", "kernelsnitch.mm_struct_sz",
        "cred.copy_size", "cred.usage_offset", "cred.usage_value",
        "cred.caps_offset", "cred.caps_count", "cred.caps_value", "cred.ref_count",
        "cred.ref0_offset", "cred.ref1_offset", "cred.ref2_offset", "cred.ref3_offset",
        "cred.ref0_image", "cred.ref1_image", "cred.ref2_image", "cred.ref3_image",
    )

    private fun isKernelSupported(): Boolean {
        val version = System.getProperty("os.version", "").orEmpty()
        return version in builtinProfiles.unames || importedOffsetsMatch(version)
    }

    private fun importedOffsetsMatch(version: String): Boolean =
        userProfileStore.containsRelease(version)

    private fun importedOffsetsRecommendShizuku(version: String): Boolean =
        userProfileStore.recommendsShizuku(version)

    private fun buildCpuPairs() {
        cpuPairs.clear()
        cpuPairLabels.clear()
        val online = parseCpuList(readSysFile("/sys/devices/system/cpu/online"))
        online.groupBy { readMaxFreq(it) }.filterKeys { it > 0 }.toSortedMap(compareByDescending { it }).forEach { (freq, cluster) ->
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
        val saved = appContext.getSharedPreferences("ghostlock_prefs", Context.MODE_PRIVATE).getString("cpu_pair", null) ?: return
        val pair = saved.split(',').mapNotNull { it.trim().toIntOrNull() }
        if (pair.size == 2) cpuPairs.indexOf(CpuPair(pair[0], pair[1])).takeIf { it >= 0 }?.let { selectedCpuPair = it }
    }

    private fun restoreShizukuPreference() {
        val prefs = appContext.getSharedPreferences("ghostlock_prefs", Context.MODE_PRIVATE)
        shizukuPreferenceSet = prefs.getBoolean("shizuku_explicit", false)
        shizukuEnabled = prefs.getBoolean("shizuku_enabled", false)
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
                        "ro.vendor.oplus.market.name", "ro.vendor.oplus.market.enname"
                    ) else arrayOf("ro.vendor.oplus.market.enname", "ro.vendor.oplus.market.name"))
                )
            }

            "vivo" -> firstValidProperty("ro.vivo.market.name")
            "honor", "huawei" -> firstValidProperty("ro.config.marketing_name")
            "zte", "nubia" -> firstValidProperty("ro.vendor.product.ztename")
            else -> null
        }
        return marketName ?: listOfNotNull(
            manufacturer, Build.BRAND.orEmpty().takeIf { !it.equals(manufacturer, true) }, Build.MODEL.orEmpty()
        ).filter { it.isNotBlank() }.joinToString(" ")
    }

    private fun resolveSocName(): String = listOf(
        systemProperty("ro.soc.manufacturer"),
        systemProperty("ro.soc.model"),
    ).mapNotNull(::validDeviceName).joinToString(" ").ifBlank { "unknown" }

    private fun firstValidProperty(vararg keys: String): String? = keys.asSequence().firstNotNullOfOrNull { validDeviceName(systemProperty(it)) }

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
        captureOutput: Boolean = true,
        stdin: ByteArray? = null,
    ): Int = runInterruptible {
        val process = builder.start()
        synchronized(processes) { processes += process }
        if (stdin != null) {
            runCatching { process.outputStream.use { it.write(stdin) } }
        }
        val reader = if (captureOutput) Thread {
            try {
                process.inputStream.bufferedReader(StandardCharsets.UTF_8).useLines { lines -> lines.forEach(onLog) }
            } catch (_: IOException) {
            }
        }.apply {
            name = "process-output-reader"
            isDaemon = true
        } else null
        try {
            reader?.start()
            val finished = process.waitFor(timeoutSeconds, TimeUnit.SECONDS)
            if (!finished) {
                process.destroy()
                if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly()
            }
            reader?.let(::joinReader)
            if (finished) process.exitValue() else -1
        } finally {
            if (process.isAlive) process.destroyForcibly()
            reader?.interrupt()
            runCatching { process.inputStream.close() }
            reader?.let(::joinReader)
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
