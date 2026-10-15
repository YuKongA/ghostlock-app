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
 * The Make/NDK artifacts still land in build/ (see core/Makefile), and the
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

/**
 * 找一个可执行工具：先看 -P<NAME>= 属性，再看同名环境变量，再扫 PATH 与 Homebrew 的常见位置。
 *
 * 为什么需要它：Gradle 守护进程的 PATH 往往不含 /opt/homebrew/bin（本机 cmake/ctest/ninja 都在那里），
 * 直接 exec("cmake") 会以 "A problem occurred starting process" 失败（2026-10-14 实测）。
 */
private fun resolveTool(name: String): String {
    providers.gradleProperty(name).orNull?.takeIf(String::isNotBlank)?.let { return it }
    System.getenv(name)?.takeIf(String::isNotBlank)?.let { return it }
    val onPath = System.getenv("PATH").orEmpty().split(File.pathSeparator)
        .asSequence().map { File(it, name) }.firstOrNull { it.isFile && it.canExecute() }
    if (onPath != null) return onPath.absolutePath
    for (dir in listOf("/opt/homebrew/bin", "/usr/local/bin", "/usr/bin")) {
        val candidate = File(dir, name)
        if (candidate.isFile && candidate.canExecute()) return candidate.absolutePath
    }
    return name
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


/* ---------------------------------------------------------------------------
 * Native 构建（CMake）与测试（CTest）：Gradle 是唯一入口。
 *
 * 取代旧 Makefile 路径（make -C core host/check/android，Makefile 已删除）以及 root 里那两条
 * 指向早已不存在的 <repo>/src 的任务。分工：CMake 负责编译与注册测试，Gradle 负责"谁跑、
 * 跑哪些、什么时候"（见 docs/analysis/jni-two-backend-plan.md）。
 *
 * 目录（全部在仓库 build/ 下）：
 *   build/cmake/host     宿主构建树（Ninja）      build/host     宿主产物（ghostlock + 各 *_test）
 *   build/cmake/android  NDK 构建树               build/native   设备产物（ghostlock + libghostlock_jni.so）
 * ------------------------------------------------------------------------- */

private val glCmake = resolveTool("cmake")
private val glCtest = resolveTool("ctest")
private val glNinja = resolveTool("ninja")
private val glCmakeCommon = listOf("-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DCMAKE_MAKE_PROGRAM=" + glNinja)

tasks.register<Exec>("nativeConfigureHost") {
    description = "配置宿主 CMake 构建树（GL_TESTS=ON，注册全部 CTest）"
    group = "native"
    workingDir(rootDir)
    commandLine(listOf(glCmake, "-S", "core", "-B", "build/cmake/host") + glCmakeCommon +
        listOf("-DGL_TESTS=ON", "-DGL_JNI=OFF"))
    inputs.files(fileTree("core/src"), file("core/CMakeLists.txt"))
    outputs.dir(layout.buildDirectory.dir("cmake/host"))
}

tasks.register<Exec>("nativeBuildHost") {
    description = "构建宿主产物（ghostlock + 全部测试二进制）"
    group = "native"
    dependsOn("nativeConfigureHost")
    workingDir(rootDir)
    commandLine(glCmake, "--build", "build/cmake/host", "-j")
    outputs.dir(layout.buildDirectory.dir("host"))
}

tasks.register<Exec>("nativeTestHost") {
    description = "跑全部原生测试（CTest：111 个用例/证据测试 + 10 项结构性检查）"
    group = "verification"
    dependsOn("nativeBuildHost")
    workingDir(rootDir)
    commandLine(glCtest, "--test-dir", "build/cmake/host", "--output-on-failure")
}

/* 这两个任务把 NDK 路径与命令行**在配置期**算好（只捕获 String）：早前放在 doFirst 里会引用脚本对象，
 * 配置缓存直接拒绝序列化（2026-10-14 实测：:nativeConfigureAndroid / :nativeBuildAndroid 无法缓存）。 */
val androidTools: Triple<String, String, String> by lazy {
    val ndk = resolveNdkDir()
    Triple(ndk, glCmake, glNinja)
}

tasks.register<Exec>("nativeConfigureAndroid") {
    description = "配置 Android（NDK）CMake 构建树"
    group = "native"
    workingDir(rootDir)
    val tools = androidTools
    commandLine(listOf(tools.second, "-S", "core", "-B", "build/cmake/android", "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_MAKE_PROGRAM=" + tools.third,
        "-DCMAKE_TOOLCHAIN_FILE=" + tools.first + "/build/cmake/android.toolchain.cmake",
        "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-29",
        "-DGL_TESTS=OFF", "-DGL_JNI=ON"))
    environment("ANDROID_NDK_HOME", tools.first)
    environment("NDK_ROOT", tools.first)
    inputs.files(fileTree("core/src"), file("core/CMakeLists.txt"))
    outputs.dir(layout.buildDirectory.dir("cmake/android"))
}

tasks.register<Exec>("nativeBuildAndroid") {
    description = "构建设备产物（build/native/ghostlock + libghostlock_jni.so）"
    group = "native"
    dependsOn("nativeConfigureAndroid")
    workingDir(rootDir)
    val tools = androidTools
    commandLine(tools.second, "--build", "build/cmake/android", "-j")
    environment("ANDROID_NDK_HOME", tools.first)
    environment("NDK_ROOT", tools.first)
    /* 输入必须声明：不声明 Gradle 就只看 outputs ⇒ 源码改了任务仍判 UP-TO-DATE，
     * APK 里装的是旧 .so（2026-10-14 实测：门禁里 image 操作全静默，根因就是这条）。 */
    inputs.files(fileTree("core/src"), file("core/CMakeLists.txt"))
    outputs.file(file("build/native/ghostlock"))
    outputs.file(file("build/native/libghostlock_jni.so"))
}

/* JNI 边界库就位：Kotlin 侧 Native.LIBRARY == "ghostlock_jni" ⇒ 打包名必须是
 * libghostlock_jni.so（旧任务复制成 libghostlock.so，与实际加载名对不上）。 */
tasks.register<Copy>("prepareGhostlockJniLibs") {
    description = "把 libghostlock_jni.so 复制进 app 的 jniLibs/arm64-v8a"
    group = "native"
    dependsOn("nativeBuildAndroid")
    from("build/native/libghostlock_jni.so")
    into("app/src/main/jniLibs/arm64-v8a")
}

/**
 * 把 JNI 边界协议表导出到 app/build：Kotlin 侧的对拍测试（NativeProtocolTest）读它，
 * 因此 op/key/status 三张表在两个端之间不可能各写一份（跨端列表不得手抄）。
 */
tasks.register("exportJniProtocolManifest") {
    description = "导出 JNI 协议表到 app/build/jni-protocol-manifest.tsv"
    group = "verification"
    dependsOn("nativeBuildHost")
    val tool = File(rootDir, "build/host/jni_protocol_manifest")
    val out = File(rootDir, "app/build/jni-protocol-manifest.tsv")
    inputs.file(tool)
    outputs.file(out)
    doLast {
        out.parentFile.mkdirs()
        val process = ProcessBuilder(tool.absolutePath).redirectErrorStream(true).start()
        val text = process.inputStream.readBytes()
        check(process.waitFor() == 0) { "jni_protocol_manifest failed" }
        out.writeBytes(text)
        logger.lifecycle("exportJniProtocolManifest: " + out.length() + " bytes -> " + out.path)
    }
}

/* 应用单测必须先看到那份导出：协议表是跨端契约，测试不允许在缺它的前提下跑过。 */
tasks.matching { it.name == "testDebugUnitTest" }.configureEach {
    dependsOn("exportJniProtocolManifest")
}
tasks.register("checkAll") {
    description = "集中测试入口：原生 CTest + Kotlin 单测"
    group = "verification"
    dependsOn("nativeTestHost", ":app:testDebugUnitTest")
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

/* ---------------------------------------------------------------------------
 * markdownlint（docs 渲染级检查）
 *
 * 目的：用 markdownlint-cli2 约束**渲染相关**的 Markdown 规则（表格列数/空行、
 * 列表与围栏空行、引用块空行等）。规则与范围见根目录 .markdownlint.jsonc 与
 * .markdownlint-cli2.jsonc（docs/archive 目录为历史快照，显式豁免）。
 *
 * 候选运行器（按序尝试；只用 JVM/进程 API，不依赖 shell）：
 *   1) 项目本地 node_modules/markdownlint-cli2 的 bin 入口（npm ci 后离线可用）；
 *   2) PATH 上的 npx --no-install markdownlint-cli2（npx 缓存命中时可用）；
 *   3) -PmarkdownlintAllowInstall=true 时先 npm install（需网络），再走 1)。
 *
 * 判定表（**防假绿**，不依赖易抖的 --version 预探针）：
 *   横幅出现 + exit 0      ⇒ 通过；
 *   横幅出现 + exit ≠ 0    ⇒ lint 命中问题 ⇒ 构建失败（红）；
 *   横幅缺失（无论 exit）  ⇒ 工具没跑起来或入口错（历史事故：库入口 .mjs 静默 exit 0）
 *                            ⇒ 显式 SKIP，任务状态 = SKIPPED（不是 SUCCESS）；
 *   -PmarkdownlintStrict=true ⇒ SKIP 直接失败（CI 硬门禁）。
 *
 * 用法：
 *   ./gradlew markdownlint        # 检查
 *   ./gradlew markdownlintFix     # 自动修复可修项（--fix）
 *   MARKDOWNLINT_NODE=<path>      # 指定 node 可执行文件
 *
 * 实现说明：写成**类型化 DefaultTask**（而非脚本闭包），以兼容
 * org.gradle.configuration-cache=true（脚本对象不可序列化）。
 */
abstract class MarkdownlintCli2Task : DefaultTask() {
    @get:Input
    abstract val fixMode: Property<Boolean>

    @get:Input
    abstract val strictMode: Property<Boolean>

    @get:Input
    abstract val allowInstall: Property<Boolean>

    @get:Optional
    @get:Input
    abstract val nodeExecutable: Property<String>

    @get:Internal
    abstract val repoDir: DirectoryProperty

    @TaskAction
    fun lint() {
        val pin = "0.23.3"
        val root = repoDir.get().asFile
        val isWindows = System.getProperty("os.name").lowercase().contains("windows")
        val node = nodeExecutable.orNull?.takeIf(String::isNotBlank) ?: "node"

        data class Probe(val ok: Boolean, val output: String)

        fun run(command: List<String>): Probe =
            try {
                val process = ProcessBuilder(command).directory(root).redirectErrorStream(true).start()
                val text = process.inputStream.readBytes().toString(Charsets.UTF_8)
                val code = process.waitFor()
                logger.lifecycle(text.trimEnd())
                Probe(code == 0, text.trim())
            } catch (e: Exception) {
                Probe(false, e.message ?: e.toString())
            }

        fun giveUp(reason: String): Nothing {
            logger.lifecycle("SKIP markdownlint: " + reason)
            logger.lifecycle(
                "SKIP markdownlint: 本次没有执行任何 Markdown 检查 —— 任务结果为 SKIPPED，" +
                    "不代表 lint 通过（不假绿）；需要硬门禁请加 -PmarkdownlintStrict=true",
            )
            if (strictMode.get()) throw GradleException("markdownlint SKIPPED: " + reason)
            throw StopExecutionException("markdownlint skipped: " + reason)
        }

        val nodeProbe = run(listOf(node, "--version"))
        if (!nodeProbe.ok) {
            giveUp("找不到可用的 node（" + nodeProbe.output.take(200) + "）；设 MARKDOWNLINT_NODE=<path> 或安装 Node.js")
        }

        // 本地安装的 **bin** 入口（不是库入口 markdownlint-cli2.mjs —— 后者静默 exit 0）。
        fun localBin(): File? {
            val pkgDir = File(root, "node_modules/markdownlint-cli2")
            val pkgJson = File(pkgDir, "package.json")
            val fromPkg = if (pkgJson.isFile) {
                val match = Regex("\"markdownlint-cli2\"\\s*:\\s*\"([^\"]+)\"").find(pkgJson.readText())
                match?.groupValues?.get(1)?.let { File(pkgDir, it) }
            } else {
                null
            }
            val fallback = File(pkgDir, "markdownlint-cli2-bin.mjs")
            return listOfNotNull(fromPkg, fallback).firstOrNull { it.isFile }
        }

        val npx = if (isWindows) "npx.cmd" else "npx"
        val candidates = mutableListOf<List<String>>()
        localBin()?.let { candidates += listOf(node, it.absolutePath) }
        candidates += listOf(npx, "--no-install", "markdownlint-cli2")

        if (allowInstall.get() && localBin() == null) {
            val npm = if (isWindows) "npm.cmd" else "npm"
            logger.lifecycle("markdownlint: 本地未安装，执行 npm install（需网络）")
            run(listOf(npm, "install", "--no-audit", "--no-fund"))
            localBin()?.let { candidates.add(0, listOf(node, it.absolutePath)) }
        }

        val args = mutableListOf("--config", ".markdownlint-cli2.jsonc")
        if (fixMode.get()) args += "--fix"

        var lastReason = "没有任何候选运行器"
        var ran = false
        for (candidate in candidates) {
            val command = candidate + args
            logger.lifecycle("markdownlint: " + command.joinToString(" "))
            val result = run(command)
            if (!result.output.contains("markdownlint-cli2", ignoreCase = true)) {
                lastReason = "运行器未产出工具横幅（" + command.joinToString(" ") + "）：" + result.output.take(200)
                continue
            }
            ran = true
            if (!result.ok) {
                throw GradleException("markdownlint 失败：见上方 file:line:rule 输出")
            }
            break
        }
        if (!ran) {
            giveUp(
                "markdownlint-cli2 未能运行（" + lastReason + "）；" +
                    "修复：npm ci（或 -PmarkdownlintAllowInstall=true 联网安装 markdownlint-cli2@" + pin + "）",
            )
        }
    }
}

tasks.register<MarkdownlintCli2Task>("markdownlint") {
    group = "verification"
    description = "markdownlint-cli2 检查 Markdown 渲染级问题；无法运行时显式 SKIP（不假绿）。"
    fixMode.set(false)
    strictMode.set(providers.gradleProperty("markdownlintStrict").map(String::toBoolean).orElse(false))
    allowInstall.set(providers.gradleProperty("markdownlintAllowInstall").map(String::toBoolean).orElse(false))
    nodeExecutable.set(providers.environmentVariable("MARKDOWNLINT_NODE"))
    repoDir.set(layout.projectDirectory)
}

tasks.register<MarkdownlintCli2Task>("markdownlintFix") {
    group = "verification"
    description = "markdownlint-cli2 --fix（自动修复可修的渲染级问题）。"
    fixMode.set(true)
    strictMode.set(providers.gradleProperty("markdownlintStrict").map(String::toBoolean).orElse(false))
    allowInstall.set(providers.gradleProperty("markdownlintAllowInstall").map(String::toBoolean).orElse(false))
    nodeExecutable.set(providers.environmentVariable("MARKDOWNLINT_NODE"))
    repoDir.set(layout.projectDirectory)
}
