import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

val appName = "GhostLock"
val appVersionName = "1.1"

val signingProperties = Properties()
val signingPropertiesFile = rootProject.file("local.properties")
if (signingPropertiesFile.isFile) {
    signingPropertiesFile.inputStream().use(signingProperties::load)
}
val keystorePath: String? = System.getenv("KEYSTORE_PATH") ?: signingProperties.getProperty("KEYSTORE_PATH")
val keystorePassword: String? = System.getenv("KEYSTORE_PASS") ?: signingProperties.getProperty("KEYSTORE_PASS")
val keyAlias: String? = System.getenv("KEY_ALIAS") ?: signingProperties.getProperty("KEY_ALIAS")
val keyPassword: String? = System.getenv("KEY_PASSWORD") ?: signingProperties.getProperty("KEY_PASSWORD")
val isPullRequestBuild = System.getenv("GITHUB_EVENT_NAME") == "pull_request" ||
    (System.getenv("GITHUB_REF") ?: "").startsWith("refs/pull/")

data class SigningValues(
    val keystorePath: String,
    val keystorePassword: String,
    val keyAlias: String,
    val keyPassword: String,
)

val releaseSigning = if (!isPullRequestBuild) {
    keystorePath?.let { path ->
        keystorePassword?.let { storePassword ->
            keyAlias?.let { alias ->
                keyPassword?.let { password ->
                    SigningValues(path, storePassword, alias, password)
                }
            }
        }
    }
} else {
    null
}
val hasReleaseSigning = releaseSigning != null

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

tasks.register("generateSupportedKernels") {
    description = "generateSupportedKernels"
    val offsets = fileTree(rootProject.projectDir) { include("src/kernels/*/offsets.h") }
    inputs.files(offsets)
    inputs.file(sharedOffsetsHeader)
    outputs.file(supportedKernelsSrc.map { it.file("com/ghostlock/app/domain/model/SupportedKernels.kt") })
    doLast {
        val macros = parseStructMacros(sharedOffsetsHeader.readText())
        val names = mutableListOf<String>()
        val builtins = linkedMapOf<String, Map<String, Long>>()
        offsets.forEach { header ->
            val parsed = parseKernelEntries(header, macros)
            names += parsed.names
            builtins.putAll(parsed.entries)
        }
        val output = buildString {
            appendLine("package com.ghostlock.app.domain.model")
            appendLine()
            appendLine("/** Generated from kernel offset headers; do not edit. */")
            appendLine("object SupportedKernels {")
            appendLine("    val UNAMES: Set<String> = setOf(")
            names.distinct().forEach { appendLine("        \"${escapeKotlinString(it)}\",") }
            appendLine("    )")
            appendLine()
            appendLine("    /** Built-in release -> field -> value (STRUCT_OFFSETS_* macros expanded). */")
            appendLine("    val BUILTIN: Map<String, Map<String, Long>> = mapOf(")
            builtins.forEach { (release, fields) ->
                appendLine("        \"${escapeKotlinString(release)}\" to mapOf(")
                fields.forEach { (key, value) -> appendLine("            \"${escapeKotlinString(key)}\" to ${formatOffsetValue(value)},") }
                appendLine("        ),")
            }
            appendLine("    )")
            appendLine("}")
        }
        val outputFile = supportedKernelsSrc.get().file("com/ghostlock/app/domain/model/SupportedKernels.kt").asFile
        outputFile.parentFile.mkdirs()
        outputFile.writeText(output)
    }
}

android {
    namespace = "com.ghostlock.app"
    compileSdk {
        version = release(37)
    }
    defaultConfig {
        applicationId = "com.ghostlock.app"
        minSdk = 34
        targetSdk = 37
        versionCode = gitVersionCode
        versionName = appVersionName
        ndk {
            //noinspection ChromeOsAbiSupport
            abiFilters += "arm64-v8a"
        }
    }
    if (releaseSigning != null) {
        signingConfigs {
            create("release") {
                storeFile = file(releaseSigning.keystorePath)
                storePassword = releaseSigning.keystorePassword
                keyAlias = releaseSigning.keyAlias
                keyPassword = releaseSigning.keyPassword
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }
    sourceSets {
        named("main") {
            kotlin.directories.add(supportedKernelsSrc.get().asFile.absolutePath)
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName(if (hasReleaseSigning) "release" else "debug")
        }
        debug {
            isMinifyEnabled = false
            if (hasReleaseSigning) signingConfig = signingConfigs.getByName("release")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

tasks.named("preBuild") {
    dependsOn(rootProject.tasks.named("prepareGhostlockJniLibs"))
    dependsOn(rootProject.tasks.named("prepareGhostlockExtractJniLibs"))
    dependsOn(tasks.named("generateSupportedKernels"))
}

base {
    archivesName.set("$appName-v$appVersionName($gitVersionCode)")
}

dependencies {
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-ktx:2.11.0")
    implementation("androidx.compose.ui:ui:1.12.0")
    implementation("androidx.compose.foundation:foundation:1.12.0")
    implementation("androidx.compose.animation:animation:1.12.0")
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    implementation("top.yukonga.miuix.kmp:miuix-ui:0.9.4-rc01")
    implementation("top.yukonga.miuix.kmp:miuix-icons:0.9.4-rc01")
    implementation("top.yukonga.miuix.kmp:miuix-preference:0.9.4-rc01")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20260814")
}
