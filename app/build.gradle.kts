@file:Suppress("UnstableApiUsage")

import com.android.build.api.artifact.SingleArtifact
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.io.ByteArrayOutputStream
import java.nio.file.Files
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
val appVersionName = "2.0.12"

val debugNativeManifest = layout.buildDirectory.file("native-artifacts/debug/manifest.tsv")

/**
 * Verifies the actual debug variant native artifacts after AGP has merged them.
 *
 * The expected artifact/JNI set comes only from the CMake-generated target
 * manifest. A header-only manifest is the authoritative disconnected state and
 * requires the merged output to contain no native artifact. LLVM tools come
 * from the same NDK Provider that AGP uses when at least one target exists.
 */
abstract class VerifyNativeArtifactsTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.InputDirectory
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val mergedNativeLibs: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.tasks.InputFile
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val manifestFile: org.gradle.api.file.RegularFileProperty

    @get:org.gradle.api.tasks.InputFile
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.NONE)
    abstract val llvmReadelf: org.gradle.api.file.RegularFileProperty

    @get:org.gradle.api.tasks.InputFile
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.NONE)
    abstract val llvmNm: org.gradle.api.file.RegularFileProperty

    @get:javax.inject.Inject
    abstract val execOps: org.gradle.process.ExecOperations

    private data class NativeManifestRow(
        val target: String,
        val artifact: String,
        val type: String,
        val expectedJni: Set<String>,
    )

    /** Checks zero-target disconnection or the exact ELF architecture/JNI artifact set. */
    @org.gradle.api.tasks.TaskAction
    fun verifyArtifacts() {
        val manifest = manifestFile.get().asFile
        val lines = manifest.readLines()
        check(lines.isNotEmpty()) { "native artifact manifest is empty: ${manifest.absolutePath}" }
        check(lines.first() == "target\tartifact\ttype\texpected_jni") {
            "native artifact manifest header mismatch: ${lines.first()}"
        }
        val rows = lines.drop(1).mapIndexed { index, line ->
            check(line.isNotBlank()) { "blank native artifact row at line ${index + 2}" }
            val fields = line.split('\t', limit = 4)
            check(fields.size == 4 && fields.take(3).all(String::isNotBlank)) {
                "native artifact row ${index + 2} must contain four fields"
            }
            val expectedJni = if (fields[3].isEmpty()) {
                emptySet()
            } else {
                fields[3].split(',').onEach { symbol ->
                    check(symbol.matches(Regex("Java_[A-Za-z0-9_]+"))) {
                        "invalid JNI symbol in manifest: $symbol"
                    }
                }.toSet()
            }
            check(fields[1].endsWith(".so") && '/' !in fields[1] && '\\' !in fields[1]) {
                "invalid native artifact name: ${fields[1]}"
            }
            check(fields[2] == "EXECUTABLE" || fields[2] == "SHARED_LIBRARY") {
                "invalid native target type: ${fields[2]}"
            }
            NativeManifestRow(fields[0], fields[1], fields[2], expectedJni)
        }
        check(rows.map(NativeManifestRow::target).toSet().size == rows.size) {
            "native artifact manifest contains duplicate targets"
        }
        check(rows.map(NativeManifestRow::artifact).toSet().size == rows.size) {
            "native artifact manifest contains duplicate artifacts"
        }

        val mergedRoot = mergedNativeLibs.get().asFile
        val libraryRoot = mergedRoot.resolve("lib")
        if (rows.isEmpty()) {
            val unexpectedArtifacts = if (libraryRoot.isDirectory) {
                libraryRoot.walkTopDown()
                    .filter { entry -> entry.isFile || Files.isSymbolicLink(entry.toPath()) }
                    .map { entry -> entry.relativeTo(mergedRoot).invariantSeparatorsPath }
                    .sorted()
                    .toList()
            } else {
                emptyList()
            }
            check(unexpectedArtifacts.isEmpty()) {
                "merged native artifacts exist while the CMake target manifest is empty: $unexpectedArtifacts"
            }
            logger.lifecycle("NATIVE_ARTIFACTS=PASS COUNT=0 ABI=none JNI=0")
            return
        }
        check(libraryRoot.isDirectory) {
            "merged native library root is missing: ${libraryRoot.absolutePath}"
        }
        val abiEntries = libraryRoot.listFiles().orEmpty()
        check(abiEntries.all { entry -> entry.isDirectory && !Files.isSymbolicLink(entry.toPath()) }) {
            "merged native lib root contains a non-directory or symlink entry"
        }
        val abiDirectories = abiEntries.map { file -> file.name }.toSet()
        check(abiDirectories == setOf("arm64-v8a")) {
            "merged native ABI set mismatch: $abiDirectories"
        }
        val arm64Directory = libraryRoot.resolve("arm64-v8a")
        val artifacts = arm64Directory.listFiles().orEmpty().associateBy { file -> file.name }
        val expectedArtifacts = rows.map(NativeManifestRow::artifact).toSet()
        check(artifacts.keys == expectedArtifacts) {
            "merged native artifact drift: expected=${expectedArtifacts.sorted()} actual=${artifacts.keys.sorted()}"
        }

        var totalJni = 0
        rows.forEach { row ->
            val artifact = artifacts.getValue(row.artifact)
            check(
                Files.isRegularFile(artifact.toPath()) &&
                    !Files.isSymbolicLink(artifact.toPath()) &&
                    artifact.length() > 0L,
            ) {
                "native artifact is not a non-empty regular file: ${artifact.absolutePath}"
            }
            val elfHeader = runTool(llvmReadelf.get().asFile, listOf("-h", artifact.absolutePath))
            check(Regex("(?m)^\\s*Class:\\s+ELF64\\s*$").containsMatchIn(elfHeader)) {
                "native artifact is not ELF64: ${row.artifact}"
            }
            check(Regex("(?m)^\\s*Machine:\\s+AArch64\\s*$").containsMatchIn(elfHeader)) {
                "native artifact is not AArch64: ${row.artifact}"
            }
            val symbols = runTool(
                llvmNm.get().asFile,
                listOf("-D", "--defined-only", artifact.absolutePath),
            )
            val actualJni = Regex("\\bJava_[A-Za-z0-9_]+\\b")
                .findAll(symbols)
                .map { match -> match.value }
                .toSet()
            check(actualJni == row.expectedJni) {
                "JNI export drift for ${row.artifact}: expected=${row.expectedJni.sorted()} actual=${actualJni.sorted()}"
            }
            totalJni += actualJni.size
        }
        logger.lifecycle(
            "NATIVE_ARTIFACTS=PASS COUNT={} ABI=arm64-v8a JNI={}",
            rows.size,
            totalJni,
        )
    }

    /** Runs one LLVM inspection command and returns stdout or fails with stderr. */
    private fun runTool(executable: java.io.File, arguments: List<String>): String {
        val stdout = ByteArrayOutputStream()
        val stderr = ByteArrayOutputStream()
        val result = execOps.exec {
            commandLine(listOf(executable.absolutePath) + arguments)
            standardOutput = stdout
            errorOutput = stderr
            isIgnoreExitValue = true
        }
        check(result.exitValue == 0) {
            "${executable.name} exited ${result.exitValue}: ${stderr.toString(Charsets.UTF_8.name())}"
        }
        return stdout.toString(Charsets.UTF_8.name())
    }
}

/** Parses every debug unit-test XML suite and enforces the frozen four buckets. */
abstract class VerifyUnitTestXmlTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.InputDirectory
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val resultsDirectory: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.tasks.Input
    abstract val expectedTests: org.gradle.api.provider.Property<Int>

    @get:org.gradle.api.tasks.Input
    abstract val expectedFailures: org.gradle.api.provider.Property<Int>

    @get:org.gradle.api.tasks.Input
    abstract val expectedErrors: org.gradle.api.provider.Property<Int>

    @get:org.gradle.api.tasks.Input
    abstract val expectedSkipped: org.gradle.api.provider.Property<Int>

    /** Securely parses all suites and checks paths, suite names, and totals. */
    @org.gradle.api.tasks.TaskAction
    fun verifyResults() {
        val root = resultsDirectory.get().asFile
        val xmlFiles = root.walkTopDown()
            .filter { file -> file.isFile && file.extension.equals("xml", ignoreCase = true) }
            .sortedBy { file -> file.relativeTo(root).invariantSeparatorsPath }
            .toList()
        check(xmlFiles.isNotEmpty()) { "no unit-test XML under ${root.absolutePath}" }

        val factory = javax.xml.parsers.DocumentBuilderFactory.newInstance().apply {
            setFeature("http://apache.org/xml/features/disallow-doctype-decl", true)
            setFeature("http://xml.org/sax/features/external-general-entities", false)
            setFeature("http://xml.org/sax/features/external-parameter-entities", false)
            setFeature("http://apache.org/xml/features/nonvalidating/load-external-dtd", false)
            isXIncludeAware = false
            setExpandEntityReferences(false)
        }
        val seenPaths = mutableSetOf<String>()
        val seenSuites = mutableSetOf<String>()
        var tests = 0
        var failures = 0
        var errors = 0
        var skipped = 0
        xmlFiles.forEach { xml ->
            val relative = xml.relativeTo(root).invariantSeparatorsPath
            check(seenPaths.add(relative)) { "duplicate XML relative path: $relative" }
            val document = xml.inputStream().use { input ->
                factory.newDocumentBuilder().parse(input)
            }
            val suite = document.documentElement
            check(suite.tagName == "testsuite") {
                "$relative root is ${suite.tagName}, not testsuite"
            }
            val suiteName = suite.getAttribute("name")
            check(suiteName.isNotBlank()) { "$relative has an empty suite name" }
            check(seenSuites.add(suiteName)) { "duplicate suite name: $suiteName" }

            fun exactBucket(name: String): Int {
                check(suite.hasAttribute(name)) { "$relative is missing $name" }
                val raw = suite.getAttribute(name)
                val value = raw.toIntOrNull()
                check(value != null && value >= 0) { "$relative has invalid $name=$raw" }
                return value
            }
            tests += exactBucket("tests")
            failures += exactBucket("failures")
            errors += exactBucket("errors")
            skipped += exactBucket("skipped")
        }
        check(tests == expectedTests.get()) {
            "tests=$tests expected=${expectedTests.get()}"
        }
        check(failures == expectedFailures.get()) {
            "failures=$failures expected=${expectedFailures.get()}"
        }
        check(errors == expectedErrors.get()) {
            "errors=$errors expected=${expectedErrors.get()}"
        }
        check(skipped == expectedSkipped.get()) {
            "skipped=$skipped expected=${expectedSkipped.get()}"
        }
        logger.lifecycle(
            "UNIT_TEST_XML=PASS FILES={} TESTS={} FAILURES={} ERRORS={} SKIPPED={}",
            xmlFiles.size,
            tests,
            failures,
            errors,
            skipped,
        )
    }
}

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
    ndkVersion = "30.0.16248370"
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
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        /* Direct-cleanup D1e: generated app BuildConfig is the single build-metadata authority. */
        buildConfigField("boolean", "IS_PR_BUILD", "false")
    }
    androidResources {
        localeFilters += listOf("en", "zh-rCN")
    }
    testOptions {
        unitTests.isIncludeAndroidResources = true
        /* Robolectric 4.17 needs these JDK module openings on Java 17+ test workers. */
        unitTests.all {
            it.jvmArgs(
                "--add-opens=java.base/java.lang=ALL-UNNAMED",
                "--add-opens=java.base/java.util=ALL-UNNAMED",
                "--add-opens=java.base/java.io=ALL-UNNAMED",
                "--add-opens=java.base/java.net=ALL-UNNAMED",
                "--add-opens=java.base/java.security=ALL-UNNAMED",
                "--add-opens=java.base/java.text=ALL-UNNAMED",
                "--add-opens=java.base/jdk.internal.access=ALL-UNNAMED",
                "--add-opens=java.desktop/java.awt.font=ALL-UNNAMED",
                "--add-opens=jdk.compiler/com.sun.tools.javac.api=ALL-UNNAMED",
            )
        }
    }
    sourceSets {
        named("main") {
            kotlin.directories.add(buildInfoSrc.get().asFile.absolutePath)
            /* LKM images are generated (never committed); AGP forbids Provider here. */
            assets.directories.add(lkmGeneratedAssets.get().asFile.absolutePath)
            /* A6-4 finishing script, copied from the repository source. */
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
            externalNativeBuild {
                cmake {
                    arguments +=
                        "-DGHOSTLOCK_NATIVE_ARTIFACT_MANIFEST=${debugNativeManifest.get().asFile.absolutePath}"
                }
            }
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
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
}

val llvmBinDirectory = androidComponents.sdkComponents.ndkDirectory.map { ndk ->
    val prebuiltRoot = ndk.asFile.resolve("toolchains/llvm/prebuilt")
    val candidates = prebuiltRoot.listFiles().orEmpty().filter { directory ->
        directory.isDirectory && listOf("llvm-readelf", "llvm-nm").all { name ->
            directory.resolve("bin/$name").isFile || directory.resolve("bin/$name.exe").isFile
        }
    }
    check(candidates.size == 1) {
        "expected exactly one NDK LLVM host prebuilt under $prebuiltRoot; " +
            "found ${candidates.map { directory -> directory.name }}"
    }
    candidates.single().resolve("bin")
}

/** Resolves one LLVM tool from AGP's own NDK Provider without a second NDK policy. */
fun ndkTool(name: String): org.gradle.api.provider.Provider<org.gradle.api.file.RegularFile> =
    layout.file(
        llvmBinDirectory.map { bin ->
            listOf(bin.resolve(name), bin.resolve("$name.exe"))
                .single { candidate -> candidate.isFile }
        },
    )

androidComponents {
    onVariants(selector().withBuildType("release")) {
        it.packaging.resources.excludes
            .add("**")
    }
    onVariants(selector().withName("debug")) { variant ->
        tasks.register<VerifyNativeArtifactsTask>(
            variant.computeTaskName("verify", "NativeArtifacts"),
        ) {
            mergedNativeLibs.set(variant.artifacts.get(SingleArtifact.MERGED_NATIVE_LIBS))
            manifestFile.set(debugNativeManifest)
            llvmReadelf.set(ndkTool("llvm-readelf"))
            llvmNm.set(ndkTool("llvm-nm"))
        }
    }
}

val verifyDebugUnitTestXml = tasks.register<VerifyUnitTestXmlTask>("verifyDebugUnitTestXml") {
    group = "verification"
    description = "Verifies all debug unit-test XML against exact four-bucket totals."
    resultsDirectory.set(layout.buildDirectory.dir("test-results/testDebugUnitTest"))
    expectedTests.set(96)
    expectedFailures.set(0)
    expectedErrors.set(0)
    expectedSkipped.set(0)
    dependsOn("testDebugUnitTest")
}

base {
    archivesName.set("$appName-v$appVersionName($gitVersionCode)")
}

kotlin {
    jvmToolchain(21)
    /* GhostLock dual-skin UI uses these Material3 experimental APIs. */
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

    /* Forward the M3 one-off write-back switch to the forked test JVM. */
    providers.systemProperty("glk.m3.writeback").orNull?.let { systemProperty("glk.m3.writeback", it) }
}

tasks.named("preBuild") {
    dependsOn(generateBuildInfo)
}


dependencies {
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.foundation:foundation:1.12.1")
    /* Material3：新 UI 的默认套件（另一个是 miuix，见 app/ui/kit）；本仓无 BOM，版本显式写。
     * 1.5.0-alpha22 不是随手挑的：miuix 0.9.4 依赖的就是这一版，声明更低会被冲突解析顶上来，
     * 而"声明与实际不符"比版本标签难看更糟；miuix 升到稳定 material3 时这两行一起抬。 */
    implementation("androidx.compose.material3:material3:1.5.0-beta02")
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    /* @Preview 的注解与预览渲染器（2026-10-17 用户要求加入 tooling.preview）：注解在 ui-tooling-preview，
     * 渲染器在 ui-tooling（只进 debug，不进 release APK）。 */
    implementation("androidx.compose.ui:ui-tooling-preview:1.12.1")
    debugImplementation("androidx.compose.ui:ui-tooling:1.12.1")
    implementation("com.typesafe:config:1.4.9")
    implementation("org.luaj:luaj-jse:3.0.1")
    implementation("org.apache.commons:commons-compress:1.28.0")
    implementation("dev.rikka.shizuku:api:13.1.5")
    implementation("dev.rikka.shizuku:provider:13.1.5")
    /* GhostLock Compose shell and renderer dependencies. */
    implementation("androidx.core:core-splashscreen:1.2.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.11.0")
    implementation("androidx.navigationevent:navigationevent-compose:1.2.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.12.0")
    implementation("org.tukaani:xz:1.12")
    implementation("org.commonmark:commonmark:0.32.0")
    implementation("org.commonmark:commonmark-ext-gfm-tables:0.32.0")
    implementation("org.commonmark:commonmark-ext-gfm-strikethrough:0.32.0")
    implementation("org.commonmark:commonmark-ext-autolink:0.32.0")
    implementation("org.commonmark:commonmark-ext-task-list-items:0.32.0")
    implementation("androidx.webkit:webkit:1.18.0")
    implementation("org.lsposed.hiddenapibypass:hiddenapibypass:6.2")
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
    androidTestImplementation("androidx.compose.ui:ui-test-junit4:1.12.1")
    debugImplementation("androidx.compose.ui:ui-test-manifest:1.12.1")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}
