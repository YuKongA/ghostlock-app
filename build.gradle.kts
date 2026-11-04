import java.nio.file.Files
import java.util.Properties

plugins {
    id("com.android.application") version "9.4.1" apply false
    id("com.android.library") version "9.4.1" apply false
    id("org.jetbrains.kotlin.plugin.serialization") version "2.4.20" apply false
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

/**
 * 找一个可执行工具：先看 -P<NAME>= 属性，再看同名环境变量，再扫 PATH 与 Homebrew 的常见位置。
 *
 * 为什么需要它：Gradle 守护进程的 PATH 往往不含 /opt/homebrew/bin（本机 cmake/ctest/ninja 都在那里），
 * 直接 exec("cmake") 会以 "A problem occurred starting process" 失败（2026-10-14 实测）。
 */
private fun resolveTool(name: String): String {
    providers.gradleProperty(name).orNull?.takeIf(String::isNotBlank)?.let { return it }
    System.getenv(name)?.takeIf(String::isNotBlank)?.let { return it }
    val win = System.getProperty("os.name").lowercase().contains("windows")
    val names = if (win) listOf(name + ".exe", name) else listOf(name)
    val onPath = System.getenv("PATH").orEmpty().split(File.pathSeparator)
        .asSequence().flatMap { dir -> names.asSequence().map { File(dir, it) } }
        .firstOrNull { it.isFile && it.canExecute() }
    if (onPath != null) return onPath.absolutePath
    for (dir in listOf("/opt/homebrew/bin", "/usr/local/bin", "/usr/bin")) {
        val candidate = File(dir, name)
        if (candidate.isFile && candidate.canExecute()) return candidate.absolutePath
    }
    return name
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

/**
 * Configures, builds, and executes one module-owned host CTest tree.
 *
 * Inputs include the complete module source boundary, the three resolved tool
 * paths, and every fixed option. The task deliberately reruns CTest on every
 * invocation; an incremental compile is allowed, a cached test result is not.
 */
abstract class NativeHostCmakeTestTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.InputDirectory
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val sourceDir: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.tasks.InputFiles
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val sourceInputs: org.gradle.api.file.ConfigurableFileCollection

    @get:org.gradle.api.tasks.Input
    abstract val cmakeExecutable: org.gradle.api.provider.Property<String>

    @get:org.gradle.api.tasks.Input
    abstract val ctestExecutable: org.gradle.api.provider.Property<String>

    @get:org.gradle.api.tasks.Input
    abstract val ninjaExecutable: org.gradle.api.provider.Property<String>

    @get:org.gradle.api.tasks.Input
    abstract val configureOptions: org.gradle.api.provider.ListProperty<String>

    @get:org.gradle.api.tasks.Input
    abstract val buildOptions: org.gradle.api.provider.ListProperty<String>

    @get:org.gradle.api.tasks.Input
    abstract val buildTargets: org.gradle.api.provider.ListProperty<String>

    @get:org.gradle.api.tasks.Input
    abstract val ctestOptions: org.gradle.api.provider.ListProperty<String>

    @get:org.gradle.api.tasks.OutputDirectory
    abstract val outputDir: org.gradle.api.file.DirectoryProperty

    @get:javax.inject.Inject
    abstract val execOps: org.gradle.process.ExecOperations

    /** Runs configure, build, then CTest; the first non-zero process stops the gate. */
    @org.gradle.api.tasks.TaskAction
    fun runGate() {
        val source = sourceDir.get().asFile.absolutePath
        val output = outputDir.get().asFile.absolutePath
        val configureCommand = buildList {
            add(cmakeExecutable.get())
            addAll(configureOptions.get())
            add("-DCMAKE_MAKE_PROGRAM=${ninjaExecutable.get()}")
            addAll(listOf("-S", source, "-B", output))
        }
        execOps.exec {
            commandLine(*configureCommand.toTypedArray())
        }.assertNormalExitValue()

        val buildCommand = buildList {
            addAll(listOf(cmakeExecutable.get(), "--build", output))
            addAll(buildOptions.get())
            val targets = buildTargets.get()
            if (targets.isNotEmpty()) {
                add("--target")
                addAll(targets)
            }
        }
        execOps.exec {
            commandLine(*buildCommand.toTypedArray())
        }.assertNormalExitValue()

        val ctestCommand = buildList {
            addAll(listOf(ctestExecutable.get(), "--test-dir", output))
            addAll(ctestOptions.get())
        }
        execOps.exec {
            commandLine(*ctestCommand.toTypedArray())
        }.assertNormalExitValue()
    }
}

/**
 * Proves that every current native test/probe candidate has one explicit
 * disposition and that the three host modules retain their frozen totals.
 */
abstract class VerifyNativeTestInventoryTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.Internal
    abstract val repoRoot: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.tasks.InputFiles
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val candidateFiles: org.gradle.api.file.ConfigurableFileCollection

    @get:org.gradle.api.tasks.InputFile
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val dispositionFile: org.gradle.api.file.RegularFileProperty

    @get:org.gradle.api.tasks.Input
    abstract val allowedRows: org.gradle.api.provider.SetProperty<String>

    @get:org.gradle.api.tasks.Input
    abstract val expectedHostCounts: org.gradle.api.provider.MapProperty<String, Int>

    /** Parses the four-column TSV and compares its path set with live discovery. */
    @org.gradle.api.tasks.TaskAction
    fun verifyInventory() {
        val root = repoRoot.get().asFile.canonicalFile
        val rootPath = root.toPath()
        val candidates = candidateFiles.files.map { file ->
            check(!Files.isSymbolicLink(file.toPath())) {
                "native candidate must not be a symlink: $file"
            }
            val canonical = file.canonicalFile
            check(canonical.isFile) { "native candidate is not a regular file: $canonical" }
            check(canonical.toPath().startsWith(rootPath)) {
                "native candidate escapes repository: $canonical"
            }
            canonical.relativeTo(root).invariantSeparatorsPath
        }.toSet()

        val lines = dispositionFile.get().asFile.readLines()
        check(lines.isNotEmpty()) { "native disposition is empty" }
        check(lines.first() == "relative_path\tstatus\tmodule\treplacement_gate") {
            "native disposition header mismatch: ${lines.first()}"
        }
        val rowsByPath = linkedMapOf<String, List<String>>()
        lines.drop(1).forEachIndexed { index, rawLine ->
            val lineNumber = index + 2
            check(rawLine.isNotBlank()) { "blank disposition row at line $lineNumber" }
            val fields = rawLine.split('\t')
            check(fields.size == 4 && fields.all(String::isNotBlank)) {
                "disposition line $lineNumber must contain four non-empty fields"
            }
            val path = fields[0]
            check(!path.startsWith('/') && '\\' !in path) {
                "disposition path is not normalized at line $lineNumber: $path"
            }
            val segments = path.split('/')
            check(segments.none { it.isEmpty() || it == "." || it == ".." }) {
                "disposition path has forbidden segments at line $lineNumber: $path"
            }
            check(rowsByPath.put(path, fields) == null) {
                "duplicate disposition path at line $lineNumber: $path"
            }
            val unresolvedSource = root.resolve(path)
            check(!Files.isSymbolicLink(unresolvedSource.toPath())) {
                "disposition path must not be a symlink: $path"
            }
            val source = unresolvedSource.canonicalFile
            check(source.isFile && source.toPath().startsWith(rootPath)) {
                "disposition path is missing or escapes repository: $path"
            }
            val rowKind = fields.drop(1).joinToString("\t")
            check(rowKind in allowedRows.get()) {
                "unknown native disposition at line $lineNumber: $rowKind"
            }
        }

        val manifestPaths = rowsByPath.keys
        check(manifestPaths == candidates) {
            val missingRows = (candidates - manifestPaths).sorted()
            val staleRows = (manifestPaths - candidates).sorted()
            "native disposition drift: missing_rows=$missingRows stale_rows=$staleRows"
        }

        val actualHostCounts = rowsByPath.values.groupingBy { fields ->
            "${fields[1]}:${fields[2]}"
        }.eachCount()
        expectedHostCounts.get().forEach { (key, expected) ->
            check(actualHostCounts[key] == expected) {
                "native host count mismatch for $key: expected=$expected actual=${actualHostCounts[key]}"
            }
        }
        val expectedHostKeys = expectedHostCounts.get().keys
        val unexpectedHostKeys = actualHostCounts.keys.filter { key ->
            (key.startsWith("host-ctest:") || key.startsWith("existing-host-ctest:")) &&
                key !in expectedHostKeys
        }
        check(unexpectedHostKeys.isEmpty()) {
            "unexpected host disposition groups: ${unexpectedHostKeys.sorted()}"
        }

        logger.lifecycle(
            "NATIVE_TEST_INVENTORY=PASS CANDIDATES={} HOST=33 43499GLUED=27 CONTRACTS=5 AVBCHECK=1",
            candidates.size,
        )
    }
}

val verifyNativeTestInventory = tasks.register<VerifyNativeTestInventoryTask>("verifyNativeTestInventory") {
    group = "verification"
    description = "Verifies the complete current-tree native test/probe disposition."
    repoRoot.set(layout.projectDirectory)
    dispositionFile.set(layout.projectDirectory.file("tools/tests/native-test-disposition.tsv"))
    candidateFiles.from(
        fileTree("app/src/main/cpp") {
            include("**/*_test.c", "**/*_test.cc", "**/*_test.cpp")
            include("**/test_*.c", "**/test_*.cc", "**/test_*.cpp")
            include("backends/*/*_probe.c", "backends/*/*_probe.cpp")
            include("backends/*/dev/*_probe.c", "backends/*/dev/*_probe.cpp")
            include("backends/*/timer/*_probe.c", "backends/*/timer/*_probe.cpp")
        },
        fileTree("tools") {
            include("**/*_test.c", "**/*_test.cc", "**/*_test.cpp")
            include("**/test_*.c", "**/test_*.cc", "**/test_*.cpp")
            include("**/*_probe.c", "**/*_probe.cpp")
        },
    )
    allowedRows.set(
        setOf(
            "host-ctest\t43499glued\tnativeTest43499GluedHost",
            "host-ctest\tnative-contracts\tnativeTestContractsHost",
            "existing-host-ctest\tavbcheck\tavbcheck",
            "retired-orphan\tnone\tmissing-glk-log-owner",
            "superseded-stale-api\tnone\tnativeTest43499GluedHost",
            "manual-linux-probe\tnone\tmanual-perf-policy-probe",
            "manual-device-diagnostic\t98260\tdevice-gate-98260",
            "manual-diagnostic\tnone\tmanual-abi-layout-probe",
            "manual-diagnostic\tnone\tmanual-perf-task-probe",
            "android-debug-probe\t43499\tandroidTest-43499-probes",
            "android-experiment-target\t98260\tdevice-gate-98260",
        ),
    )
    expectedHostCounts.set(
        mapOf(
            "host-ctest:43499glued" to 27,
            "host-ctest:native-contracts" to 5,
            "existing-host-ctest:avbcheck" to 1,
        ),
    )
    outputs.upToDateWhen { false }
}




/**
 *  avbcheck 的主机侧（原 tools/avbcheck/Makefile 的 host/test 目标）。
 *
 *  写成**任务类**：本仓启用了 configuration cache，脚本级任务的 action 不能触碰脚本对象
 *  （providers/layout/project），所以这里用注入的服务与类型化属性，全部全限定名，免去 import。
 */
abstract class AvbcheckHostTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.InputDirectory
    @get:org.gradle.api.tasks.PathSensitive(org.gradle.api.tasks.PathSensitivity.RELATIVE)
    abstract val sourceDir: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.tasks.OutputDirectory
    abstract val outputDir: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.tasks.Input
    abstract val cmakeExecutable: org.gradle.api.provider.Property<String>

    @get:org.gradle.api.tasks.Input
    abstract val ctestExecutable: org.gradle.api.provider.Property<String>

    @get:org.gradle.api.tasks.Input
    abstract val ninjaExecutable: org.gradle.api.provider.Property<String>

    @get:org.gradle.api.tasks.Input
    abstract val configureOptions: org.gradle.api.provider.ListProperty<String>

    @get:org.gradle.api.tasks.Input
    abstract val buildOptions: org.gradle.api.provider.ListProperty<String>

    @get:org.gradle.api.tasks.Input
    abstract val buildTargets: org.gradle.api.provider.ListProperty<String>

    @get:org.gradle.api.tasks.Input
    abstract val ctestOptions: org.gradle.api.provider.ListProperty<String>

    @get:javax.inject.Inject
    abstract val execOps: org.gradle.process.ExecOperations

    @org.gradle.api.tasks.TaskAction
    fun run() {
        val source = sourceDir.get().asFile.absolutePath
        val out = outputDir.get().asFile.absolutePath
        val configureCommand = buildList {
            add(cmakeExecutable.get())
            addAll(configureOptions.get())
            add("-DCMAKE_MAKE_PROGRAM=${ninjaExecutable.get()}")
            addAll(listOf("-S", source, "-B", out))
        }
        execOps.exec {
            commandLine(*configureCommand.toTypedArray())
        }.assertNormalExitValue()
        val buildCommand = buildList {
            addAll(listOf(cmakeExecutable.get(), "--build", out))
            addAll(buildOptions.get())
            add("--target")
            addAll(buildTargets.get())
        }
        execOps.exec {
            commandLine(*buildCommand.toTypedArray())
        }.assertNormalExitValue()
        val ctestCommand = buildList {
            addAll(listOf(ctestExecutable.get(), "--test-dir", out))
            addAll(ctestOptions.get())
        }
        execOps.exec {
            commandLine(*ctestCommand.toTypedArray())
        }.assertNormalExitValue()
    }
}

val avbcheck = tasks.register<AvbcheckHostTask>("avbcheck") {
    group = "verification"
    description = "Builds and tests the host side of tools/avbcheck (offline cross-check + unit test)"
    sourceDir.set(layout.projectDirectory.dir("tools/avbcheck"))
    outputDir.set(layout.buildDirectory.dir("avbcheck-host"))
    cmakeExecutable.set(glCmake)
    ctestExecutable.set(glCtest)
    ninjaExecutable.set(glNinja)
    configureOptions.set(listOf("-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"))
    buildOptions.set(listOf("--parallel"))
    buildTargets.set(listOf("avbcheck-host", "avbcheck-tests"))
    ctestOptions.set(listOf("--output-on-failure"))
    dependsOn(verifyNativeTestInventory)
    outputs.upToDateWhen { false }
}

val nativeTest43499GluedHost = tasks.register<NativeHostCmakeTestTask>("nativeTest43499GluedHost") {
    group = "verification"
    description = "Builds and executes all 27 host-compatible 43499-Glued CTests."
    sourceDir.set(layout.projectDirectory.dir("app/src/main/cpp/backends/43499glued/tests"))
    sourceInputs.from(
        fileTree("app/src/main/cpp/backends/43499glued") {
            include("**/*.c", "**/*.cc", "**/*.cpp", "**/*.h", "**/*.hpp", "**/CMakeLists.txt")
        },
        layout.projectDirectory.file("tools/tests/native-test-disposition.tsv"),
    )
    cmakeExecutable.set(glCmake)
    ctestExecutable.set(glCtest)
    ninjaExecutable.set(glNinja)
    configureOptions.set(listOf("-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"))
    buildOptions.set(listOf("--parallel"))
    buildTargets.set(emptyList())
    ctestOptions.set(listOf("--output-on-failure"))
    outputDir.set(layout.buildDirectory.dir("cmake/host-43499glued"))
    dependsOn(verifyNativeTestInventory)
    outputs.upToDateWhen { false }
}

val nativeTestContractsHost = tasks.register<NativeHostCmakeTestTask>("nativeTestContractsHost") {
    group = "verification"
    description = "Builds and executes the five repository-wide native contract CTests."
    sourceDir.set(layout.projectDirectory.dir("tools/tests"))
    sourceInputs.from(
        fileTree("tools/tests") {
            include("**/*.c", "**/*.cc", "**/*.cpp", "**/*.h", "**/*.hpp", "CMakeLists.txt", "*.tsv")
        },
        fileTree("app/src/main/cpp/common") {
            include("gl_log.c", "gl_log.h")
        },
        fileTree("app/src/main/cpp/backends/43499/core") {
            include("**/*.h", "**/*.hpp")
        },
    )
    cmakeExecutable.set(glCmake)
    ctestExecutable.set(glCtest)
    ninjaExecutable.set(glNinja)
    configureOptions.set(listOf("-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"))
    buildOptions.set(listOf("--parallel"))
    buildTargets.set(emptyList())
    ctestOptions.set(listOf("--output-on-failure"))
    outputDir.set(layout.buildDirectory.dir("cmake/host-contracts"))
    dependsOn(verifyNativeTestInventory)
    outputs.upToDateWhen { false }
}

tasks.register("nativeTestHost") {
    group = "verification"
    description = "Runs the complete current-tree host-native aggregate (33 CTests)."
    dependsOn(verifyNativeTestInventory, nativeTest43499GluedHost, nativeTestContractsHost, avbcheck)
    doLast {
        logger.lifecycle("NATIVE_TEST_HOST=PASS TOTAL=33 FAILED=0")
    }
}

tasks.register("nativeBuildAndroid") {
    group = "verification"
    description = "Builds and verifies the debug Android native target graph."
    dependsOn(":app:verifyDebugNativeArtifacts")
}

tasks.register("checkAll") {
    group = "verification"
    description = "Runs host-native, Android-native, and exact Kotlin XML gates."
    dependsOn("nativeTestHost", "nativeBuildAndroid", ":app:verifyDebugUnitTestXml")
}

/**
 *  avbcheck 的设备侧：NDK aarch64 静态可执行（原 Makefile 的默认目标）。
 *
 *  同样是任务类（configuration cache）。NDK 路径走既有解析：环境变量 ANDROID_NDK_HOME / ANDROID_NDK_ROOT，
 *  都没有就**具名失败**并说明怎么设 —— 不在任何文件里写死本机路径（原 Makefile 就是在这里硬编码了一个）。
 */
abstract class AvbcheckDeviceTask : org.gradle.api.DefaultTask() {
    @get:org.gradle.api.tasks.InputDirectory
    abstract val sourceDir: org.gradle.api.file.DirectoryProperty

    @get:org.gradle.api.tasks.OutputDirectory
    abstract val outputDir: org.gradle.api.file.DirectoryProperty

    @get:javax.inject.Inject
    abstract val execOps: org.gradle.process.ExecOperations

    @org.gradle.api.tasks.TaskAction
    fun run() {
        val ndk = System.getenv("ANDROID_NDK_HOME") ?: System.getenv("ANDROID_NDK_ROOT")
            ?: throw org.gradle.api.GradleException(
                "avbcheckDevice needs an NDK: set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT). " +
                    "The toolchain file comes from \$NDK/build/cmake/android.toolchain.cmake.",
            )
        val source = sourceDir.get().asFile.absolutePath
        val out = outputDir.get().asFile.absolutePath
        execOps.exec {
            commandLine(
                "cmake", "-S", source, "-B", out, "-G", "Ninja",
                "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake",
                "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-34",
            )
        }
        execOps.exec {
            commandLine("cmake", "--build", out, "-j", "8", "--target", "avbcheck-device")
        }
        val built = java.io.File(out, "avbcheck-device")
        logger.lifecycle("avbcheckDevice: {} bytes at {}", built.length(), built)
    }
}

val avbcheckDevice = tasks.register<AvbcheckDeviceTask>("avbcheckDevice") {
    group = "verification"
    description = "Builds the on-device aarch64 static avbcheck via the NDK toolchain"
    sourceDir.set(layout.projectDirectory.dir("tools/avbcheck"))
    outputDir.set(layout.buildDirectory.dir("avbcheck-device"))
}
