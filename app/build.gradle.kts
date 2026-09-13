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

tasks.register<GenerateSupportedKernelsTask>("generateSupportedKernels") {
    description = "generateSupportedKernels"
    profilesDirectory.set(layout.projectDirectory.dir("src/main/assets/kernel_profiles"))
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
    androidResources {
        localeFilters += listOf("en", "zh")
    }
    sourceSets {
        named("main") {
            kotlin.directories.add(supportedKernelsSrc.get().asFile.absolutePath)
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
    dependsOn(tasks.named("generateSupportedKernels"))
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
}
