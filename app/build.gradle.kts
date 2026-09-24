@file:Suppress("UnstableApiUsage")

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Keep this module's output under the repository-root build/ directory.
layout.buildDirectory.set(rootProject.layout.buildDirectory.dir("app"))

val appName = "GhostLock"
val appVersionName = "1.2"

val gitVersionCode = runCatching {
    providers.exec {
        commandLine("git", "rev-list", "--count", "HEAD")
    }.standardOutput.asText.get().trim().toInt()
}.getOrElse {
    logger.warn("git rev-list failed (${it.message}); versionCode falls back to 1")
    1
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
        minSdk = 34
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

tasks.named("preBuild") {
    dependsOn(rootProject.tasks.named("prepareGhostlockJniLibs"))
    dependsOn(rootProject.tasks.named("prepareGhostlockExtractJniLibs"))
    dependsOn(generateBuildInfo)
}

dependencies {
    implementation("dev.rikka.shizuku:api:13.1.5")
    implementation("dev.rikka.shizuku:provider:13.1.5")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.foundation:foundation:1.12.1")
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    implementation("top.yukonga.miuix.kmp:miuix-ui:0.9.4-rc01")
    implementation("top.yukonga.miuix.kmp:miuix-icons:0.9.4-rc01")
    implementation("top.yukonga.miuix.kmp:miuix-preference:0.9.4-rc01")
    implementation("org.apache.commons:commons-compress:1.26.0")
    implementation(project(":profile-core"))
    implementation("com.typesafe:config:1.4.3")

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.16")
}
