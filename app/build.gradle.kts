@file:Suppress("UnstableApiUsage")

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.io.ByteArrayOutputStream
import java.security.MessageDigest
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
    id("org.jetbrains.kotlin.plugin.serialization")
    id("kotlin-parcelize")
}

// Keep this module's output under the repository-root build/ directory.
layout.buildDirectory.set(rootProject.layout.buildDirectory.dir("app"))

val appName = "GhostLock"
val appVersionName = "1.3"

val gitVersionCode = runCatching {
    providers.exec {
        commandLine("git", "rev-list", "--count", "HEAD")
    }.standardOutput.asText.get().trim().toInt()
}.getOrElse {
    logger.warn("git rev-list failed (${it.message}); versionCode falls back to 1")
    1
}

// //TODO(kotlin-disable-20261010): 资源路径仍在，模块已禁用；恢复见台账
val lkmManifestFile = rootProject.file("profile-core/src/main/resources/lkm-kmi-manifest.tsv")
val lkmSourceDir = rootProject.file("tools/lkm/ghostlock")
/* Generated artefacts live under the Gradle build root (user instruction: "构建统一
 * 通过 gradle 构建，并且产物放在 /build 下，不要乱放"). The app module owns them
 * because both LKM tasks live here and the consumer is :app's assets; the path is
 * still under $HOME, which is what the podman VM can mount. */
val lkmCacheDir = layout.buildDirectory.dir("lkm").get().asFile
val lkmGeneratedAssets = layout.buildDirectory.dir("generated/lkmAssets")
/* A6-4: the finishing script is a DEVICE PAYLOAD (tools/lkm/ghostlock/root_cmd.sh -
 * the same file the module build ships). It is packaged as an asset so the
 * device-side resolve can read it; being a source file it needs no container and
 * never blocks a build, so the fail-closed rule stays on the LKM images alone. */
val payloadGeneratedAssets = layout.buildDirectory.dir("generated/payloadAssets")

/** The repository's SINGLE NDK authority (the root script resolves the same keys;
 *  AGP 8's ApplicationExtension exposes no ndkDirectory). */
fun resolveNdkDir(): File {
    val properties = Properties()
    val local = rootProject.file("local.properties")
    if (local.isFile) local.inputStream().use { stream -> properties.load(stream) }
    val candidates = listOf(
        properties.getProperty("ndk.dir"),
        properties.getProperty("ondk.dir"),
        System.getenv("ANDROID_NDK_HOME"),
    )
    val dir = candidates.firstOrNull { !it.isNullOrBlank() }?.let { File(it) }
    check(dir != null && dir.isDirectory) {
        "NDK not found; set ANDROID_NDK_HOME or ndk.dir in local.properties"
    }
    return dir
}

val buildInfoSrc = layout.buildDirectory.dir("generated/source/buildInfo")

val generateBuildInfo = tasks.register("generateBuildInfo") {
    description = "generateBuildInfo"
    val outputDirectory = buildInfoSrc
    outputs.dir(outputDirectory)
    // Always rewrite so the debug UI shows the timestamp of the installed build.
    outputs.upToDateWhen { false }
    doLast {
        val directory = outputDirectory.get().asFile.resolve("com/ghostlock/app")
        directory.mkdirs()
        // CPP-BUILD-02: this task is the only writer of the directory, so any
        // other file is a stale duplicate that must not reach the Kotlin build.
        directory.listFiles()?.forEach { stale -> if (stale.isFile) stale.delete() }
        val timeMillis = System.currentTimeMillis()
        val label = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT)
            .format(Date(timeMillis))
        directory.resolve("BuildInfo.kt").writeText(
            buildString {
                appendLine("package com.ghostlock.app")
                appendLine()
                appendLine("/** Generated per build; shown only by debug builds. */")
                appendLine("object BuildInfo {")
                appendLine("    const val BUILD_TIME_EPOCH_MILLIS: Long = ${timeMillis}L")
                appendLine("    const val BUILD_TIME_LABEL: String = \"$label\"")
                appendLine("}")
            },
        )
    }
}

android {
    namespace = "com.ghostlock.app"
    compileSdk {
        version = release(37) {
            minorApiLevel = 2
        }
    }
    defaultConfig {
        applicationId = "com.ghostlock.app"
        minSdk = 31
        targetSdk = 37
        versionCode = gitVersionCode
        versionName = appVersionName
    }
    androidResources {
        localeFilters += listOf("en", "zh-rCN")
    }
    testOptions {
        unitTests.isIncludeAndroidResources = true
    }
    sourceSets {
        named("main") {
            kotlin.directories.add(buildInfoSrc.get().asFile.absolutePath)
            /* LKM images are generated (never committed); AGP forbids Provider here. */
            assets.directories.add(lkmGeneratedAssets.get().asFile.absolutePath)
            /* A6-4 finishing script, copied from the repository source. */
            assets.directories.add(payloadGeneratedAssets.get().asFile.absolutePath)
        }
    }
    val properties = Properties()
    runCatching { properties.load(project.rootProject.file("local.properties").inputStream()) }
    val keystorePath = (properties.getProperty("KEYSTORE_PATH") ?: System.getenv("KEYSTORE_PATH"))?.trim()?.takeIf { it.isNotEmpty() }
    val keystorePwd = properties.getProperty("KEYSTORE_PASS") ?: System.getenv("KEYSTORE_PASS")
    val alias = properties.getProperty("KEY_ALIAS") ?: System.getenv("KEY_ALIAS")
    val pwd = properties.getProperty("KEY_PASSWORD") ?: System.getenv("KEY_PASSWORD")
    val keystoreFile = keystorePath?.let(::file)?.takeIf { it.isFile && it.length() > 0L }
    if (keystoreFile != null) {
        signingConfigs {
            create("release") {
                storeFile = keystoreFile
                storePassword = keystorePwd
                keyAlias = alias
                keyPassword = pwd
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }
    buildTypes {
        release {
            optimization.enable = true
            vcsInfo.include = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName(if (keystoreFile != null) "release" else "debug")
        }
        debug {
            signingConfig = signingConfigs.getByName(if (keystoreFile != null) "release" else "debug")
        }
    }
    buildFeatures {
        buildConfig = true
        aidl = true
        /* 2026-10-17：模块必须声明 Compose，Android Studio 才会把 @Preview 当预览渲染
         * （没有这一行，gutter 上的图标只会给出运行配置，预览面板不启动）。 */
        compose = true
    }
    dependenciesInfo {
        includeInApk = false
        includeInBundle = false
    }
    // The native exploit runtime is deliberately shipped only for arm64.
    lint {
        disable += "ChromeOsAbiSupport"
    }
    packaging {
        jniLibs {
            useLegacyPackaging = true
            excludes += "lib/*/libandroidx.graphics.path.so"
        }
        dex {
            useLegacyPackaging = true
        }
    }
    splits {
        abi {
            isEnable = true
            isUniversalApk = false
            reset()
            include("arm64-v8a")
        }
    }
}

androidComponents {
    onVariants(selector().withBuildType("release")) {
        it.packaging.resources.excludes
            .add("**")
    }
}

base {
    archivesName.set("$appName-v$appVersionName($gitVersionCode)")
}

kotlin {
    jvmToolchain(21)
    /* KernelSU 移植界面用到的实验 API（上游 manager 模块级 opt-in 的等价物）。 */
    compilerOptions {
        optIn.addAll(
            "androidx.compose.material3.ExperimentalMaterial3Api",
            "androidx.compose.material3.ExperimentalMaterial3ExpressiveApi",
        )
    }
}

// The exporter-agreement test compares against the freshly exported .bin set.
tasks.withType<Test>().configureEach {
    // //TODO(kotlin-disable-20261010): profile-core 任务已禁用
    // dependsOn(":profile-core:exportProfiles")

    /* JNI 协议表是跨端契约：Kotlin 侧的对拍测试（NativeProtocolTest）必须看到 native 导出的那一份，
     * 所以单测先触发导出（导出任务自己依赖宿主构建）。禁止手抄 op/key/status 三张表。 */
    dependsOn(rootProject.tasks.named("exportJniProtocolManifest"))
    /* Forward the M3 one-off write-back switch to the forked test JVM. */
    providers.systemProperty("glk.m3.writeback").orNull?.let { systemProperty("glk.m3.writeback", it) }
}

tasks.named("preBuild") {
    dependsOn(generateBuildInfo)
}

/* The arm64 native payload is only needed by the tasks that merge/package the
 * APK/AAB. Keeping it off preBuild means pure JVM unit tests
 * (:app:testDebugUnitTest) no longer build the native binaries, so a developer
 * machine without the NDK / aarch64 Rust target can still run them. */
tasks.matching { task ->
    (task.name.startsWith("merge") && task.name.endsWith("JniLibFolders")) ||
        (task.name.startsWith("merge") && task.name.endsWith("NativeLibs"))
}.configureEach {
    dependsOn(rootProject.tasks.named("prepareGhostlockJniLibs"))
    dependsOn(rootProject.tasks.named("prepareGhostlockExtractJniLibs"))
}

/* ---- LKM / DDK images (user instruction 2026-10-06: Gradle KTS, cross-platform, no .sh) ----
 *
 * Two task CLASSES with injected services and typed properties: the configuration
 * cache cannot serialize script references, so a task action must never touch
 * project/providers. Plain helpers live in LkmSupport. */
data class LkmKmiRow(val label: String, val kmi: String, val koFileName: String)

/** Plain helpers: no Gradle types, safe inside a serialized task action. */
object LkmSupport {
    const val KMI_COUNT = 8

    fun readManifest(file: File): List<LkmKmiRow> {
        check(file.isFile) {
            "missing LKM manifest: " + file + " (the manifest lives in profile-core/src/main/resources; the module images come from ./gradlew buildLkmImages)"
        }
        val rows = file.readLines()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .map { line ->
                val parts = line.split('\t')
                check(parts.size == 4) { "lkm-kmi-manifest needs 4 tab-separated columns: " + line }
                LkmKmiRow(parts[0], parts[2], parts[3])
            }
        check(rows.size == KMI_COUNT) {
            "lkm-kmi-manifest must declare " + KMI_COUNT + " KMIs, got " + rows.size
        }
        return rows
    }

    fun sha256Hex(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { stream ->
            val buffer = ByteArray(1 shl 16)
            var read = stream.read(buffer)
            while (read > 0) {
                digest.update(buffer, 0, read)
                read = stream.read(buffer)
            }
        }
        return digest.digest().joinToString("") { byte -> "%02x".format(byte) }
    }

    /** The digest ledger: one row per PRODUCED image, rewritten after each label. */
    const val LEDGER_NAME = "kmis.tsv"
    const val LEDGER_HEADER = "# label	image	digest	ko_sha256	bytes"

    fun readLedger(file: File): List<List<String>> =
        if (!file.isFile) {
            emptyList()
        } else {
            file.readLines()
                .filter { it.isNotBlank() && !it.startsWith("#") }
                .map { line -> line.split('	') }
        }

    /** Rows DERIVED from the cache (present .ko only), in manifest order. */
    fun ledgerRows(
        rows: List<LkmKmiRow>,
        cache: File,
        previous: List<List<String>>,
    ): List<MutableList<String>> = rows.mapNotNull { row ->
        val ko = File(cache, row.label + "/ghostlock.ko")
        if (!ko.isFile) return@mapNotNull null
        val old = previous.firstOrNull { it.size == 5 && it[0] == row.label }
        mutableListOf(
            row.label,
            old?.get(1) ?: "-",
            old?.get(2) ?: "-",
            sha256Hex(ko),
            ko.length().toString(),
        )
    }

    fun writeLedger(file: File, rows: List<List<String>>) {
        file.parentFile?.mkdirs()
        val body = rows.joinToString("\n") { entry -> entry.joinToString("	") }
        file.writeText(if (rows.isEmpty()) LEDGER_HEADER + "\n" else LEDGER_HEADER + "\n" + body + "\n")
    }

    /**
     * Fail-closed ledger/cache agreement: every recorded row must name a present
     * .ko whose bytes hash to the recorded sha256, and no present .ko may be
     * unrecorded. Detects a stale ledger, a deleted or replaced image.
     */
    fun verifyLedger(file: File, rows: List<LkmKmiRow>, cache: File) {
        val recorded = readLedger(file)
        val present = rows.filter { File(cache, it.label + "/ghostlock.ko").isFile }.map { it.label }
        val listed = recorded.map { it.getOrNull(0) ?: "" }
        check(listed == present) {
            "LKM ledger/cache mismatch: ledger lists " + listed + " but the cache holds " + present
        }
        for (entry in recorded) {
            check(entry.size == 5) {
                "LKM ledger row needs 5 tab-separated columns: " + entry.joinToString("|")
            }
            val ko = File(cache, entry[0] + "/ghostlock.ko")
            check(ko.isFile) { "LKM ledger lists " + entry[0] + " but " + ko + " is missing" }
            val actual = sha256Hex(ko)
            check(actual == entry[3]) {
                "LKM ledger sha256 for " + entry[0] + " is " + entry[3] +
                    " but " + ko + " hashes to " + actual
            }
        }
    }

    /** llvm-objcopy from the NDK: the prebuilt host tag is globbed, never hardcoded. */
    fun objcopyIn(ndk: File): File {
        val matches = File(ndk, "toolchains/llvm/prebuilt").listFiles().orEmpty()
            .map { File(it, "bin/llvm-objcopy") }
            .filter { it.isFile }
        check(matches.isNotEmpty()) {
            "llvm-objcopy not found under " + ndk + "/toolchains/llvm/prebuilt/*/bin"
        }
        return matches.first()
    }

    /**
     * podman -> docker; the -P override is passed in by the task.
     *
     * 每个候选都先按名字试、再试 Homebrew 的绝对路径：Gradle 守护进程的 PATH 里常常没有
     * /opt/homebrew/bin（本机 podman 就装在那里），只按名字找会得到 "no container engine found"，
     * 而工具其实装了 —— 2026-10-14 实测踩到。
     */
    fun detectEngine(execOps: ExecOperations, override: String?): String {
        override?.trim()?.takeIf { it.isNotEmpty() }?.let { return it }
        /* 2026-10-18 用户裁定：**docker 优先**（兼容性），podman 作为回退。
         * Docker Desktop 的 CLI 不在 PATH 里，所以把它的固定安装路径也列进来。 */
        /* 2026-10-18：docker → Apple container → podman。Apple container 与 docker 参数兼容
         * （--rm/--platform/--network/-v/-w 同名），所以同一套命令行直接可用。 */
        for (candidate in listOf("docker", "container", "podman")) {
            val exes = listOf(
                candidate,
                "/opt/homebrew/bin/" + candidate,
                "/opt/homebrew/opt/container/bin/" + candidate,
                "/usr/local/bin/" + candidate,
                "/Applications/Docker.app/Contents/Resources/bin/" + candidate,
                System.getProperty("user.home") + "/.docker/bin/" + candidate,
            )
            for (exe in exes) {
                val ok = runCatching {
                    execOps.exec {
                        commandLine(exe, "--version")
                        standardOutput = ByteArrayOutputStream()
                        errorOutput = ByteArrayOutputStream()
                    }
                }.isSuccess
                if (ok) return exe
            }
        }
        throw GradleException(
            "no container engine found: install docker / Apple container / podman, or pass -PcontainerEngine=<exe>",
        )
    }
}

/** 每个 KMI 一个 worker：容器 build + objcopy + 落到缓存。并行度由 Gradle --max-workers 决定。 */
interface LkmBuildParams : WorkParameters {
    val engine: Property<String>
    val objcopy: Property<String>
    val label: Property<String>
    val image: Property<String>
    val sourceDir: DirectoryProperty
    val cacheDir: DirectoryProperty

    /** 单个 LKM 内部 make 的并行度（-jN）。 */
    val makeJobs: Property<Int>
}

abstract class LkmBuildWorkAction : WorkAction<LkmBuildParams> {
    @get:Inject abstract val execOps: ExecOperations
    @get:Inject abstract val fsOps: FileSystemOperations

    override fun execute() {
        val engine = parameters.engine.get()
        val label = parameters.label.get()
        val image = parameters.image.get()
        val cache = parameters.cacheDir.get().asFile
        /* 每个 label 的临时目录都在缓存下（源码目录保持干净）。 */
        val startedAt = System.currentTimeMillis()
        println("[lkm] start " + label + " t=" + startedAt)
        val work = File(cache, "work-" + label)
        work.mkdirs()
        val source = parameters.sourceDir.get().asFile
        fsOps.copy {
            from(File(source, "Makefile"), File(source, "ghostlock.c"))
            into(work)
        }
        execOps.exec {
            commandLine(
                engine, "run", "--rm",
                "--platform", "linux/amd64",
                "--network=none",
                "-v", work.absolutePath + ":/work",
                "-w", "/work",
                image,
                "make", "-j" + parameters.makeJobs.get(), "-C", "/opt/ddk/kdir/" + label,
                "M=/work", "ARCH=arm64", "LLVM=1", "modules",
            )
        }
        val built = File(work, "ghostlock.ko")
        check(built.isFile) { "the container produced no ghostlock.ko for " + label }
        execOps.exec {
            commandLine(
                parameters.objcopy.get(), "--strip-unneeded",
                "-R", ".comment", "-R", ".note.gnu.build-id", "-R", ".note.gnu.property",
                "-R", ".note.Linux", "-R", ".note.GNU-stack", "-R", ".BTF", "-R", ".BTF.base",
                "-R", ".llvm_addrsig", "-R", ".hyp.text", "-R", ".hyp.bss", "-R", ".hyp.rodata",
                "-R", ".hyp.event_ids", "-R", ".hyp.patchable_function_entries", "-R", ".hyp.data",
                built.absolutePath,
            )
        }
        val targetDir = File(cache, label)
        targetDir.mkdirs()
        fsOps.copy { from(built); into(targetDir) }
        work.deleteRecursively()
        println("[lkm] done " + label + " t=" + System.currentTimeMillis() + " ms=" + (System.currentTimeMillis() - startedAt))
    }
}

/** Explicit: builds the 8 DDK images into the external cache. */
abstract class BuildLkmImagesTask : DefaultTask() {
    @get:Inject abstract val execOps: ExecOperations
    @get:Inject abstract val fsOps: FileSystemOperations
    @get:Inject abstract val workerExecutor: WorkerExecutor

    @get:InputFile abstract val manifestFile: RegularFileProperty
    /* NOT named lkmSourceDir: that script-level val would self-reference here. */
    @get:InputDirectory abstract val moduleSourceDir: DirectoryProperty
    @get:InputDirectory abstract val ndkDir: DirectoryProperty
    /* DECLARED OUTPUT: the images land here (build/app/lkm). Without an output
     * declaration Gradle can never mark this task UP-TO-DATE, so every build re-ran
     * the 8 container builds (~4 min) even with org.gradle.caching=true. */
    @get:OutputDirectory abstract val cacheDir: DirectoryProperty
    @get:Input @get:Optional abstract val containerEngine: Property<String>
    /** -PlkmLabel=<label> narrows the run; empty (the default) builds all 8. */
    @get:Input @get:Optional abstract val onlyLabel: Property<String>

    /** -PlkmCacheOnly=true：只复用 build/lkm 里的镜像，不启动容器（UI 迭代用）。 */
    @get:Input @get:Optional abstract val cacheOnly: Property<Boolean>

    /** -PlkmMakeJobs=N：每个 LKM 内部 make 的 -jN（默认 2，用户裁定；LKM 之间的并行由 org.gradle.workers.max=4 给）。 */
    @get:Input @get:Optional abstract val makeJobs: Property<Int>

    @TaskAction
    fun run() {
        val enginePath = LkmSupport.detectEngine(execOps, containerEngine.orNull)
        val objcopyPath = LkmSupport.objcopyIn(ndkDir.get().asFile)
        val allRows = LkmSupport.readManifest(manifestFile.get().asFile)
        val rows = allRows.filter { onlyLabel.orNull.isNullOrBlank() || it.label == onlyLabel.orNull }
        check(rows.isNotEmpty()) { "-PlkmLabel=" + onlyLabel.orNull + " matches no KMI label" }
        val cache = cacheDir.get().asFile
        cache.mkdirs()

        /* 2026-10-18：容器引擎没起时不要把整个 APK 构建拖死 —— 已有缓存就复用，并说清楚原因。
         * 显式 -PlkmCacheOnly=true 时连引擎都不探。 */
        val cachedAll = rows.all { File(cache, it.label + "/ghostlock.ko").isFile }
        /* 健康探测按引擎分派：Apple container 没有 `info`，用 `system status`。 */
        val probeArgs = if (enginePath.substringAfterLast('/') == "container") {
            listOf("system", "status")
        } else {
            listOf("info")
        }
        val engineAlive = !cacheOnly.getOrElse(false) && runCatching {
            execOps.exec {
                commandLine(listOf(enginePath) + probeArgs)
                standardOutput = ByteArrayOutputStream()
                errorOutput = ByteArrayOutputStream()
            }
        }.isSuccess
        if (cachedAll && !engineAlive) {
            logger.warn(
                "LKM: container engine 不可用（docker/podman 未运行），复用已有镜像 " +
                    rows.size + " 个（build/lkm/<label>/ghostlock.ko）；要重建请先 podman machine start",
            )
            return
        }
        /* 并行：每个 KMI 一个 worker（--max-workers 控制并发；默认按 CPU 数）。 */
        rows.forEach { row ->
            workerExecutor.noIsolation().submit(LkmBuildWorkAction::class) {
                engine.set(enginePath)
                objcopy.set(objcopyPath.absolutePath)
                label.set(row.label)
                image.set("ghcr.io/ylarod/ddk-min:" + row.label)
                sourceDir.set(moduleSourceDir)
                cacheDir.set(this@BuildLkmImagesTask.cacheDir)
                makeJobs.set(this@BuildLkmImagesTask.makeJobs.get())
            }
        }
        workerExecutor.await()

        /* 账本按 label 逐个写（顺序执行，避免并发写同一个 tsv）。 */
        for (row in rows) {
            val image = "ghcr.io/ylarod/ddk-min:" + row.label
            val target = File(cache, row.label + "/ghostlock.ko")
            check(target.isFile && target.length() > 0) { "empty LKM image for " + row.label }
            val digestOut = ByteArrayOutputStream()
            runCatching {
                execOps.exec {
                    commandLine(enginePath, "image", "inspect", image, "--format", "{{.Digest}}")
                    standardOutput = digestOut
                    errorOutput = ByteArrayOutputStream()
                }
            }
            val ledger = File(cache, LkmSupport.LEDGER_NAME)
            val merged = LkmSupport.ledgerRows(allRows, cache, LkmSupport.readLedger(ledger))
            merged.firstOrNull { it[0] == row.label }?.let { entry ->
                entry[1] = image
                entry[2] = digestOut.toString().trim()
            }
            LkmSupport.writeLedger(ledger, merged)
            LkmSupport.verifyLedger(ledger, allRows, cache)
            logger.lifecycle("LKM " + row.label + " -> " + target + " (" + target.length() + " bytes)")
        }
    }
}

/** Fail-closed: the APK assets carry exactly the 8 cached images. */
abstract class CopyLkmIntoAssetsTask : DefaultTask() {
    @get:Inject abstract val fsOps: FileSystemOperations

    @get:InputFile abstract val manifestFile: RegularFileProperty
    @get:Internal abstract val cacheDir: DirectoryProperty
    @get:OutputDirectory abstract val generatedDir: DirectoryProperty

    @TaskAction
    fun run() {
        val rows = LkmSupport.readManifest(manifestFile.get().asFile)
        val cache = cacheDir.get().asFile
        val missing = rows.filterNot { File(cache, it.label + "/ghostlock.ko").isFile }
        check(missing.isEmpty()) {
            "missing " + missing.size + " of " + rows.size + " LKM images (all are listed):" +
                missing.joinToString("") { entry ->
                    "\n  - " + entry.label + " -> expected " +
                        File(cache, entry.label + "/ghostlock.ko")
                } +
                "\nGenerate them with: ./gradlew buildLkmImages (needs podman/docker)," +
                " or copy ghostlock-<label>.ko into " + cache + "/<label>/ghostlock.ko"
        }
        val outDir = generatedDir.get().asFile
        fsOps.delete { delete(outDir) }
        rows.forEach { row ->
            fsOps.copy {
                from(File(cache, row.label + "/ghostlock.ko"))
                into(File(outDir, "lkm/" + row.label))
            }
        }
        logger.lifecycle("LKM assets: " + rows.size + " images -> " + outDir)
    }
}

val buildLkmImages = tasks.register<BuildLkmImagesTask>("buildLkmImages") {
    group = "ghostlock"
    description = "Builds the 8 DDK LKM images into build/lkm/<label> (needs docker, podman as fallback)"
    manifestFile.set(lkmManifestFile)
    moduleSourceDir.set(lkmSourceDir)
    cacheDir.set(lkmCacheDir)
    containerEngine.set((findProperty("containerEngine") as String?) ?: "")
    onlyLabel.set((findProperty("lkmLabel") as String?) ?: "")
    cacheOnly.set((findProperty("lkmCacheOnly") as String?)?.toBoolean() ?: false)
    makeJobs.set(((findProperty("lkmMakeJobs") as String?)?.toIntOrNull() ?: 2).coerceAtLeast(1))
    val ndkResolution = runCatching { resolveNdkDir() }
    ndkDir.set(ndkResolution.getOrElse { lkmManifestFile.parentFile })
    onlyIf { ndkResolution.isSuccess }
}

/** KernelSU 风格模块 zip：module/ + 8 个 KMI 镜像 -> build/ghostlock-module.zip。 */
val buildModuleZip = tasks.register<Zip>("buildModuleZip") {
    group = "ghostlock"
    description = "Packages module/ and the 8 LKM images into a KernelSU-style module zip"
    from(rootProject.file("module"))
    into("lkm") {
        from(lkmCacheDir) { include("*/ghostlock.ko") }
    }
    archiveFileName.set("ghostlock-module.zip")
    destinationDirectory.set(rootProject.layout.buildDirectory)
    dependsOn(buildLkmImages)
}

val copyLkmIntoAssets = tasks.register<CopyLkmIntoAssetsTask>("copyLkmIntoAssets") {
    group = "ghostlock"
    description = "Fail-closed: copies the 8 cached LKM images into the APK assets"
    /* Cold-build self-sufficiency: after `clean` the cache is EMPTY, so the images
     * must be BUILT here instead of assumed - `installDebug` then succeeds in one
     * go. The fail-closed behaviour is unchanged (the copy still refuses a cache
     * that disagrees with the manifest); it simply can no longer be reached with an
     * empty cache. Pure JVM unit tests are unaffected: they depend on neither the
     * packaging tasks nor this one. */
    dependsOn(buildLkmImages)
    manifestFile.set(lkmManifestFile)
    cacheDir.set(lkmCacheDir)
    generatedDir.set(lkmGeneratedAssets)
}

/** Standalone: proves the digest ledger still describes the cache. */
abstract class VerifyLkmLedgerTask : DefaultTask() {
    @get:InputFile abstract val manifestFile: RegularFileProperty
    @get:Internal abstract val cacheDir: DirectoryProperty

    @TaskAction
    fun run() {
        val rows = LkmSupport.readManifest(manifestFile.get().asFile)
        val cache = cacheDir.get().asFile
        val ledger = File(cache, LkmSupport.LEDGER_NAME)
        check(ledger.isFile) { "missing " + ledger + "; run ./gradlew buildLkmImages first" }
        LkmSupport.verifyLedger(ledger, rows, cache)
        logger.lifecycle("LKM ledger: " + LkmSupport.readLedger(ledger).size + " row(s) match the cache")
    }
}

val verifyLkmLedger = tasks.register<VerifyLkmLedgerTask>("verifyLkmLedger") {
    group = "ghostlock"
    description = "Fails when kmis.tsv and build/lkm disagree (stale/deleted/replaced .ko)"
    manifestFile.set(lkmManifestFile)
    cacheDir.set(lkmCacheDir)
}

/* A6-4: package the finishing script as a device payload asset. Plain Copy with no
 * fail-closed gate: the source file is part of the repository, so a missing one is
 * a broken checkout, not a device/KMI condition. */
val copyPayloadScriptIntoAssets = tasks.register<Copy>("copyPayloadScriptIntoAssets") {
    group = "ghostlock"
    description = "Copies the A6-4 finishing script into the APK assets (device payload)"
    from(rootProject.layout.projectDirectory.file("tools/lkm/ghostlock/root_cmd.sh"))
    into(payloadGeneratedAssets.map { it.dir("payload") })
}
tasks.matching { task -> task.name.startsWith("merge") && task.name.endsWith("Assets") }
    .configureEach {
        dependsOn(copyPayloadScriptIntoAssets)
        mustRunAfter(copyPayloadScriptIntoAssets)
    }

/* Fail-closed ONLY on the packaging path: package/bundle depend on the copy and the
 * asset merge is ordered after it, so pure JVM unit tests never need a container
 * while an APK build without the 8 images fails with the full list. */
/* Only the real APK/AAB packaging tasks: AGP also has package<...>Assets and
 * package<...>UnitTest tasks, and Robolectric runs the latter. */
val lkmPackagingTasks = setOf("packageDebug", "packageRelease", "bundleDebug", "bundleRelease")
tasks.matching { task -> task.name in lkmPackagingTasks }
    .configureEach { dependsOn(copyLkmIntoAssets) }
tasks.matching { task -> task.name.startsWith("merge") && task.name.endsWith("Assets") }
    .configureEach { mustRunAfter(copyLkmIntoAssets) }

dependencies {
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.foundation:foundation:1.12.1")
    /* Material3：新 UI 的默认套件（另一个是 miuix，见 app/ui/kit）；本仓无 BOM，版本显式写。
     * 1.5.0-alpha22 不是随手挑的：miuix 0.9.4 依赖的就是这一版，声明更低会被冲突解析顶上来，
     * 而"声明与实际不符"比版本标签难看更糟；miuix 升到稳定 material3 时这两行一起抬。 */
    implementation("androidx.compose.material3:material3:1.5.0-alpha28")
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    /* @Preview 的注解与预览渲染器（2026-10-17 用户要求加入 tooling.preview）：注解在 ui-tooling-preview，
     * 渲染器在 ui-tooling（只进 debug，不进 release APK）。 */
    implementation("androidx.compose.ui:ui-tooling-preview:1.12.1")
    debugImplementation("androidx.compose.ui:ui-tooling:1.12.1")
    implementation("com.typesafe:config:1.4.9")
    implementation("org.apache.commons:commons-compress:1.28.0")
    implementation("dev.rikka.shizuku:api:13.1.5")
    implementation("dev.rikka.shizuku:provider:13.1.5")
    /* KernelSU 管理器 UI 移植（2026-10-18）：依赖与上游 manager/app 对齐。 */
    implementation("androidx.core:core-splashscreen:1.2.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.11.0")
    implementation("androidx.navigationevent:navigationevent-compose:1.1.2")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.9.0")
    implementation("com.github.topjohnwu.libsu:core:6.0.0")
    implementation("com.github.topjohnwu.libsu:service:6.0.0")
    implementation("com.github.topjohnwu.libsu:io:6.0.0")
    implementation("dev.rikka.rikkax.parcelablelist:parcelablelist:2.0.1")
    implementation("org.apache.commons:commons-compress:1.28.0")
    implementation("org.tukaani:xz:1.12")
    implementation("org.commonmark:commonmark:0.30.0")
    implementation("org.commonmark:commonmark-ext-gfm-tables:0.30.0")
    implementation("org.commonmark:commonmark-ext-gfm-strikethrough:0.30.0")
    implementation("org.commonmark:commonmark-ext-autolink:0.30.0")
    implementation("org.commonmark:commonmark-ext-task-list-items:0.30.0")
    implementation("androidx.webkit:webkit:1.17.0")
    implementation("org.lsposed.hiddenapibypass:hiddenapibypass:6.1")
    implementation("me.zhanghai.android.appiconloader:appiconloader:1.5.0")
    implementation("com.materialkolor:material-kolor:5.0.1")
    implementation("top.yukonga.miuix.kmp:miuix-blur:0.9.4")
    implementation("top.yukonga.miuix.kmp:miuix-ui:0.9.4")
    implementation("top.yukonga.miuix.kmp:miuix-nav:0.9.4")
    implementation("top.yukonga.miuix.kmp:miuix-icons:0.9.4")
    implementation("top.yukonga.miuix.kmp:miuix-preference:0.9.4")
    // //TODO(kotlin-disable-20261010): profile-core 已禁用
    // implementation(project(":profile-core"))

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.17")
    /* Compose UI 测试（JVM/Robolectric）：版本与本仓 compose foundation 同源（1.12.1），
     * 无 BOM、无 version catalog ⇒ 必须显式版本。ui-test-manifest 未确认必需，暂不加。 */
    testImplementation("androidx.compose.ui:ui-test-junit4:1.12.1")
}
