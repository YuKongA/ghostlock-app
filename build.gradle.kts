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
    /* D1 host tools are generated from core/src too; without inputs a successful old build made Gradle
     * skip CMake after a new executable was registered. */
    inputs.files(fileTree("core/src"), file("core/CMakeLists.txt"))
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
    /* 三个库都要进 APK：旧的帧边界 + 两个 backend 各自的函数面（D2）。 */
    from("build/native/libghostlock_jni.so")
    from("build/native/libghostlock43499.so")
    from("build/native/libghostlock43284.so")
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

/**
 * D1 跨端字段生成：唯一输入是 native attack_contract_fields.def；生成 Kotlin 参数类型与 TSV 清单。
 * 两个输出都在仓库根 build/app 下，源码树不接收生成物。
 */
tasks.register("exportJniAttackContract") {
    description = "从 D1 单表生成 Kotlin JNI 参数类型与字段清单"
    group = "verification"
    dependsOn("nativeBuildHost")
    val tool = file("build/host/jni_attack_contract_codegen")
    val kotlinOut = file(
        "build/app/generated/source/attackContract/com/ghostlock/app/core/generated/AttackContractDtos.kt",
    )
    val manifestOut = file("build/app/jni-attack-contract-fields.tsv")
    inputs.files(
        file("core/src/jni/attack_contract_fields.def"),
        file("core/src/jni/attack_contract_model.hpp"),
        file("core/src/jni/attack_contract_codegen.cpp"),
        tool,
    )
    outputs.files(kotlinOut, manifestOut)
    doLast {
        val process = ProcessBuilder(
            tool.absolutePath,
            kotlinOut.absolutePath,
            manifestOut.absolutePath,
        ).redirectErrorStream(true).start()
        val text = process.inputStream.readBytes().toString(Charsets.UTF_8)
        val code = process.waitFor()
        check(code == 0) { "jni_attack_contract_codegen failed(" + code + "):\n" + text }
        logger.lifecycle(text.trim())
        logger.lifecycle(
            "exportJniAttackContract: " + kotlinOut.path + " + " + manifestOut.path,
        )
    }
}

/* 应用单测必须先看到那份导出：协议表是跨端契约，测试不允许在缺它的前提下跑过。 */
tasks.matching { it.name == "testDebugUnitTest" }.configureEach {
    dependsOn("exportJniProtocolManifest")
    dependsOn("exportJniAttackContract")
}
/* ---------------------------------------------------------------------------
 * 设备门禁（J7）：把「在真机上跑一遍」变成一条 Gradle 命令，并**机器校验**关键行。
 *
 * 校验值一律不手抄：镜像摘要与钩子数字从 native 导出的 manifest 读
 * （app/build/jni-protocol-manifest.tsv，由 native 的 jni_protocol_manifest 产出），
 * 期望的计划步名与拒绝名写成这里的一张表。抓到的原始日志落到 build/app/device-gate.log。
 * ------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------
 * JNI 库的导出符号检查（2026-10-14 审查第 1 点）：库与函数面必须一一对应。
 *
 * 期望值不手抄：每个 backend 该导出哪些函数，从它自己的源码里的 JNIEXPORT 声明算出来；
 * 另一个 backend 的命名空间在本库里出现一个符号就算失败。
 * ------------------------------------------------------------------------- */
tasks.register("verifyJniLibs") {
    description = "检查三个 JNI 库的导出符号：每个 backend 只导出自己的函数面"
    group = "verification"
    dependsOn("nativeBuildAndroid")
    val libDir = file("build/native")
    val ndkRoot = resolveNdkDir()
    val sourceDir = file("core/src/jni")

    doLast {
        val llvmNm = File(ndkRoot, "toolchains/llvm/prebuilt").listFiles()
            ?.firstOrNull { it.isDirectory }
            ?.let { File(it, "bin/llvm-nm") }
        check(llvmNm != null && llvmNm.isFile) { "找不到 llvm-nm（在 " + ndkRoot + " 下）" }

        fun exec(vararg command: String): String {
            val process = ProcessBuilder(*command).redirectErrorStream(true).start()
            val text = process.inputStream.readBytes().toString(Charsets.UTF_8)
            val code = process.waitFor()
            check(code == 0) { "命令失败(" + code + "): " + command.joinToString(" ") + "\n" + text }
            return text
        }

        /** 源码里声明的函数名（JNIEXPORT 后面那一段）。 */
        fun expectedFunctions(backend: String): Set<String> {
            val pattern = Regex("Java_com_ghostlock_app_jni_cve" + backend + "_([A-Za-z0-9_]+)")
            return sourceDir.listFiles().orEmpty()
                .filter { it.name.endsWith(".cpp") && it.name.contains(backend) }
                .flatMap { file -> pattern.findAll(file.readText()).map { it.groupValues[1] }.toList() }
                .toSet()
        }

        fun exportedFunctions(library: String, backend: String): Set<String> {
            val pattern = Regex("Java_com_ghostlock_app_jni_cve" + backend + "_([A-Za-z0-9_]+)")
            val text = exec(llvmNm!!.absolutePath, "-D", "--defined-only", File(libDir, library).absolutePath)
            return pattern.findAll(text).map { it.groupValues[1] }.toSet()
        }

        val problems = mutableListOf<String>()
        for ((backend, library) in listOf("43499" to "libghostlock43499.so", "43284" to "libghostlock43284.so")) {
            val expected = expectedFunctions(backend)
            val exported = exportedFunctions(library, backend)
            check(expected.isNotEmpty()) { "源码里读不到 cve" + backend + " 的函数声明" }
            val missing = expected - exported
            if (missing.isNotEmpty()) {
                problems += library + " 少了导出：" + missing.joinToString(",")
            }
            val other = if (backend == "43499") "43284" else "43499"
            val foreign = exportedFunctions(library, other)
            if (foreign.isNotEmpty()) {
                problems += library + " 导出了另一个 backend 的函数：" + foreign.joinToString(",")
            }
            logger.lifecycle("verifyJniLibs: " + library + " 导出 " + exported.size + " 个函数（源码期望 " + expected.size + "）")
        }
        val legacy = exportedFunctions("libghostlock_jni.so", "43499") +
            exportedFunctions("libghostlock_jni.so", "43284")
        if (legacy.isNotEmpty()) {
            problems += "旧的 libghostlock_jni.so 仍然导出新函数面的符号：" + legacy.joinToString(",")
        }
        check(problems.isEmpty()) { "JNI 库符号检查未通过：\n  " + problems.joinToString("\n  ") }
        logger.lifecycle("verifyJniLibs: PASS（三个库互不重叠）")
    }
}

tasks.register("deviceGate") {
    description = "在已连接的设备上跑门禁：装 APK → 起临时界面 → 抓 logcat → 校验关键行"
    group = "verification"
    dependsOn(":app:assembleDebug")
    val adbExe: String = run {
        val onPath = resolveTool("adb")
        if (File(onPath).isFile) {
            onPath
        } else {
            val fromSdk = File(System.getProperty("user.home"), "Library/Android/sdk/platform-tools/adb")
            if (fromSdk.isFile) fromSdk.absolutePath else "adb"
        }
    }
    val apkDir = file("build/app/outputs/apk/debug")
    val manifestFile = file("app/build/jni-protocol-manifest.tsv")
    val logFile = file("build/app/device-gate.log")
    /* 配置期读一次：doLast 里调 findProperty 会把 Project 捕获进配置缓存。 */
    val serial_override = findProperty("deviceSerial") as String?

    /* 要读运行时的 adb 输出，且每次都必须真跑（不许被缓存跳过）。 */
    notCompatibleWithConfigurationCache("设备门禁读运行时的 adb 输出")
    outputs.upToDateWhen { false }

    doLast {
        fun exec(vararg command: String): String {
            val process = ProcessBuilder(*command).redirectErrorStream(true).start()
            val text = process.inputStream.readBytes().toString(Charsets.UTF_8)
            val code = process.waitFor()
            check(code == 0) { "命令失败(" + code + "): " + command.joinToString(" ") + "\n" + text }
            return text
        }

        /* 同一台机器可能同时挂在两条 transport 上（USB 与无线 adb，实测 2026-10-15）：
         * 只认 USB 那条，并把 -s 传给每一步 —— 否则 adb 会以 "more than one device" 拒绝，
         * 或者把 APK 装到一条通道、把 am start 发到另一条。 */
        val listing = exec(adbExe, "devices", "-l")
        /* -l 的行是 SERIAL<空白>device usb:... transport_id:N（adb 用空格对齐，不是 TAB），
         * 所以按空白切：第二列是 device 且行里有 usb: 的，就是 USB 那条。 */
        val usb_parts = listing.lines()
            .map { line -> line.trim().split(Regex("\\s+")) }
            .firstOrNull { parts -> parts.size >= 2 && parts[1] == "device" &&
                                    parts.any { it.startsWith("usb:") } }
        val serial = serial_override ?: usb_parts?.firstOrNull()
            ?: throw GradleException("没有 USB 设备（可用 -PdeviceSerial= 指定）：\n" + listing)
        logger.lifecycle("deviceGate: 设备 " + serial)
        fun adb(vararg args: String): String = exec(adbExe, "-s", serial, *args)

        val apk = apkDir.listFiles()?.filter { it.name.endsWith(".apk") }?.maxByOrNull { it.lastModified() }
        check(apk != null) { "没有 APK：先运行 :app:assembleDebug" }

        logger.lifecycle("deviceGate: 安装 " + apk!!.name)
        /* 先卸再装，并用 --no-streaming 走 push 路径。
         * 原因（实测 2026-10-15）：连续 `-r` 覆盖同一个 versionCode 会把 PackageManager 留在
         * 「REPLACED, but missing application info」状态 —— 此时 am start 报 Activity class does not
         * exist（result code=-92），而 APK 的 manifest、dex、dexopt 全都是好的；卸载 + 重启 + 重装才清干净。
         * 卸载失败不致命（本来就没装）。 */
        runCatching { adb("uninstall", "com.ghostlock.app") }
        adb("install", "--no-streaming", "-g", apk.absolutePath)
        adb("logcat", "-c")
        adb("shell", "am", "start", "-n", "com.ghostlock.app/.tempui.TempUiActivity")
        Thread.sleep(7000L)
        val log = adb("logcat", "-d", "-s", "GhostlockGate:I", "GhostlockPlan:I", "GhostlockUi:I", "GhostlockTyped:I")
        logFile.parentFile.mkdirs()
        logFile.writeText(log)

        /* 期望值：从 native 导出的 manifest 里读，缺行就是 native 侧没导出 —— 也是失败。 */
        val rows: Map<String, String> = manifestFile.readLines()
            .mapNotNull { line ->
                val parts = line.split('\t')
                if (parts.size == 3) parts[0] + " " + parts[1] to parts[2] else null
            }
            .toMap()
        val expected = mutableListOf<String>()
        expected += "gate: end"
        for (name in listOf("splice_helper", "landing_payload")) {
            rows["image " + name]?.let { digest -> expected += "sha256=" + digest }
        }
        rows["hook branch_word"]?.let { expected += "branch=" + it }
        rows["hook displaced_word"]?.let { expected += "displaced=" + it }
        /* 计划那几条：画像名与 release 由设备自己报（uname -r），声明的步骤由画像资产里的
         * available 队列决定 —— 两者都不手抄。43284 的 declared 是 PlanCatalog 的别名展开，
         * 该展开由 PlanCatalogTest 单测钉住。 */
        val release = adb("shell", "uname", "-r").trim()
        expected += "plan: profile=" + release + ".conf release=" + release + " generic=false schema=3"
        val profileText = file("app/src/main/assets/profile/" + release + ".conf").readText()
        val declared = profileText
            .substringAfter("cve_2026_43499 {")
            .substringBefore("cve_2026_43284")
            .let { block -> Regex("step = \"([a-z0-9_]+)\"").findAll(block).map { it.groupValues[1] } }
            .joinToString(",")
        expected += "plan: cve_2026_43499 declared=" + declared
        expected += "plan: cve_2026_43499 step=w1 outcome=Refused reason=precondition:w1-address"
        expected += "plan: cve_2026_43284 declared=patch1,carrier,hook,trigger,wait"
        expected += "plan: cve_2026_43284 step=patch1 outcome=Refused reason=precondition:patch1-path"

        /* D2：两个新库必须在真机上被 Kotlin 调用到（库名 + 调用码 + 具名拒绝）。 */
        expected += "typed: 43499 library loaded"
        expected += "typed: 43499 initialize code=0"
        expected += "typed: 43499 snapshot code=0 phase=1"
        expected += "typed: 43499 prepare code=-3 refusal=op-not-wired:prepare"
        expected += "typed: 43499 writeWord code=-3 refusal=op-not-wired:write_word"
        expected += "typed: 43499 shutdown-after-destroy code=-5 refusal=stale-handle"
        expected += "typed: 43284 library loaded"
        expected += "typed: 43284 initialize code=0"
        /* 门现在会真的开：本应用自己的缓存目录能开（token>0），不存在的路径具名拒绝。 */
        /* 票据 = (slot << 32) | generation：只断言开了且给了正票据，数字不写死。 */
        expected += "typed: 43284 resolveTarget code=0 ticket="
        expected += "typed: 43284 resolveTarget(bad) code=-4 refusal=door-refused"
        /* 读块现在已经接线（门那扇 fd 上的 pread）：读到文件里的十六个 0x5A。 */
        expected += "typed: 43284 readBlock code=0 bytes=5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a refusal="
        /* 写走落地对象（ESP 路线）：真机上 land_block 成功、序号推进；flags 里没有 Applied，
           因为读回的是门那扇 fd 上的旧内容 —— 状态机如实地停在 Dispatched。 */
        /* patch1 的真结局：第一块原语返回成功，第二块被拒；回读仍是文件原内容（写未落上）。 */
        expected += "typed: 43284 patch1-via-kotlin blocks=1 refusedAt=16"
        expected += "typed: 43284 patch1-readback code=0 first16=5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
        /* 快照只断言不变量：phase/flags/width/ticket/role；序号会随写次数变，不写死。 */

        /* 43284 的数据 op：**新**函数面上尚未接线（旧帧路径仍提供两张图，上面已经按 manifest
         * 对拍过摘要），所以这里如实断言 op-not-wired —— 等 43284 的 D2 装配落地后再改成
         * 断言 ok + size=1440。 */
        /* 43284 的数据 op 现在**已接线**（D2 装配）：镜像与钩子计划必须成功，摘要与 manifest 一致。 */
        expected += "typed: 43284 embeddedImage code=ok size=1440"
        rows["image splice_helper"]?.let { digest -> expected += "sha256=" + digest }
        expected += "typed: 43284 hookPlan code=0"
        expected += "typed: 43284 moduleCheck code=-4 refusal=module-check-refused"
        /* 等终态接的是真轮询：不存在的路径在预算内必须答 inflight（既非成功也非失败）。 */
        expected += "typed: 43284 waitTerminal code=-8 state=2 refusal=wait_terminal:inflight"
        /* trigger 已经接线（复用 steps 的 trigger_orphan）：真机上双 fork 真的执行成功，所以答案就是 ok。 */
        expected += "typed: 43284 trigger code=0 refusal="
        /* 包装类（Assembled43284）在真机上跑一遍最小闭环：开门、读块、收尾都走它。 */
        expected += "typed: assembled 43284 init code=0"
        expected += "typed: assembled 43284 open code=0 ticket="
        expected += "typed: assembled 43284 read bytes=5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a refusal="
        expected += "typed: assembled 43284 close ok=true"
        /* 分步拼装（顺序在 Kotlin）：真机实测 socket/surface/landing 句柄均为 1（空表第一个）、落地对象拉起来了。 */
        expected += "typed: stepwise 43284 open target="
        expected += "typed: stepwise 43284 socket=1 refusal="
        expected += "typed: stepwise 43284 surface=1 refusal="
        expected += "typed: stepwise 43284 landing=1 refusal="
        expected += "typed: stepwise 43284 bringUp=true refusal="

        val missing = expected.filterNot { log.contains(it) }
        if (missing.isNotEmpty()) {
            throw GradleException(
                "设备门禁未通过，缺少 " + missing.size + " 项：\n  " + missing.joinToString("\n  ") +
                    "\n原始日志：" + logFile.path,
            )
        }
        logger.lifecycle("deviceGate: PASS（" + expected.size + " 项校验全部命中；日志 " + logFile.path + "）")
    }
}

/** 汇总门禁：宿主测试（原生 CTest + app 单测）+ 设备门禁。 */
tasks.register("gateAll") {
    description = "集中门禁：checkAll（宿主）+ deviceGate（真机）"
    group = "verification"
    dependsOn("checkAll", "deviceGate")
}

tasks.register("checkAll") {
    description = "集中测试入口：原生 CTest + Kotlin 单测"
    group = "verification"
    dependsOn("nativeTestHost", ":app:testDebugUnitTest")
    dependsOn("verifyJniLibs")
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
