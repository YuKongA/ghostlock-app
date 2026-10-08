import java.nio.file.Files
import java.util.Properties

plugins {
    id("com.android.application") version "9.1.0" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20" apply false
    id("org.jetbrains.kotlin.jvm") version "2.4.20" apply false
}

/*
 * Gradle outputs stay INSIDE the repository, under each module build/
 * directory (the Gradle default: <module>/build/, plus the root build/).
 *
 * The build directory used to be redirected to an external root under the
 * user home. That is no longer allowed: builds and caches must live under
 * build/ only, so the layout below is deliberately the default one and
 * nothing is redirected.
 *
 * NOTE (macOS File Provider): this checkout can sit in an iCloud Drive
 * container whose sync daemons materialise conflict copies inside build
 * trees. If that reappears, keep everything under build/ and make the
 * build directories non-syncing (a .nosync marker) instead of redirecting
 * outputs out of the repository.
 *
 * The Make/NDK artifacts still land in build/ (see src/Makefile), and the
 * root clean removes every module build/ directory.
 */

private fun localProperties(): Properties = Properties().also { properties ->
    val propertiesFile = rootProject.file("local.properties")
    if (propertiesFile.isFile) {
        propertiesFile.inputStream().use(properties::load)
    }
}

private fun ondkHome(): String? =
    System.getenv("ONDK_HOME")?.takeIf(String::isNotBlank)
        ?: localProperties().getProperty("ondk.dir")?.takeIf(String::isNotBlank)

private fun useOndk(): Boolean = !ondkHome().isNullOrBlank()

private fun resolveNdkDir(): String {
    val ondk = ondkHome()
    if (ondk != null) return ondk

    val properties = localProperties()
    val ndkEnvironment = System.getenv("ANDROID_NDK_HOME")
        ?: System.getenv("ANDROID_NDK_ROOT")
    if (!ndkEnvironment.isNullOrBlank()) return ndkEnvironment

    properties.getProperty("ndk.dir")?.takeIf(String::isNotBlank)?.let { return it }

    val sdkDir = properties.getProperty("sdk.dir") ?: System.getenv("ANDROID_HOME")
    if (!sdkDir.isNullOrBlank()) {
        val ndkRoot = File(sdkDir, "ndk")
        val versions = ndkRoot.listFiles()
            ?.filter(File::isDirectory)
            ?.map(File::getName)
            ?.sorted()
            .orEmpty()
        if (versions.isNotEmpty()) return File(ndkRoot, versions.last()).absolutePath
    }

    throw GradleException("NDK not found; set ANDROID_NDK_HOME or ndk.dir in local.properties")
}

private data class NdkTools(val clang: String, val ar: String)

private fun resolveCargoExecutable(): String {
    val cargoOnPath = System.getenv("PATH")
        .orEmpty()
        .split(File.pathSeparator)
        .asSequence()
        .map { File(it, "cargo") }
        .firstOrNull { it.isFile && it.canExecute() }
    if (cargoOnPath != null) return cargoOnPath.absolutePath

    val cargoInRustupHome = File(System.getProperty("user.home"), ".cargo/bin/cargo")
    return if (cargoInRustupHome.isFile && cargoInRustupHome.canExecute()) {
        cargoInRustupHome.absolutePath
    } else {
        "cargo"
    }
}

private fun extractNdkTools(): NdkTools {
    val ndk = resolveNdkDir()
    val osName = System.getProperty("os.name").lowercase()
    val isWindows = osName.contains("windows")
    val prebuilt = when {
        isWindows -> "windows-x86_64"
        osName.contains("mac") -> "darwin-x86_64"
        else -> "linux-x86_64"
    }
    val binDir = File(ndk, "toolchains/llvm/prebuilt/$prebuilt/bin")
    return NdkTools(
        clang = File(
            binDir,
            if (isWindows) "aarch64-linux-android34-clang.cmd" else "aarch64-linux-android34-clang",
        ).absolutePath,
        ar = File(binDir, if (isWindows) "llvm-ar.exe" else "llvm-ar").absolutePath,
    )
}

// Every module's output lives under the root build/ directory (native,
// host-test, extract, profiles, app). Delete the whole tree here so a
// single root `clean` resets all of them.
tasks.register<Delete>("clean") {
    description = "Delete every module build/ directory."
    delete(layout.buildDirectory)
    subprojects.forEach { delete(it.layout.buildDirectory) }
}

/**
 * 清理 build/ 下由文件同步工具（iCloud Drive / Finder）产生的污染副本。
 *
 * 目的：替代整树 clean —— 后者会连增量缓存一起删掉；本任务只删除可证明来自
 * 同步冲突的副本，保留一切真实构建产物（例如 D8 的 x 2.dex / x 2.globals 中间物）。
 *
 * 删除判据（满足其一，缺一不删）：
 *   1) 形如 <base> <n>.<ext> 且同目录存在 <base>.<ext>（Finder/iCloud 冲突副本特征）；
 *   2) 文件名含 的冲突副本 / conflicted copy / conflict 显式标记。
 *
 * 安全性（fail-closed）：只在根 buildDirectory（仓库内 build/）之内遍历与删除；
 * 不跟随符号链接；任何越界路径直接抛错而不删除。
 *
 * 输出：POLLUTION deleted=<n> matched=<n> scanned=<n> 一行，外加删除样本。
 */
val pollutionRootPath = layout.buildDirectory.get().asFile.absolutePath

tasks.register("cleanBuildPollution") {
    group = "build"
    description = "删除 build/ 下的同步冲突副本（保留真实构建产物；取代整树 clean）。"
    val rootPath = pollutionRootPath
    doLast {
        val root = File(rootPath)
        if (!root.isDirectory) {
            logger.lifecycle("POLLUTION deleted=0 matched=0 scanned=0 (no build dir)")
            return@doLast
        }
        val rootCanonical = root.canonicalPath + File.separator
        val numericSuffix = Regex("^(.+) ([0-9]+)(\\.[^.]*)?$")
        val explicitMarkers = listOf("的冲突副本", "conflicted copy", "conflict")
        var scanned = 0
        var matched = 0
        var deleted = 0
        val samples = mutableListOf<String>()
        root.walkTopDown().onEnter { dir -> !Files.isSymbolicLink(dir.toPath()) }.forEach { f ->
            if (!f.isFile) return@forEach
            if (Files.isSymbolicLink(f.toPath())) return@forEach
            scanned++
            val name = f.name
            val m = numericSuffix.matchEntire(name)
            val bySibling = m != null && File(f.parentFile, m.groupValues[1] + m.groupValues[3]).isFile
            val byMarker = explicitMarkers.any { name.contains(it) }
            if (!bySibling && !byMarker) return@forEach
            matched++
            val canonical = f.canonicalPath
            if (!canonical.startsWith(rootCanonical)) {
                throw GradleException("refusing to delete outside build/: " + canonical)
            }
            if (f.delete()) {
                deleted++
                if (samples.size < 10) samples.add(f.relativeTo(root).path)
            } else {
                logger.warn("POLLUTION failed-to-delete " + f.relativeTo(root).path)
            }
        }
        logger.lifecycle("POLLUTION deleted=" + deleted + " matched=" + matched + " scanned=" + scanned)
        samples.forEach { logger.lifecycle("POLLUTION sample " + it) }
    }
}


tasks.register<Exec>("buildGhostlockNative") {
    description = "buildGhostlockNative"
    workingDir(file("src"))
    commandLine("make", "ghostlock")
    val ndk = resolveNdkDir()
    environment("ANDROID_NDK_HOME", ndk)
    environment("NDK_ROOT", ndk)
    inputs.files(
        fileTree("src") { include("**/*.c", "**/*.h", "**/*.cpp", "**/*.hpp") },
        file("src/Makefile"),
    )
    outputs.file(file("build/native/ghostlock"))
}

tasks.register<Copy>("prepareGhostlockJniLibs") {
    description = "prepareGhostlockJniLibs"
    dependsOn("buildGhostlockNative")
    from("build/native/ghostlock")
    into("app/src/main/jniLibs/arm64-v8a")
    rename { "libghostlock.so" }
    /* Strip only the packaged copy: static libc++ carries its DWARF into the
     * binary, while the top-level ghostlock keeps its symbols for the
     * disassembly comparisons. Paths are captured as plain strings so the
     * configuration cache can serialize this task. */
    val stripPath = File(extractNdkTools().clang)
        .resolveSibling("llvm-strip").absolutePath
    val packagedPath = File(rootDir, "app/src/main/jniLibs/arm64-v8a/libghostlock.so").absolutePath
    doLast {
        val code = ProcessBuilder(stripPath, "--strip-all", packagedPath)
            .inheritIO()
            .start()
            .waitFor()
        check(code == 0) { "llvm-strip failed with $code" }
    }
}

tasks.register<Exec>("buildGhostlockExtract") {
    description = "buildGhostlockExtract"
    val tools = extractNdkTools()
    val isOndk = useOndk()
    val command = mutableListOf(resolveCargoExecutable())
    if (isOndk) command += "+ondk"
    command += listOf("build", "--release", "--target", "aarch64-linux-android")
    if (isOndk) {
        command += listOf("-Z", "build-std=std,panic_abort")
        command += listOf("-Z", "build-std-features=optimize_for_size")
    }
    workingDir(rootProject.file("tools/extract_rs"))
    commandLine(command)
    environment("CC_aarch64_linux_android", tools.clang)
    environment("AR_aarch64_linux_android", tools.ar)
    environment("CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER", tools.clang)
    environment("RUSTFLAGS", "-C force-unwind-tables=no -C link-arg=-Wl,--icf=all")
    if (isOndk) environment("RUSTC_BOOTSTRAP", "1")
    inputs.files(
        fileTree("tools/extract_rs/src") { include("**/*.rs") },
        file("tools/extract_rs/Cargo.toml"),
        file("tools/extract_rs/Cargo.lock"),
    )
    inputs.property("useOndk", isOndk)
    outputs.file(file("build/extract/aarch64-linux-android/release/ghostlock-extract"))
}

tasks.register<Copy>("prepareGhostlockExtractJniLibs") {
    description = "prepareGhostlockExtractJniLibs"
    dependsOn("buildGhostlockExtract")
    from("build/extract/aarch64-linux-android/release/ghostlock-extract")
    into("app/src/main/jniLibs/arm64-v8a")
    rename { "libextract.so" }
}
