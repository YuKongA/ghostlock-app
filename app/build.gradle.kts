@file:Suppress("UnstableApiUsage")

import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

val appName = "GhostLock"
val appVersionName = "1.1"

val gitVersionCode = runCatching {
    providers.exec {
        commandLine("git", "rev-list", "--count", "HEAD")
    }.standardOutput.asText.get().trim().toInt()
}.getOrElse {
    logger.warn("git rev-list failed (${it.message}); versionCode falls back to 1")
    1
}

val supportedKernelsSrc = layout.buildDirectory.dir("generated/source/supportedKernels")
val sharedOffsetsHeader = rootProject.file("src/kernels/offsets.h")
val offsetFieldRe = Regex("\\.([A-Za-z0-9_]+)\\s*=\\s*(0[xX][0-9A-Fa-f]+|-?\\d+)")

fun parseOffsetValue(text: String): Long = if (text.length > 2 && text.startsWith("0x", ignoreCase = true)) {
    text.substring(2).toLong(16)
} else {
    text.toLong()
}

fun formatOffsetValue(value: Long): String = if (value < 0) "${value}L" else "0x${value.toString(16)}L"

fun escapeKotlinString(value: String): String = value.replace("\\", "\\\\").replace("\"", "\\\"")

fun parseStructMacros(text: String): Map<String, Map<String, Long>> {
    val macros = mutableMapOf<String, Map<String, Long>>()
    val lines = text.lines()
    var index = 0
    while (index < lines.size) {
        val match = Regex("#define\\s+(STRUCT_OFFSETS_[A-Za-z0-9_]+)\\s*(.*)").matchEntire(lines[index])
        if (match == null) {
            index++
            continue
        }
        val name = match.groupValues[1]
        var body = match.groupValues[2]
        while (lines[index].trimEnd().endsWith("\\") && index + 1 < lines.size) {
            index++
            body += " ${lines[index]}"
        }
        macros[name] = offsetFieldRe.findAll(body).associate { it.groupValues[1] to parseOffsetValue(it.groupValues[2]) }
        index++
    }
    return macros
}

data class ParsedKernelEntries(val names: List<String>, val entries: Map<String, Map<String, Long>>)

fun parseKernelEntries(header: File, macros: Map<String, Map<String, Long>>): ParsedKernelEntries {
    val text = header.readText()
    val names = mutableListOf<String>()
    val entries = linkedMapOf<String, Map<String, Long>>()
    val matcher = Regex("OFFSETS_ENTRY\\(\\s*\"([^\"]+)\"").findAll(text)
    for (match in matcher) {
        val release = match.groupValues[1]
        val tail = text.substring(match.range.last + 1)
        val body = tail.substringBefore("\n),")
        val fields = linkedMapOf<String, Long>()
        Regex("STRUCT_OFFSETS_[A-Za-z0-9_]+").find(body)?.value?.let { macros[it]?.let(fields::putAll) }
        offsetFieldRe.findAll(body).forEach { fields[it.groupValues[1]] = parseOffsetValue(it.groupValues[2]) }
        names += release
        entries[release] = fields
    }
    return ParsedKernelEntries(names, entries)
}

tasks.register<GenerateSupportedKernelsTask>("generateSupportedKernels") {
    description = "generateSupportedKernels"
    offsetHeaders.from(fileTree(rootProject.projectDir) { include("src/kernels/*/offsets.h") })
    sharedHeader.set(rootProject.layout.projectDirectory.file("src/kernels/offsets.h"))
    generatedFile.set(supportedKernelsSrc.map { it.file("com/ghostlock/app/domain/model/SupportedKernels.kt") })
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
        minSdk = 34
        targetSdk = 37
        versionCode = gitVersionCode
        versionName = appVersionName
    }
    sourceSets {
        named("main") {
            kotlin.directories.add(supportedKernelsSrc.get().asFile.absolutePath)
        }
    }
    val properties = Properties()
    runCatching { properties.load(project.rootProject.file("local.properties").inputStream()) }
    val keystorePath = properties.getProperty("KEYSTORE_PATH") ?: System.getenv("KEYSTORE_PATH")
    val keystorePwd = properties.getProperty("KEYSTORE_PASS") ?: System.getenv("KEYSTORE_PASS")
    val alias = properties.getProperty("KEY_ALIAS") ?: System.getenv("KEY_ALIAS")
    val pwd = properties.getProperty("KEY_PASSWORD") ?: System.getenv("KEY_PASSWORD")
    if (keystorePath != null) {
        signingConfigs {
            create("release") {
                storeFile = file(keystorePath)
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
            signingConfig = signingConfigs.getByName(if (keystorePath != null) "release" else "debug")
        }
        debug {
            signingConfig = signingConfigs.getByName(if (keystorePath != null) "release" else "debug")
        }
    }
    buildFeatures {
        buildConfig = true
    }
    dependenciesInfo {
        includeInApk = false
        includeInBundle = false
    }
    packaging {
        jniLibs {
            useLegacyPackaging = true
            excludes += "lib/*/libandroidx.graphics.path.so"
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

tasks.named("preBuild") {
    dependsOn(rootProject.tasks.named("prepareGhostlockJniLibs"))
    dependsOn(rootProject.tasks.named("prepareGhostlockExtractJniLibs"))
    dependsOn(tasks.named("generateSupportedKernels"))
}

dependencies {
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.foundation:foundation:1.12.0")
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    implementation("top.yukonga.miuix.kmp:miuix-ui:0.9.4-rc01")
    implementation("top.yukonga.miuix.kmp:miuix-icons:0.9.4-rc01")
    implementation("top.yukonga.miuix.kmp:miuix-preference:0.9.4-rc01")
}
