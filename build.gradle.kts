import com.typesafe.config.ConfigFactory
import com.typesafe.config.ConfigParseOptions
import com.typesafe.config.ConfigSyntax
import java.util.Properties

buildscript {
    repositories {
        mavenCentral()
    }
    dependencies {
        classpath("com.typesafe:config:1.4.9")
    }
}

plugins {
    id("com.android.application") version "9.1.1" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20" apply false
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
 * Serializes every bundled HOCON kernel profile into the v2 binary layout
 * the native side reads (`src/core/profile/binary.cpp`). Mirrors
 * `NativeProfileDocument` (app/.../data/NativeProfile.kt): fixed common slots
 * (`kCommonFields`) plus a per-route section (`kTcp/kSelect/kMulticastFields`).
 * Output: build/kernel-profiles/<release>.bin.
 */
tasks.register("exportKernelProfiles") {
    description = "Serialize bundled HOCON kernel profiles into v2 .bin documents"
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

        fun lookupOrNull(root: Map<*, *>, path: String): Long? {
            var current: Any? = root
            for (segment in path.split('.')) {
                current = (current as? Map<*, *>)?.get(segment) ?: return null
            }
            return when (current) {
                is Number -> current.toLong()
                is Boolean -> if (current) 1L else 0L
                is String -> current.toLongOrNull()
                else -> null
            }
        }

        fun lookup(root: Map<*, *>, path: String): Long = lookupOrNull(root, path) ?: 0L

        /* Mirrors the app's nativeValue: canonical route fields map onto the
         * declared `route.<name>.<field>` / `fallback.route.<name>.<field>`
         * branches (`compact_waiter`, `waiter_shift`, `mcast.*`). */
        fun nativeValue(
            root: Map<*, *>,
            routeName: String?,
            fallbackName: String?,
            path: String,
        ): Long {
            val branchField = when (path) {
                "compact_waiter" -> "compact_waiter"
                "pselect_waiter_shift" -> "waiter_shift"
                else -> null
            }
            if (branchField != null) {
                if (routeName != null) {
                    lookupOrNull(root, "route.$routeName.$branchField")?.let { return it }
                }
                if (fallbackName != null && fallbackName != "none") {
                    lookupOrNull(root, "fallback.route.$fallbackName.$branchField")?.let { return it }
                }
            }
            if (path.startsWith("mcast.")) {
                val field = path.removePrefix("mcast.")
                if (routeName != null) {
                    lookupOrNull(root, "route.$routeName.$field")?.let { return it }
                }
                if (fallbackName != null && fallbackName != "none") {
                    lookupOrNull(root, "fallback.route.$fallbackName.$field")?.let { return it }
                }
                return lookup(root, "mcast.$field")
            }
            return lookup(root, path)
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

        fun deepCopy(value: Any?): Any? = when (value) {
            is Map<*, *> -> value.entries.associate { it.key to deepCopy(it.value) }.toMutableMap()
            is List<*> -> value.map { deepCopy(it) }.toMutableList()
            else -> value
        }

        /* Mirrors the app's fillRouteExecutionDefaults: a profile carries only
         * its own route group, so missing groups are filled from the shared
         * execution-<route>.conf. Common slots (e.g. the consumer cadence read
         * by every route) depend on this. */
        fun fillRouteExecution(profile: Map<*, *>): Map<*, *> {
            val root = deepCopy(profile) as MutableMap<Any?, Any?>
            val execution = (root["execution"] as? MutableMap<Any?, Any?>) ?: return root
            val routes = execution.getOrPut("routes") { mutableMapOf<Any?, Any?>() }
                    as MutableMap<Any?, Any?>
            for (route in knownRoutes) {
                if (routes.containsKey(route)) continue
                val conf = parse("execution-${route.replace('_', '-')}.conf") ?: continue
                val group = (((conf["execution"] as? Map<*, *>)?.get("routes") as? Map<*, *>)
                        ?.get(route)) as? Map<*, *> ?: continue
                routes[route] = deepCopy(group)
            }
            return root
        }

        /* v2: fixed common slots + a per-route section. Mirrors the native
         * kCommonFields / kXxxFields tables and NativeProfileDocument. */
        val commonFieldPaths = listOf(
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
            "offset.root_task_group", "offset.selinux_enforcing",
            "offset.selinux_blob_sizes", "offset.security_hook_heads",
            "offset.slide_nfulnl_logger", "offset.slide_loggers_0_1", "offset.slide_boot_id",
            "kernel_phys_load", "compact_waiter",
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
            "execution.handoff.pre_dispatch_settle_ms", "execution.handoff.module_poll_attempts",
            "execution.handoff.module_poll_interval_ms", "execution.handoff.enforce_poll_attempts",
            "execution.handoff.enforce_poll_interval_ms",
            "execution.routes.select_stack.consumer_max_calls",
            "execution.routes.select_stack.consumer_burst_calls",
            "safe_mode",
        )
        check(commonFieldPaths.size == 68) { "common field table drifted: ${commonFieldPaths.size}" }

        /* key (wire name) -> dotted HOCON path, per route. */
        val routeFieldPaths = mapOf(
            "tcp_zerocopy" to listOf(
                "tcp_attempts" to "execution.routes.tcp_zerocopy.attempts",
                "tcp_arm_sequence" to "execution.routes.tcp_zerocopy.arm_sequence",
                "tcp_post_receive_hold_iterations" to
                    "execution.routes.tcp_zerocopy.post_receive_hold_iterations",
            ),
            "select_stack" to listOf(
                "pselect_waiter_shift" to "pselect_waiter_shift",
                "select_enter_delay_us" to "execution.routes.select_stack.enter_delay_us",
                "select_timeout_us" to "execution.routes.select_stack.timeout_us",
            ),
            "multicast_waiter" to listOf(
                "mcast_waiter_off" to "mcast.waiter_off",
                "mcast_buffer_size" to "mcast.buffer_size",
                "mcast_task_offset" to "mcast.task_offset",
                "mcast_lock_offset" to "mcast.lock_offset",
                "mcast_fake_lock_offset" to "mcast.fake_lock_offset",
                "mcast_fake_task_offset" to "mcast.fake_task_offset",
                "mcast_lock_slots_offset" to "mcast.lock_slots_offset",
                "mcast_lock_slot_count" to "mcast.lock_slot_count",
                "mcast_lock_slot_stride" to "mcast.lock_slot_stride",
                "off_mcast_fake_bss" to "offset.mcast_fake_bss",
                "multicast_resident" to "multicast_resident",
                "multicast_ready_timeout_ms" to
                    "execution.routes.multicast_waiter.ready_timeout_ms",
                "multicast_post_requeue_settle_us" to
                    "execution.routes.multicast_waiter.post_requeue_settle_us",
                "multicast_post_adjust_settle_us" to
                    "execution.routes.multicast_waiter.post_adjust_settle_us",
            ),
        )

        fun serialize(
            release: String,
            route: Int,
            kernelMajor: Long,
            recommendShizuku: Long,
            fallbackRoute: Int,
            common: LongArray,
            routeFields: List<Pair<String, Long>>,
        ): ByteArray {
            val releaseBytes = release.toByteArray(Charsets.UTF_8)
            require(releaseBytes.size <= 0xffff) { "release is too long: $release" }
            val keyBytes = routeFields.map { it.first.toByteArray(Charsets.UTF_8) }
            var size = 12 + releaseBytes.size + common.size * 8 + 1
            keyBytes.forEach { size += 1 + it.size + 8 }
            val buffer = java.nio.ByteBuffer
                .allocate(size)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
            buffer.putInt(0x0D000721)
            buffer.putShort(2)          // layout version
            buffer.put(route.toByte())
            buffer.put(kernelMajor.toByte())
            buffer.put(recommendShizuku.toByte())
            buffer.put(fallbackRoute.toByte())
            buffer.putShort(releaseBytes.size.toShort())
            buffer.put(releaseBytes)
            common.forEach(buffer::putLong)
            buffer.put(routeFields.size.toByte())
            for (i in routeFields.indices) {
                buffer.put(keyBytes[i].size.toByte())
                buffer.put(keyBytes[i])
                buffer.putLong(routeFields[i].second)
            }
            return buffer.array()
        }

        var exported = 0
        srcDir.listFiles { entry -> entry.isFile && entry.name.endsWith(".conf") }
            ?.sortedBy { it.name }
            ?.forEach { file ->
                val profile = parse(file.name)?.let { fillRouteExecution(it) } ?: return@forEach
                val release = profile["release"] as? String ?: return@forEach
                val routeName = routeNameOf(profile)
                val fallbackName = fallbackOf(profile)
                val routeFields = (routeFieldPaths[routeName] ?: emptyList())
                    .map { it.first to nativeValue(profile, routeName, fallbackName, it.second) }
                val bytes = serialize(
                    release = release,
                    route = routeWire(routeName),
                    kernelMajor = lookup(profile, "kernel_major"),
                    recommendShizuku = lookup(profile, "recommend_shizuku"),
                    fallbackRoute = routeWire(fallbackName),
                    common = LongArray(commonFieldPaths.size) {
                        nativeValue(profile, routeName, fallbackName, commonFieldPaths[it])
                    },
                    routeFields = routeFields,
                )
                File(outDir, "$release.bin").writeBytes(bytes)
                exported++
                logger.lifecycle("exportKernelProfiles: $release (${bytes.size} bytes)")
            }
        logger.lifecycle("exportKernelProfiles: $exported profile(s) -> ${outDir.absolutePath}")
    }
}
