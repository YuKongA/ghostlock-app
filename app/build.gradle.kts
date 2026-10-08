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

val lkmManifestFile = rootProject.file("profile-core/src/main/resources/lkm-kmi-manifest.tsv")
val lkmSourceDir = rootProject.file("tools/lkm/ghostlock")
/* Generated artefacts live under the Gradle build root (user instruction: "构建统一
 * 通过 gradle 构建，并且产物放在 /build 下，不要乱放"). The app module owns them
 * because both LKM tasks live here and the consumer is :app's assets; the path is
 * still under $HOME, which is what the podman VM can mount. */
val lkmCacheDir = layout.buildDirectory.dir("lkm").get().asFile
val lkmGeneratedAssets = layout.buildDirectory.dir("generated/lkmAssets")

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
        localeFilters += listOf("en", "zh")
    }
    testOptions {
        unitTests.isIncludeAndroidResources = true
    }
    sourceSets {
        named("main") {
            kotlin.directories.add(buildInfoSrc.get().asFile.absolutePath)
            /* LKM images are generated (never committed); AGP forbids Provider here. */
            assets.directories.add(lkmGeneratedAssets.get().asFile.absolutePath)
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
}

// The exporter-agreement test compares against the freshly exported .bin set.
tasks.withType<Test>().configureEach {
    dependsOn(":profile-core:exportProfiles")
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
            "missing LKM manifest: " + file + " (regenerate with: make -C src lkm-kmi-manifest)"
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

    /** podman -> docker; the -P override is passed in by the task. */
    fun detectEngine(execOps: ExecOperations, override: String?): String {
        override?.trim()?.takeIf { it.isNotEmpty() }?.let { return it }
        for (candidate in listOf("podman", "docker")) {
            val ok = runCatching {
                execOps.exec {
                    commandLine(candidate, "--version")
                    standardOutput = ByteArrayOutputStream()
                    errorOutput = ByteArrayOutputStream()
                }
            }.isSuccess
            if (ok) return candidate
        }
        throw GradleException(
            "no container engine found: install podman or docker, or pass -PcontainerEngine=<exe>",
        )
    }
}

/** Explicit: builds the 8 DDK images into the external cache. */
abstract class BuildLkmImagesTask : DefaultTask() {
    @get:Inject abstract val execOps: ExecOperations
    @get:Inject abstract val fsOps: FileSystemOperations

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

    @TaskAction
    fun run() {
        val engine = LkmSupport.detectEngine(execOps, containerEngine.orNull)
        val objcopy = LkmSupport.objcopyIn(ndkDir.get().asFile)
        val allRows = LkmSupport.readManifest(manifestFile.get().asFile)
        val rows = allRows.filter { onlyLabel.orNull.isNullOrBlank() || it.label == onlyLabel.orNull }
        check(rows.isNotEmpty()) { "-PlkmLabel=" + onlyLabel.orNull + " matches no KMI label" }
        val cache = cacheDir.get().asFile
        cache.mkdirs()
        for (row in rows) {
            /* Per-label work dir inside the build root: the SOURCE directory stays clean. */
            val work = File(cache, "work-" + row.label)
            work.mkdirs()
            fsOps.copy {
                from(moduleSourceDir.file("Makefile"), moduleSourceDir.file("ghostlock.c"))
                into(work)
            }
            val image = "ghcr.io/ylarod/ddk-min:" + row.label
            execOps.exec {
                commandLine(
                    engine, "run", "--rm",
                    "--platform", "linux/amd64",
                    "--network=none",
                    /* Stable in-container mount point: no host-path quirks. */
                    "-v", work.absolutePath + ":/work",
                    "-w", "/work",
                    image,
                    "make", "-C", "/opt/ddk/kdir/" + row.label,
                    "M=/work", "ARCH=arm64", "LLVM=1", "modules",
                )
            }
            val built = File(work, "ghostlock.ko")
            check(built.isFile) { "the container produced no ghostlock.ko for " + row.label }
            execOps.exec {
                commandLine(
                    objcopy.absolutePath, "--strip-unneeded",
                    "-R", ".comment", "-R", ".note.gnu.build-id", "-R", ".note.gnu.property",
                    "-R", ".note.Linux", "-R", ".note.GNU-stack", "-R", ".BTF", "-R", ".BTF.base",
                    "-R", ".llvm_addrsig", "-R", ".hyp.text", "-R", ".hyp.bss", "-R", ".hyp.rodata",
                    "-R", ".hyp.event_ids", "-R", ".hyp.patchable_function_entries", "-R", ".hyp.data",
                    built.absolutePath,
                )
            }
            val targetDir = File(cache, row.label)
            targetDir.mkdirs()
            fsOps.copy { from(built); into(targetDir) }
            val target = File(targetDir, "ghostlock.ko")
            check(target.isFile && target.length() > 0) { "empty LKM image for " + row.label }
            work.deleteRecursively()
            val digestOut = ByteArrayOutputStream()
            runCatching {
                execOps.exec {
                    commandLine(engine, "image", "inspect", image, "--format", "{{.Digest}}")
                    standardOutput = digestOut
                    errorOutput = ByteArrayOutputStream()
                }
            }
            /* Rewrite the ledger after EVERY label so a partial run still leaves
             * an accurate account of what the cache holds. */
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
    description = "Builds the 8 DDK LKM images into build/lkm/<label> (needs podman/docker)"
    manifestFile.set(lkmManifestFile)
    moduleSourceDir.set(lkmSourceDir)
    cacheDir.set(lkmCacheDir)
    containerEngine.set((findProperty("containerEngine") as String?) ?: "")
    onlyLabel.set((findProperty("lkmLabel") as String?) ?: "")
    val ndkResolution = runCatching { resolveNdkDir() }
    ndkDir.set(ndkResolution.getOrElse { lkmManifestFile.parentFile })
    onlyIf { ndkResolution.isSuccess }
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
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    implementation("com.typesafe:config:1.4.9")
    implementation("org.apache.commons:commons-compress:1.28.0")
    implementation("dev.rikka.shizuku:api:13.1.5")
    implementation("dev.rikka.shizuku:provider:13.1.5")
    implementation("top.yukonga.miuix.kmp:miuix-ui:0.9.4")
    implementation("top.yukonga.miuix.kmp:miuix-nav:0.9.4")
    implementation("top.yukonga.miuix.kmp:miuix-icons:0.9.4")
    implementation("top.yukonga.miuix.kmp:miuix-preference:0.9.4")
    implementation(project(":profile-core"))

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.17")
    /* Compose UI 测试（JVM/Robolectric）：版本与本仓 compose foundation 同源（1.12.1），
     * 无 BOM、无 version catalog ⇒ 必须显式版本。ui-test-manifest 未确认必需，暂不加。 */
    testImplementation("androidx.compose.ui:ui-test-junit4:1.12.1")
}
