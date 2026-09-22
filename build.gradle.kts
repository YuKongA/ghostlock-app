import com.typesafe.config.ConfigFactory
import com.typesafe.config.ConfigParseOptions
import com.typesafe.config.ConfigSyntax
import java.util.Properties

buildscript {
    repositories {
        mavenCentral()
    }
    dependencies {
        classpath("com.typesafe:config:1.4.3")
    }
}

plugins {
    id("com.android.application") version "9.1.1" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.10" apply false
}

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
// host-test, extract, kernel-profiles, app). Delete the whole tree here so a
// single root `clean` resets all of them.
tasks.register<Delete>("clean") {
    description = "Delete the root build/ directory (all module outputs)."
    delete(layout.buildDirectory)
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

/**
 * Serializes every bundled HOCON kernel profile into the GLK1 v2 binary layout
 * the native side reads (`src/core/profile_binary.cpp`). Mirrors
 * `NativeProfileDocument` (app/.../data/NativeProfile.kt): same header and the
 * same 86-field order. Output: build/kernel-profiles/<release>.bin.
 */
tasks.register("exportKernelProfiles") {
    description = "Serialize bundled HOCON kernel profiles into GLK1 .bin documents"
    group = "build"
    val profilesDir = layout.projectDirectory.dir("app/src/main/assets/kernel_profiles")
    val outputDir = layout.buildDirectory.dir("kernel-profiles")
    inputs.dir(profilesDir)
    outputs.dir(outputDir)
    doLast {
        val srcDir = profilesDir.asFile
        val outDir = outputDir.get().asFile
        if (outDir.exists()) outDir.deleteRecursively()
        outDir.mkdirs()

        val parseOptions = ConfigParseOptions.defaults()
            .setSyntax(ConfigSyntax.CONF)
            .setAllowMissing(false)

        fun expand(name: String, visiting: MutableSet<String>): String {
            if (!visiting.add(name)) return ""
            val file = File(srcDir, name)
            if (!file.isFile) {
                visiting.remove(name)
                return ""
            }
            val text = file.readText()
            val base = name.substringBeforeLast('/', "")
            val expanded = StringBuilder()
            for (line in text.lineSequence()) {
                val trimmed = line.trim()
                val target = when {
                    trimmed.startsWith("#") || trimmed.startsWith("//") -> null
                    trimmed.startsWith("include ") ->
                        trimmed.removePrefix("include ").trim().trim('"').takeIf { it.isNotEmpty() }

                    else -> null
                }
                if (target == null) {
                    expanded.append(line).append('\n')
                } else {
                    val resolved = if (base.isEmpty()) target else "$base/$target"
                    expanded.append(expand(resolved, visiting)).append('\n')
                }
            }
            visiting.remove(name)
            return expanded.toString()
        }

        fun parse(name: String): Map<*, *>? = runCatching {
            ConfigFactory.parseString(expand(name, linkedSetOf()), parseOptions)
                .resolve().root().unwrapped()
        }.getOrNull()

        fun lookup(root: Map<*, *>, path: String): Long {
            var current: Any? = root
            for (segment in path.split('.')) {
                current = (current as? Map<*, *>)?.get(segment) ?: return 0L
            }
            return when (current) {
                is Number -> current.toLong()
                is Boolean -> if (current) 1L else 0L
                is String -> current.toLongOrNull() ?: 0L
                else -> 0L
            }
        }

        val knownRoutes = listOf("tcp_zerocopy", "select_stack", "multicast_waiter")
        fun routeWire(route: String?): Int = when (route) {
            "tcp_zerocopy" -> 1
            "select_stack" -> 2
            "multicast_waiter" -> 3
            else -> 0
        }

        fun routeNameOf(profile: Map<*, *>): String? = when (val route = profile["route"]) {
            is String -> route.takeIf { it.isNotEmpty() && it != "null" }
            is Map<*, *> -> route.keys.filterIsInstance<String>().firstOrNull { it in knownRoutes }
            else -> null
        }

        fun fallbackOf(profile: Map<*, *>): String? {
            val to = ((profile["fallback"] as? Map<*, *>)?.get("to") as? String)
                ?.takeIf { it.isNotEmpty() && it != "null" }
            if (to != null) return to
            return (profile["fallback_to"] as? String)?.takeIf { it.isNotEmpty() && it != "null" }
        }

        /* Field order mirrors NativeProfileDocument.flatten() indices 0..85. */
        val fieldPaths = listOf(
            "task_struct.prio", "task_struct.normal_prio", "task_struct.sched_task_group",
            "task_struct.pi_lock", "task_struct.pi_waiters", "task_struct.pi_top_task",
            "task_struct.pi_blocked_on", "task_struct.pid", "task_struct.tgid",
            "task_struct.atomic_flags", "task_struct.real_cred", "task_struct.cred",
            "task_struct.comm", "task_struct.tasks", "task_struct.seccomp",
            "cred.copy_size", "cred.usage_offset", "cred.usage_value",
            "cred.caps_offset", "cred.caps_count", "cred.caps_value", "cred.ref_count",
            "cred.ref0_offset", "cred.ref1_offset", "cred.ref2_offset", "cred.ref3_offset",
            "cred.ref0_image", "cred.ref1_image", "cred.ref2_image", "cred.ref3_image",
            "offset.init_task", "offset.init_cred", "offset.empty_zero_page",
            "offset.mcast_fake_bss", "offset.root_task_group", "offset.selinux_enforcing",
            "offset.selinux_blob_sizes", "offset.security_hook_heads",
            "offset.slide_nfulnl_logger", "offset.slide_loggers_0_1", "offset.slide_boot_id",
            "mcast.waiter_off", "mcast.buffer_size", "mcast.task_offset", "mcast.lock_offset",
            "mcast.fake_lock_offset", "mcast.fake_task_offset", "mcast.lock_slots_offset",
            "mcast.lock_slot_count", "mcast.lock_slot_stride",
            "kernel_phys_load", "pselect_waiter_shift", "compact_waiter",
            "kernelsnitch.collisions", "kernelsnitch.mm_struct_sz",
            "execution.recommended_cpus.main", "execution.recommended_cpus.consumer",
            "execution.heap.prepare_max_attempts", "execution.heap.prepare_timeout_ms",
            "execution.heap.kernelsnitch_timeout_ms",
            "execution.race.route_wait_ms", "execution.race.setup_settle_us",
            "execution.race.state_poll_interval_us",
            "execution.stages.w1_attempts", "execution.stages.w1_settle_us",
            "execution.stages.w1_scratch_repair_attempts",
            "execution.stages.w2_attempts", "execution.stages.w2_settle_us",
            "execution.stages.w3_chain_rounds", "execution.stages.w3_attempts",
            "execution.stages.w3_settle_us",
            "execution.routes.tcp_zerocopy.attempts", "execution.routes.tcp_zerocopy.arm_sequence",
            "execution.routes.tcp_zerocopy.post_receive_hold_iterations",
            "execution.routes.select_stack.enter_delay_us", "execution.routes.select_stack.timeout_us",
            "execution.routes.select_stack.consumer_max_calls",
            "execution.routes.select_stack.consumer_burst_calls",
            "execution.routes.multicast_waiter.ready_timeout_ms",
            "execution.routes.multicast_waiter.post_requeue_settle_us",
            "execution.routes.multicast_waiter.post_adjust_settle_us",
            "execution.handoff.pre_dispatch_settle_ms", "execution.handoff.module_poll_attempts",
            "execution.handoff.module_poll_interval_ms", "execution.handoff.enforce_poll_attempts",
            "execution.handoff.enforce_poll_interval_ms",
            "safe_mode", "multicast_resident",
        )
        check(fieldPaths.size == 88) { "field table drifted: ${fieldPaths.size}" }

        fun serialize(
            release: String,
            route: Int,
            kernelMajor: Long,
            recommendShizuku: Long,
            fallbackRoute: Int,
            values: LongArray,
        ): ByteArray {
            val releaseBytes = release.toByteArray(Charsets.UTF_8)
            require(releaseBytes.size <= 0xffff) { "release is too long: $release" }
            val buffer = java.nio.ByteBuffer
                .allocate(12 + releaseBytes.size + values.size * 8)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
            buffer.putInt(0x314B4C47)   // "GLK1"
            buffer.putShort(3)          // layout version
            buffer.put(route.toByte())
            buffer.put(kernelMajor.toByte())
            buffer.put(recommendShizuku.toByte())
            buffer.put(fallbackRoute.toByte())
            buffer.putShort(releaseBytes.size.toShort())
            buffer.put(releaseBytes)
            values.forEach(buffer::putLong)
            return buffer.array()
        }

        var exported = 0
        srcDir.listFiles { entry -> entry.isFile && entry.name.endsWith(".conf") }
            ?.sortedBy { it.name }
            ?.forEach { file ->
                val profile = parse(file.name) ?: return@forEach
                val release = profile["release"] as? String ?: return@forEach
                val bytes = serialize(
                    release = release,
                    route = routeWire(routeNameOf(profile)),
                    kernelMajor = lookup(profile, "kernel_major"),
                    recommendShizuku = lookup(profile, "recommend_shizuku"),
                    fallbackRoute = routeWire(fallbackOf(profile)),
                    values = LongArray(fieldPaths.size) { lookup(profile, fieldPaths[it]) },
                )
                File(outDir, "$release.bin").writeBytes(bytes)
                exported++
                logger.lifecycle("exportKernelProfiles: $release (${bytes.size} bytes)")
            }
        logger.lifecycle("exportKernelProfiles: $exported profile(s) -> ${outDir.absolutePath}")
    }
}
