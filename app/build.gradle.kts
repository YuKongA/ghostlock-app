@file:Suppress("UnstableApiUsage")

import com.android.build.api.variant.HostTestBuilder
import com.android.build.api.artifact.SingleArtifact
import groovy.json.JsonOutput
import groovy.json.JsonSlurper
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.Properties
import java.util.concurrent.ConcurrentHashMap
import java.util.zip.ZipFile
import org.gradle.api.attributes.Category
import org.gradle.api.attributes.LibraryElements
import org.gradle.api.attributes.Usage
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.internal.tasks.testing.filter.DefaultTestFilter
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction

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

abstract class UnitTestXmlRuns : org.gradle.api.services.BuildService<org.gradle.api.services.BuildServiceParameters.None> {
    val started = ConcurrentHashMap<String, File>()
    val completed = ConcurrentHashMap<String, File>()
    val reported = ConcurrentHashMap<String, File>()
}

/** Turns the package producer's non-classpath artifact into APK seed assets. */
abstract class GenerateBuiltinPluginAssets : org.gradle.api.DefaultTask() {
    @get:InputFiles
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val pluginArchives: ConfigurableFileCollection

    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty

    @TaskAction
    fun generate() {
        val archive = pluginArchives.files.singleOrNull()
            ?: error("exactly one builtin .glplugin.zip artifact is required")
        require(archive.isFile && archive.name.endsWith(".glplugin.zip")) {
            "builtin plugin artifact must be a regular .glplugin.zip file"
        }

        val rawManifest = ZipFile(archive).use { zip ->
            val manifests = zip.entries().asSequence().filter { it.name == MANIFEST_PATH }.toList()
            require(manifests.size == 1 && !manifests.single().isDirectory) {
                "builtin plugin artifact must contain exactly one regular $MANIFEST_PATH"
            }
            ByteArrayOutputStream().also { output ->
                zip.getInputStream(manifests.single()).use { input -> input.copyTo(output, STREAM_BUFFER_BYTES) }
            }.toByteArray()
        }
        val manifestText = rawManifest.toString(Charsets.UTF_8)
        val parsed = JsonSlurper().parseText(manifestText) as? Map<*, *>
            ?: error("builtin plugin manifest must be a JSON object")
        val canonical = canonicalManifest(parsed)
        require(rawManifest.contentEquals(canonical)) {
            "package producer emitted a non-canonical builtin manifest"
        }
        val contentRoot = semanticContentRoot(canonical)

        val publisherId = parsed.requiredString("publisherId")
        val packageId = parsed.requiredString("packageId")
        val versionCode = parsed.requiredWholeNumber("versionCode")
        val variant = parsed.requiredString("variant")
        require(publisherId == "com.ghostlock.firstparty") {
            "builtin publisher differs from the first-party build policy"
        }
        require(packageId == "com.ghostlock.diagnose") {
            "unexpected builtin packageId=$packageId"
        }
        require(versionCode > 0L && variant == "android-arm64-v8a") {
            "builtin version or variant is invalid"
        }
        val components = parsed.requiredObjectList("components")
        require(components.size == 1 && components.single().requiredString("backendKey") == "DIAGNOSE") {
            "builtin package must expose exactly the DIAGNOSE backend"
        }

        val root = outputDirectory.get().asFile
        root.deleteRecursively()
        val pluginRoot = File(root, "ghostlock-plugins")
        val objectDirectory = File(pluginRoot, "objects").also(File::mkdirs)
        val assetPath = "ghostlock-plugins/objects/$contentRoot.glplugin.zip"
        archive.inputStream().use { input ->
            File(objectDirectory, "$contentRoot.glplugin.zip").outputStream().use { output ->
                input.copyTo(output, STREAM_BUFFER_BYTES)
            }
        }
        val index = linkedMapOf<String, Any?>(
            "schemaMajor" to 1,
            "schemaMinor" to 0,
            "records" to listOf(
                linkedMapOf<String, Any?>(
                    "publisherId" to publisherId,
                    "packageId" to packageId,
                    "versionCode" to versionCode,
                    "variant" to variant,
                    "assetPath" to assetPath,
                    "pluginContentSha256" to contentRoot,
                ),
            ),
            "defaultBackendKey" to "DIAGNOSE",
        )
        File(pluginRoot, "builtin-index.json").writeText(
            JsonOutput.toJson(index) + "\n",
            Charsets.UTF_8,
        )
    }

    private fun canonicalManifest(source: Map<*, *>): ByteArray {
        val expectedKeys = setOf(
            "formatMajor", "formatMinor", "publisherId", "packageId", "versionCode",
            "versionName", "variant", "minAndroidSdk", "hostApiMajor", "minHostApiMinor",
            "files", "nativeLibraries", "components",
        )
        require(source.keys == expectedKeys) { "builtin manifest fields differ from format 1.0" }
        val files = source.requiredObjectList("files").map { entry ->
            require(entry.keys == setOf("path", "sha256")) { "invalid builtin files[] entry" }
            linkedMapOf<String, Any?>(
                "path" to entry.requiredString("path"),
                "sha256" to entry.requiredString("sha256"),
            )
        }.sortedBy { it.getValue("path") as String }
        val nativeLibraries = source.requiredObjectList("nativeLibraries").map { entry ->
            require(
                entry.keys == setOf(
                    "path", "abi", "elfClass", "elfMachine", "soname", "logicalLoadName",
                    "neededLibraries",
                ),
            ) { "invalid builtin nativeLibraries[] entry" }
            linkedMapOf<String, Any?>(
                "path" to entry.requiredString("path"),
                "abi" to entry.requiredString("abi"),
                "elfClass" to entry.requiredWholeNumber("elfClass"),
                "elfMachine" to entry.requiredWholeNumber("elfMachine"),
                "soname" to entry.requiredNullableString("soname"),
                "logicalLoadName" to entry.requiredString("logicalLoadName"),
                "neededLibraries" to entry.requiredStringList("neededLibraries").sorted(),
            )
        }.sortedBy { it.getValue("path") as String }
        val components = source.requiredObjectList("components").map { entry ->
            require(
                entry.keys == setOf(
                    "kind", "id", "backendKey", "entryContract", "entryClass",
                    "selectionPolicy", "abortPolicy", "executionDomains", "runCapability",
                ),
            ) { "invalid builtin components[] entry" }
            linkedMapOf<String, Any?>(
                "kind" to entry.requiredString("kind"),
                "id" to entry.requiredString("id"),
                "backendKey" to entry.requiredString("backendKey"),
                "entryContract" to entry.requiredString("entryContract"),
                "entryClass" to entry.requiredString("entryClass"),
                "selectionPolicy" to entry.requiredString("selectionPolicy"),
                "abortPolicy" to entry.requiredString("abortPolicy"),
                "executionDomains" to entry.requiredStringList("executionDomains").sorted(),
                "runCapability" to entry.requiredString("runCapability"),
            )
        }.sortedBy { it.getValue("id") as String }
        return JsonOutput.toJson(
            linkedMapOf<String, Any?>(
                "formatMajor" to source.requiredWholeNumber("formatMajor"),
                "formatMinor" to source.requiredWholeNumber("formatMinor"),
                "publisherId" to source.requiredString("publisherId"),
                "packageId" to source.requiredString("packageId"),
                "versionCode" to source.requiredWholeNumber("versionCode"),
                "versionName" to source.requiredString("versionName"),
                "variant" to source.requiredString("variant"),
                "minAndroidSdk" to source.requiredWholeNumber("minAndroidSdk"),
                "hostApiMajor" to source.requiredWholeNumber("hostApiMajor"),
                "minHostApiMinor" to source.requiredWholeNumber("minHostApiMinor"),
                "files" to files,
                "nativeLibraries" to nativeLibraries,
                "components" to components,
            ),
        ).toByteArray(Charsets.UTF_8)
    }

    private fun semanticContentRoot(canonicalManifest: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-256")
        digest.update(CONTENT_DOMAIN.toByteArray(Charsets.UTF_8))
        digest.update(0)
        digest.update(canonicalManifest)
        return digest.digest().joinToString(separator = "") { byte ->
            "%02x".format(byte.toInt() and 0xff)
        }
    }

    private fun Map<*, *>.requiredString(key: String): String =
        requireNotNull(this[key] as? String) { "$key must be a string" }

    private fun Map<*, *>.requiredNullableString(key: String): String? {
        require(containsKey(key)) { "$key is missing" }
        val value = this[key]
        require(value == null || value is String) { "$key must be a string or null" }
        return value as String?
    }

    private fun Map<*, *>.requiredWholeNumber(key: String): Long {
        val value = requireNotNull(this[key] as? Number) { "$key must be a number" }
        val encoded = value.toString()
        require(encoded.matches(Regex("-?(?:0|[1-9][0-9]*)"))) { "$key must be an integer" }
        return encoded.toLong()
    }

    private fun Map<*, *>.requiredObjectList(key: String): List<Map<*, *>> =
        requireNotNull(this[key] as? List<*>) { "$key must be an array" }.map { value ->
            requireNotNull(value as? Map<*, *>) { "$key entries must be objects" }
        }

    private fun Map<*, *>.requiredStringList(key: String): List<String> =
        requireNotNull(this[key] as? List<*>) { "$key must be an array" }.map { value ->
            requireNotNull(value as? String) { "$key entries must be strings" }
        }

    private companion object {
        const val MANIFEST_PATH = "manifest.json"
        const val CONTENT_DOMAIN = "GhostLockPluginContent/1"
        const val STREAM_BUFFER_BYTES = 16 * 1024
    }
}

/** Verifies that a final APK embeds exactly the verified plugin asset and no plugin runtime code. */
abstract class VerifyBuiltinPluginBoundaryTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.InputDirectory
    abstract val apkDirectory: DirectoryProperty

    @get:org.gradle.api.tasks.InputDirectory
    abstract val expectedAssetsDirectory: DirectoryProperty

    @get:org.gradle.api.tasks.Input
    abstract val runtimeComponentOwners: org.gradle.api.provider.ListProperty<String>

    @TaskAction
    fun verifyBoundary() {
        val apks = apkDirectory.get().asFile.walkTopDown()
            .filter { it.isFile && it.extension.equals("apk", ignoreCase = true) }
            .toList()
        require(apks.size == 1) { "expected exactly one arm64 APK, found ${apks.map(File::getName)}" }
        val assetsRoot = expectedAssetsDirectory.get().asFile
        val expectedIndex = File(assetsRoot, INDEX_PATH)
        val expectedObjects = File(assetsRoot, OBJECTS_PATH).listFiles()
            .orEmpty()
            .filter { it.isFile && it.name.endsWith(".glplugin.zip") }
        require(expectedIndex.isFile && expectedObjects.size == 1) {
            "generated builtin assets must contain one index and one plugin object"
        }
        val expectedObject = expectedObjects.single()
        val index = JsonSlurper().parse(expectedIndex) as? Map<*, *>
            ?: error("builtin index must be a JSON object")
        require(index.keys == setOf("schemaMajor", "schemaMinor", "records", "defaultBackendKey")) {
            "builtin index fields differ from schema 1.0"
        }
        require((index["schemaMajor"] as? Number)?.toInt() == 1 &&
            (index["schemaMinor"] as? Number)?.toInt() == 0
        ) { "unsupported builtin index schema" }
        val records = index["records"] as? List<*> ?: error("builtin records must be an array")
        require(records.size == 1) { "builtin index must contain exactly one record" }
        val record = records.single() as? Map<*, *> ?: error("builtin record must be an object")
        val root = record["pluginContentSha256"] as? String ?: error("builtin root must be a string")
        val assetPath = record["assetPath"] as? String ?: error("builtin assetPath must be a string")
        require(root.matches(Regex("[0-9a-f]{64}")) &&
            assetPath == "$OBJECTS_PATH/$root.glplugin.zip" &&
            expectedObject.name == "$root.glplugin.zip" &&
            index["defaultBackendKey"] == "DIAGNOSE"
        ) { "builtin index/object identity mismatch" }

        ZipFile(apks.single()).use { apk ->
            val entries = apk.entries().asSequence().toList()
            val apkIndexPath = "$APK_ASSETS_PREFIX/$INDEX_PATH"
            val apkObjectsPath = "$APK_ASSETS_PREFIX/$OBJECTS_PATH"
            val indexEntries = entries.filter { it.name == apkIndexPath && !it.isDirectory }
            val pluginEntries = entries.filter { it.name.startsWith("$apkObjectsPath/") &&
                it.name.endsWith(".glplugin.zip") && !it.isDirectory
            }
            require(indexEntries.size == 1 && pluginEntries.size == 1) {
                "APK must contain exactly one builtin index and one plugin archive"
            }
            require(pluginEntries.single().name == "$APK_ASSETS_PREFIX/$assetPath") {
                "APK plugin object path differs from index"
            }
            apk.getInputStream(indexEntries.single()).use { actual ->
                expectedIndex.inputStream().use { expected -> requireSameBytes(actual, expected, INDEX_PATH) }
            }
            apk.getInputStream(pluginEntries.single()).use { actual ->
                expectedObject.inputStream().use { expected -> requireSameBytes(actual, expected, assetPath) }
            }
            require(entries.none { entry ->
                entry.name.matches(Regex("lib/[^/]+/(?:libghostlock_diagnose|libjnidispatch)\\.so"))
            }) { "APK native directory contains plugin-owned DSO" }
            val forbiddenDexDescriptors = listOf(
                "Lcom/ghostlock/plugin/diagnose/".toByteArray(Charsets.UTF_8),
                "Lcom/sun/jna/".toByteArray(Charsets.UTF_8),
            )
            entries.filter { entry ->
                !entry.isDirectory && entry.name.matches(Regex("classes(?:[2-9][0-9]*)?\\.dex"))
            }.forEach { dex ->
                apk.getInputStream(dex).use { input ->
                    require(!containsAny(input, forbiddenDexDescriptors)) {
                        "APK DEX contains plugin implementation/JNA class: ${dex.name}"
                    }
                }
            }
        }
        require(runtimeComponentOwners.get().none { owner ->
            owner.contains("project :plugin:packages:diagnose") ||
                owner.contains("net.java.dev.jna:jna")
        }) { "App runtime classpath contains Diagnose package or JNA" }
        logger.lifecycle("BUILTIN_PLUGIN_BOUNDARY=PASS APK={} ROOT={}", apks.single().name, root)
    }

    private fun requireSameBytes(
        actual: java.io.InputStream,
        expected: java.io.InputStream,
        label: String,
    ) {
        require(sha256(actual).contentEquals(sha256(expected))) { "$label bytes differ" }
    }

    private fun sha256(input: java.io.InputStream): ByteArray {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(STREAM_BUFFER_BYTES)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) return digest.digest()
            if (count > 0) digest.update(buffer, 0, count)
        }
    }

    private fun containsAny(input: java.io.InputStream, needles: List<ByteArray>): Boolean {
        val maxNeedle = needles.maxOf(ByteArray::size)
        var carry = ByteArray(0)
        val buffer = ByteArray(STREAM_BUFFER_BYTES)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) return false
            if (count == 0) continue
            val window = carry + buffer.copyOf(count)
            if (needles.any { needle -> window.hasSubsequence(needle) }) return true
            carry = window.takeLast(maxNeedle - 1).toByteArray()
        }
    }

    private fun ByteArray.hasSubsequence(needle: ByteArray): Boolean {
        if (needle.isEmpty() || needle.size > size) return false
        for (start in 0..size - needle.size) {
            var matches = true
            for (offset in needle.indices) {
                if (this[start + offset] != needle[offset]) {
                    matches = false
                    break
                }
            }
            if (matches) return true
        }
        return false
    }

    private companion object {
        const val INDEX_PATH = "ghostlock-plugins/builtin-index.json"
        const val OBJECTS_PATH = "ghostlock-plugins/objects"
        const val APK_ASSETS_PREFIX = "assets"
        const val STREAM_BUFFER_BYTES = 16 * 1024
    }
}

val unitTestXmlRuns = gradle.sharedServices.registerIfAbsent("unitTestXmlRuns", UnitTestXmlRuns::class) {}

/** Keeps producer freshness/full-scope proof here; Allure owns parsing, statistics and reports. */
abstract class UnitTestAllureReportTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.Input
    abstract val producerPath: org.gradle.api.provider.Property<String>

    @get:org.gradle.api.tasks.InputFile
    abstract val reportBridge: org.gradle.api.file.RegularFileProperty

    @get:org.gradle.api.tasks.OutputDirectory
    abstract val reportDirectory: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.services.ServiceReference("unitTestXmlRuns")
    abstract val runs: org.gradle.api.provider.Property<UnitTestXmlRuns>

    @get:javax.inject.Inject
    abstract val execOps: org.gradle.process.ExecOperations

    @org.gradle.api.tasks.TaskAction
    fun verifyResults() {
        reportDirectory.get().asFile.deleteRecursively()
        // The executing Test supplies its actual report location; no inferred success dependency.
        val root = runs.get().started[producerPath.get()] ?: error(
            "no current-run unit-test execution evidence for ${producerPath.get()}; old XML is not execution evidence",
        )
        val completed = runs.get().completed[producerPath.get()] == root.canonicalFile
        logger.lifecycle("UNIT_TEST_PRODUCER={} COMPLETED={}", producerPath.get(), completed)
        execOps.exec {
            commandLine(listOf("python3", reportBridge.get().asFile.absolutePath,
                "--scope", reportDirectory.get().asFile.name, "--output", reportDirectory.get().asFile.absolutePath,
                root.absolutePath) + (if (completed) emptyList() else listOf("--runner-failed")))
        }.assertNormalExitValue()
        runs.get().reported[producerPath.get()] = root.canonicalFile
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

val builtinGlPlugin = configurations.create("builtinGlPlugin") {
    isCanBeConsumed = false
    isCanBeResolved = true
    attributes {
        attribute(Usage.USAGE_ATTRIBUTE, objects.named("ghostlock-glplugin"))
        attribute(Category.CATEGORY_ATTRIBUTE, objects.named(Category.DOCUMENTATION))
        attribute(LibraryElements.LIBRARY_ELEMENTS_ATTRIBUTE, objects.named("glplugin-zip"))
    }
}
dependencies.add(builtinGlPlugin.name, project(":plugin:packages:diagnose"))

val builtinPluginAssetsDirectory = layout.buildDirectory.dir("generated/builtinPlugins/assets")
val generateBuiltinPluginAssets = tasks.register<GenerateBuiltinPluginAssets>(
    "generateBuiltinPluginAssets",
) {
    description = "Generate the APK builtin plugin index and semantic-root object asset."
    pluginArchives.from(builtinGlPlugin)
    outputDirectory.set(builtinPluginAssetsDirectory)
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
            excludes += setOf(
                "lib/armeabi/**",
                "lib/armeabi-v7a/**",
                "lib/mips/**",
                "lib/mips64/**",
                "lib/x86/**",
                "lib/x86_64/**",
            )
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
    /** Enables the release host-test component required by the R6 debug/release parity gate. */
    beforeVariants(selector().withBuildType("release")) { variantBuilder ->
        variantBuilder.hostTests[HostTestBuilder.UNIT_TEST_TYPE]?.enable = true
    }
    onVariants(selector().withBuildType("release")) {
        it.packaging.resources.excludes
            .add("**")
    }
    onVariants { variant ->
        requireNotNull(variant.sources.assets) {
            "Android variant ${variant.name} does not expose an assets source container"
        }.addGeneratedSourceDirectory(
            generateBuiltinPluginAssets,
            GenerateBuiltinPluginAssets::outputDirectory,
        )
        val capitalized = variant.name.replaceFirstChar { it.uppercase() }
        tasks.register<VerifyBuiltinPluginBoundaryTask>("verify${capitalized}BuiltinPluginBoundary") {
            group = "verification"
            description = "Verify ${variant.name} APK builtin plugin isolation and identity."
            apkDirectory.set(variant.artifacts.get(SingleArtifact.APK))
            expectedAssetsDirectory.set(
                generateBuiltinPluginAssets.flatMap(GenerateBuiltinPluginAssets::outputDirectory),
            )
            runtimeComponentOwners.set(providers.provider {
                configurations.getByName("${variant.name}RuntimeClasspath")
                    .incoming.resolutionResult.allComponents
                    .map { component -> component.id.displayName }
                    .sorted()
            })
            dependsOn(":plugin:packages:diagnose:verifyGlPluginZip")
        }
    }
}

listOf("Debug", "Release").forEach { variant ->
    val producerName = "test${variant}UnitTest"
    val producerIdentity = "$path:$producerName"
    val freshResultsRequired = objects.property<Boolean>().convention(false)
    val evidence = unitTestXmlRuns
    val allureOutput = rootProject.layout.buildDirectory.dir("reports/allure/android-${variant.lowercase()}")
    val report = tasks.register<UnitTestAllureReportTask>("report${variant}UnitTestAllure") {
        val producer = tasks.named<Test>(producerName)
        producerPath.set(producerIdentity)
        reportBridge.set(rootProject.layout.projectDirectory.file("tools/allure_report.py"))
        reportDirectory.set(allureOutput)
        onlyIf { freshResultsRequired.get() }
        outputs.upToDateWhen { false }
        dependsOn(providers.provider { if (freshResultsRequired.get()) listOf(":prepareAllure") else emptyList<String>() })
        mustRunAfter(producer)
    }
    tasks.withType<Test>().matching { it.name == producerName }.configureEach {
        // The existing verification entry reruns only its Test; direct Test retains caching.
        outputs.upToDateWhen { !freshResultsRequired.get() }
        // Finalizers run after a failed Test too; only the existing gate enables this report.
        finalizedBy(report)
        usesService(evidence)
        doFirst {
            evidence.get().completed.remove(path)
            evidence.get().started.remove(path)
            evidence.get().reported.remove(path)
            allureOutput.get().asFile.deleteRecursively()
            val test = this as Test
            if (freshResultsRequired.get()) {
                check(test.filter.includePatterns.isEmpty() && test.filter.excludePatterns.isEmpty() &&
                    // Gradle 9 exposes task-bound CLI filters only on this implementation.
                    (test.filter as DefaultTestFilter).commandLineIncludePatterns.isEmpty() &&
                    test.includes.isEmpty() && test.excludes.isEmpty()) {
                    "full-variant verification requires an unfiltered Test; use $path directly for selected tests"
                }
            }
            val directory = (this as Test).reports.junitXml.outputLocation.get().asFile
            directory.walkTopDown().filter { it.isFile && it.extension.equals("xml", ignoreCase = true) }
                .forEach { Files.delete(it.toPath()) }
            evidence.get().started[path] = directory.canonicalFile
        }
        doLast {
            evidence.get().completed[path] =
                (this as Test).reports.junitXml.outputLocation.get().asFile.canonicalFile
        }
    }
    tasks.register("verify${variant}UnitTestXml") {
        group = "verification"
        description = "Verifies current full $variant unit tests and creates the Allure human/agent report."
        val producer = tasks.named<Test>(producerName)
        freshResultsRequired.set(true)
        dependsOn(producer)
        // A success-dependency on the finalizer would cancel it when Test fails in Gradle 9.
        mustRunAfter(report)
        usesService(evidence)
        doLast {
            if (!evidence.get().reported.containsKey(producerIdentity)) {
                allureOutput.get().asFile.deleteRecursively()
                error("missing current-run Allure verification for $producerIdentity")
            }
        }
    }
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

    dependsOn(generateBuiltinPluginAssets)
    inputs.dir(builtinPluginAssetsDirectory)
    systemProperty("ghostlock.builtinPluginAssets", builtinPluginAssetsDirectory.get().asFile.absolutePath)

    /* Forward the M3 one-off write-back switch to the forked test JVM. */
    providers.systemProperty("glk.m3.writeback").orNull?.let { systemProperty("glk.m3.writeback", it) }
}

tasks.named("preBuild") {
    dependsOn(generateBuildInfo)
}


dependencies {
    implementation(project(":plugin:api"))
    implementation(project(":plugin:core"))
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
    implementation("androidx.datastore:datastore:1.3.0")
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
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.11.0")
    testImplementation("org.robolectric:robolectric:4.17")
    /* Compose UI 测试（JVM/Robolectric）：版本与本仓 compose foundation 同源（1.12.1），
     * 无 BOM、无 version catalog ⇒ 必须显式版本。ui-test-manifest 未确认必需，暂不加。 */
    testImplementation("androidx.compose.ui:ui-test-junit4:1.12.1")
    androidTestImplementation("androidx.compose.ui:ui-test-junit4:1.12.1")
    debugImplementation("androidx.compose.ui:ui-test-manifest:1.12.1")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}
