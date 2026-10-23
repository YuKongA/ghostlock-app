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
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
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
    /* KernelSU 管理器 UI 移植（2026-10-18）：依赖与上游 manager/app 对齐。 */
    implementation("androidx.core:core-splashscreen:1.2.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.11.0")
    implementation("androidx.navigationevent:navigationevent-compose:1.2.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.11.0")
    implementation("com.github.topjohnwu.libsu:core:6.0.0")
    implementation("com.github.topjohnwu.libsu:service:6.0.0")
    implementation("com.github.topjohnwu.libsu:io:6.0.0")
    implementation("dev.rikka.rikkax.parcelablelist:parcelablelist:2.0.1")
    implementation("org.tukaani:xz:1.12")
    implementation("org.commonmark:commonmark:0.30.1")
    implementation("org.commonmark:commonmark-ext-gfm-tables:0.30.1")
    implementation("org.commonmark:commonmark-ext-gfm-strikethrough:0.30.1")
    implementation("org.commonmark:commonmark-ext-autolink:0.30.1")
    implementation("org.commonmark:commonmark-ext-task-list-items:0.30.1")
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
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}
