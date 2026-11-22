@file:Suppress("UnstableApiUsage")

import com.android.build.api.artifact.SingleArtifact
import com.android.build.api.variant.HostTestBuilder
import groovy.json.JsonOutput
import groovy.json.JsonSlurper
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.RandomAccessFile
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream
import javax.inject.Inject
import org.gradle.api.attributes.Category
import org.gradle.api.attributes.LibraryElements
import org.gradle.api.attributes.Usage
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.tasks.Classpath
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.LocalState
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.TaskAction
import org.gradle.process.ExecOperations

plugins {
    id("com.android.library")
}

abstract class PackageGlPluginTask : DefaultTask() {
    @get:InputFile
    abstract val manifestSource: RegularFileProperty

    @get:InputDirectory
    abstract val licenseDirectory: DirectoryProperty

    @get:InputFile
    abstract val releaseAar: RegularFileProperty

    @get:Classpath
    abstract val runtimeClasspath: ConfigurableFileCollection

    @get:Classpath
    abstract val apiClasspath: ConfigurableFileCollection

    @get:Classpath
    abstract val androidBootClasspath: ConfigurableFileCollection

    @get:InputFile
    abstract val d8Executable: RegularFileProperty

    @get:InputFile
    abstract val dexdumpExecutable: RegularFileProperty

    @get:InputFile
    abstract val llvmNmExecutable: RegularFileProperty

    @get:LocalState
    abstract val workDirectory: DirectoryProperty

    @get:OutputFile
    abstract val archiveFile: RegularFileProperty

    @get:OutputFile
    abstract val contentRootFile: RegularFileProperty

    @get:Inject
    abstract val execOperations: ExecOperations

    @TaskAction
    fun packagePlugin() {
        val work = workDirectory.get().asFile
        work.deleteRecursively()
        val program = File(work, "program").also(File::mkdirs)
        val dexOutput = File(work, "dex").also(File::mkdirs)
        val payload = File(work, "payload").also(File::mkdirs)
        val nativeOutput = File(payload, "lib/arm64-v8a").also(File::mkdirs)

        val programInputs = mutableListOf<File>()
        val projectAar = releaseAar.get().asFile
        ZipFile(projectAar).use { archive ->
            programInputs += extractRequired(archive, "classes.jar", File(program, "project.jar"))
            extractRequired(
                archive,
                "jni/arm64-v8a/libghostlock_diagnose.so",
                File(nativeOutput, "libghostlock_diagnose.so"),
            )
        }

        var jnaNativeFound = false
        runtimeClasspath.files.sortedBy(File::getName).forEachIndexed { index, dependency ->
            when (dependency.extension.lowercase()) {
                "jar" -> programInputs += dependency
                "aar" -> ZipFile(dependency).use { archive ->
                    programInputs += extractRequired(
                        archive,
                        "classes.jar",
                        File(program, "runtime-$index.jar"),
                    )
                    archive.getEntry("jni/arm64-v8a/libjnidispatch.so")?.let { entry ->
                        require(!jnaNativeFound) { "multiple JNA arm64 native libraries in runtime closure" }
                        extract(archive, entry, File(nativeOutput, "libjnidispatch.so"))
                        jnaNativeFound = true
                    }
                }
                else -> error("unsupported plugin runtime artifact: ${dependency.name}")
            }
        }
        require(jnaNativeFound) { "JNA arm64 native library is missing from runtime closure" }

        val d8Arguments = buildList {
            add(d8Executable.get().asFile.absolutePath)
            add("--release")
            add("--min-api")
            add("31")
            androidBootClasspath.files.sortedBy(File::getName).forEach { library ->
                add("--lib")
                add(library.absolutePath)
            }
            apiClasspath.files.sortedBy(File::getName).forEach { api ->
                add("--lib")
                add(api.absolutePath)
            }
            add("--output")
            add(dexOutput.absolutePath)
            programInputs.distinctBy(File::getCanonicalPath).forEach { add(it.absolutePath) }
        }
        execOperations.exec { commandLine(d8Arguments) }.assertNormalExitValue()

        val dexFiles = dexOutput.listFiles()
            .orEmpty()
            .filter { it.name.matches(Regex("classes(?:[2-9][0-9]*)?\\.dex")) }
            .sortedBy(::dexOrdinal)
        require(dexFiles.isNotEmpty()) { "D8 produced no classes.dex" }
        require(dexFiles.map(::dexOrdinal) == (1..dexFiles.size).toList()) {
            "D8 produced a non-contiguous multidex set"
        }
        verifyDexClassOwnership(dexFiles)
        dexFiles.forEach { dex ->
            Files.copy(
                dex.toPath(),
                File(payload, "code/${dex.name}").also { it.parentFile.mkdirs() }.toPath(),
                StandardCopyOption.REPLACE_EXISTING,
            )
        }

        val notices = listOf("LICENSE", "AL2.0", "LGPL2.1")
        notices.forEach { name ->
            val source = File(licenseDirectory.get().asFile, name)
            require(source.isFile) { "missing JNA notice $name" }
            val target = File(payload, "licenses/jna/$name").also { it.parentFile.mkdirs() }
            Files.copy(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING)
        }

        val source = JsonSlurper().parse(manifestSource.get().asFile) as Map<*, *>
        val allowedSourceKeys = setOf(
            "formatMajor", "formatMinor", "publisherId", "packageId", "versionCode",
            "versionName", "variant", "minAndroidSdk", "hostApiMajor", "minHostApiMinor",
            "nativeLibraries", "components",
        )
        require(source.keys == allowedSourceKeys) { "manifest-source keys differ from format declaration" }
        val sourceNative = (source.requiredList("nativeLibraries")).associateBy { declaration ->
            declaration.requiredString("path")
        }
        val nativePaths = nativeOutput.listFiles().orEmpty().associateBy { file ->
            "lib/arm64-v8a/${file.name}"
        }
        require(sourceNative.keys == nativePaths.keys) {
            "native declarations do not match package native payload"
        }
        val nativeLibraries = nativePaths.keys.sorted().map { path ->
            val declaration = requireNotNull(sourceNative[path])
            require(declaration.keys == setOf("path", "abi", "logicalLoadName")) {
                "unexpected native declaration field for $path"
            }
            val facts = inspectElf(requireNotNull(nativePaths[path]))
            require(facts.elfClass == 2 && facts.machine == 183 && facts.aligned16KiB) {
                "$path is not a 16 KiB aligned AArch64 ELF64 library"
            }
            val packageSonames = nativePaths.keys.mapTo(mutableSetOf()) { it.substringAfterLast('/') }
            facts.needed.forEach { needed ->
                require(needed in PLATFORM_NEEDED || needed in packageSonames) {
                    "$path depends on undeclared private library $needed"
                }
            }
            if (path.endsWith("libghostlock_diagnose.so")) {
                require(facts.soname == "libghostlock_diagnose.so") {
                    "Diagnose DSO must declare its exact SONAME"
                }
                verifyDiagnoseExports(requireNotNull(nativePaths[path]))
            } else {
                require(facts.soname == null) { "JNA arm64 DSO must not fabricate a SONAME" }
            }
            linkedMapOf<String, Any?>(
                "path" to path,
                "abi" to declaration.requiredString("abi"),
                "elfClass" to facts.elfClass,
                "elfMachine" to facts.machine,
                "soname" to facts.soname,
                "logicalLoadName" to declaration.requiredString("logicalLoadName"),
                "neededLibraries" to facts.needed.sorted(),
            )
        }

        val files = payload.walkTopDown()
            .filter(File::isFile)
            .map { file ->
                linkedMapOf<String, Any?>(
                    "path" to file.relativeTo(payload).invariantSeparatorsPath,
                    "sha256" to sha256(file),
                )
            }
            .sortedBy { it.getValue("path") as String }
            .toList()
        val components = source.requiredList("components").map { declaration ->
            require(
                declaration.keys == setOf(
                    "kind", "id", "backendKey", "entryContract", "entryClass",
                    "selectionPolicy", "abortPolicy", "executionDomains", "runCapability",
                ),
            ) { "unexpected component declaration field" }
            linkedMapOf<String, Any?>(
                "kind" to declaration.requiredString("kind"),
                "id" to declaration.requiredString("id"),
                "backendKey" to declaration.requiredString("backendKey"),
                "entryContract" to declaration.requiredString("entryContract"),
                "entryClass" to declaration.requiredString("entryClass"),
                "selectionPolicy" to declaration.requiredString("selectionPolicy"),
                "abortPolicy" to declaration.requiredString("abortPolicy"),
                "executionDomains" to declaration.requiredStringList("executionDomains").sorted(),
                "runCapability" to declaration.requiredString("runCapability"),
            )
        }.sortedBy { it.getValue("id") as String }

        val manifest = linkedMapOf<String, Any?>(
            "formatMajor" to source.requiredNumber("formatMajor"),
            "formatMinor" to source.requiredNumber("formatMinor"),
            "publisherId" to source.requiredString("publisherId"),
            "packageId" to source.requiredString("packageId"),
            "versionCode" to source.requiredNumber("versionCode"),
            "versionName" to source.requiredString("versionName"),
            "variant" to source.requiredString("variant"),
            "minAndroidSdk" to source.requiredNumber("minAndroidSdk"),
            "hostApiMajor" to source.requiredNumber("hostApiMajor"),
            "minHostApiMinor" to source.requiredNumber("minHostApiMinor"),
            "files" to files,
            "nativeLibraries" to nativeLibraries,
            "components" to components,
        )
        val manifestBytes = JsonOutput.toJson(manifest).toByteArray(Charsets.UTF_8)
        val contentRoot = sha256(
            "GhostLockPluginContent/1".toByteArray(Charsets.UTF_8) + byteArrayOf(0) + manifestBytes,
        )

        val output = archiveFile.get().asFile
        output.parentFile.mkdirs()
        ZipOutputStream(output.outputStream()).use { zip ->
            putZipEntry(zip, "manifest.json", manifestBytes)
            payload.walkTopDown()
                .filter(File::isFile)
                .sortedBy { it.relativeTo(payload).invariantSeparatorsPath }
                .forEach { file ->
                    putZipEntry(zip, file.relativeTo(payload).invariantSeparatorsPath, file)
                }
        }
        contentRootFile.get().asFile.apply {
            parentFile.mkdirs()
            writeText("$contentRoot\n")
        }
    }

    private fun verifyDexClassOwnership(dexFiles: List<File>) {
        val seen = mutableSetOf<String>()
        val protectedPrefixes = listOf(
            "Lcom/ghostlock/plugin/api/v1/", "Lcom/ghostlock/app/", "Ljava/", "Ljavax/",
            "Landroid/", "Ldalvik/", "Llibcore/",
        )
        dexFiles.forEach { dex ->
            val output = ByteArrayOutputStream()
            execOperations.exec {
                commandLine(dexdumpExecutable.get().asFile.absolutePath, "-d", dex.absolutePath)
                standardOutput = output
            }.assertNormalExitValue()
            val descriptors = DEX_DESCRIPTOR.findAll(output.toString(Charsets.UTF_8))
                .map { it.groupValues[1] }
                .toList()
            descriptors.forEach { descriptor ->
                require(protectedPrefixes.none(descriptor::startsWith)) {
                    "plugin DEX defines protected class $descriptor"
                }
                require(seen.add(descriptor)) { "duplicate plugin DEX class $descriptor" }
            }
        }
    }

    private fun verifyDiagnoseExports(file: File) {
        val output = ByteArrayOutputStream()
        execOperations.exec {
            commandLine(
                llvmNmExecutable.get().asFile.absolutePath,
                "-D",
                "--defined-only",
                file.absolutePath,
            )
            standardOutput = output
        }.assertNormalExitValue()
        val symbols = output.toString(Charsets.UTF_8).lineSequence()
            .map(String::trim)
            .filter(String::isNotEmpty)
            .map { it.substringAfterLast(' ') }
            .toList()
        require(symbols == listOf("gl_diagnose_run")) {
            "Diagnose DSO exports differ from gl_diagnose_run: $symbols"
        }
    }

    private fun extractRequired(archive: ZipFile, path: String, target: File): File {
        val entry = requireNotNull(archive.getEntry(path)) { "missing archive entry $path" }
        extract(archive, entry, target)
        return target
    }

    private fun extract(archive: ZipFile, entry: java.util.zip.ZipEntry, target: File) {
        target.parentFile.mkdirs()
        archive.getInputStream(entry).use { input ->
            target.outputStream().use { output -> input.copyTo(output, 16 * 1024) }
        }
    }

    private fun putZipEntry(output: ZipOutputStream, path: String, bytes: ByteArray) {
        output.putNextEntry(ZipEntry(path).also { it.time = 0L })
        output.write(bytes)
        output.closeEntry()
    }

    private fun putZipEntry(output: ZipOutputStream, path: String, file: File) {
        output.putNextEntry(ZipEntry(path).also { it.time = 0L })
        file.inputStream().use { input -> input.copyTo(output, STREAM_BUFFER_BYTES) }
        output.closeEntry()
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(STREAM_BUFFER_BYTES)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                if (count > 0) digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString(separator = "") { byte ->
            "%02x".format(byte.toInt() and 0xff)
        }
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes)
        .joinToString(separator = "") { byte -> "%02x".format(byte.toInt() and 0xff) }

    private companion object {
        const val STREAM_BUFFER_BYTES = 16 * 1024
        val DEX_DESCRIPTOR = Regex("Class descriptor\\s+: '([^']+)'" )
        val PLATFORM_NEEDED = setOf("libc.so", "libdl.so", "liblog.so", "libm.so")
    }

private data class BuildElfFacts(
    val elfClass: Int,
    val machine: Int,
    val soname: String?,
    val needed: List<String>,
    val aligned16KiB: Boolean,
)

private data class BuildElfLoad(
    val fileOffset: Long,
    val virtualAddress: Long,
    val fileSize: Long,
    val alignment: Long,
)

private fun inspectElf(file: File): BuildElfFacts = RandomAccessFile(file, "r").use { input ->
    require(input.length() >= 64) { "ELF header is truncated: ${file.name}" }
    val ident = ByteArray(16).also(input::readFully)
    require(ident.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46))) {
        "ELF magic is invalid: ${file.name}"
    }
    val elfClass = ident[4].toInt() and 0xff
    require(elfClass == 2 && (ident[5].toInt() and 0xff) == 1) {
        "ELF must be little-endian ELF64: ${file.name}"
    }
    val machine = input.readUInt16LeAt(18)
    val programOffset = input.readUInt64LeAt(32)
    val programEntrySize = input.readUInt16LeAt(54)
    val programCount = input.readUInt16LeAt(56)
    require(programEntrySize >= 56 && programOffset <= input.length()) { "invalid ELF program table" }
    require(programCount.toLong() <= (input.length() - programOffset) / programEntrySize) {
        "ELF program table is outside file"
    }
    val loads = mutableListOf<BuildElfLoad>()
    var dynamicOffset: Long? = null
    var dynamicSize = 0L
    repeat(programCount) { ordinal ->
        val offset = programOffset + ordinal.toLong() * programEntrySize
        val type = input.readUInt32LeAt(offset)
        val fileOffset = input.readUInt64LeAt(offset + 8)
        val virtualAddress = input.readUInt64LeAt(offset + 16)
        val fileSize = input.readUInt64LeAt(offset + 32)
        val alignment = input.readUInt64LeAt(offset + 48)
        require(fileOffset <= input.length() && fileSize <= input.length() - fileOffset) {
            "ELF segment is outside file"
        }
        when (type) {
            1L -> loads += BuildElfLoad(fileOffset, virtualAddress, fileSize, alignment)
            2L -> {
                dynamicOffset = fileOffset
                dynamicSize = fileSize
            }
        }
    }
    require(loads.isNotEmpty()) { "ELF has no load segment" }
    val aligned = loads.all { load ->
        load.alignment >= 16L * 1024L &&
            load.alignment and (load.alignment - 1) == 0L &&
            Math.floorMod(load.virtualAddress - load.fileOffset, 16L * 1024L) == 0L
    }
    var stringAddress: Long? = null
    var stringSize: Long? = null
    var sonameOffset: Long? = null
    val neededOffsets = mutableListOf<Long>()
    val start = dynamicOffset
    if (start != null) {
        require(dynamicSize % 16L == 0L) { "invalid ELF dynamic table size" }
        for (ordinal in 0 until dynamicSize / 16L) {
            val offset = start + ordinal * 16L
            when (input.readInt64LeAt(offset)) {
                0L -> break
                1L -> neededOffsets += input.readUInt64LeAt(offset + 8)
                5L -> stringAddress = input.readUInt64LeAt(offset + 8)
                10L -> stringSize = input.readUInt64LeAt(offset + 8)
                14L -> sonameOffset = input.readUInt64LeAt(offset + 8)
            }
        }
    }
    if (neededOffsets.isEmpty() && sonameOffset == null) {
        return@use BuildElfFacts(elfClass, machine, null, emptyList(), aligned)
    }
    val tableAddress = requireNotNull(stringAddress) { "ELF dynamic string table is missing" }
    val tableSize = requireNotNull(stringSize) { "ELF dynamic string size is missing" }
    val tableOffset = loads.firstNotNullOfOrNull { load ->
        val relative = tableAddress - load.virtualAddress
        if (tableAddress >= load.virtualAddress &&
            relative <= load.fileSize && tableSize <= load.fileSize - relative
        ) {
            load.fileOffset + relative
        } else {
            null
        }
    } ?: error("ELF dynamic string table is outside a load segment")
    fun stringAt(relative: Long): String {
        require(relative in 0 until tableSize) { "ELF dynamic string offset is invalid" }
        input.seek(tableOffset + relative)
        return buildString {
            while (input.filePointer < tableOffset + tableSize) {
                val byte = input.read()
                require(byte >= 0) { "unterminated ELF dynamic string" }
                if (byte == 0) return@buildString
                require(byte in 0x20..0x7e) { "non-ASCII ELF dynamic string" }
                append(byte.toChar())
            }
            error("unterminated ELF dynamic string")
        }
    }
    val needed = neededOffsets.map(::stringAt)
    require(needed.toSet().size == needed.size) { "duplicate ELF DT_NEEDED" }
    BuildElfFacts(elfClass, machine, sonameOffset?.let(::stringAt), needed, aligned)
}

/** Reopens the sole package artifact and verifies the bytes actually offered to consumers. */
abstract class VerifyGlPluginZipTask : DefaultTask() {
    @get:InputFile
    abstract val archiveFile: RegularFileProperty

    @get:InputFile
    abstract val contentRootFile: RegularFileProperty

    @TaskAction
    fun verifyArchive() {
        val archive = archiveFile.get().asFile
        val expectedRoot = contentRootFile.get().asFile.readText(Charsets.UTF_8).trim()
        require(expectedRoot.matches(Regex("[0-9a-f]{64}"))) { "invalid producer content root" }
        ZipFile(archive).use { zip ->
            val entries = zip.entries().asSequence().toList()
            entries.forEach { entry ->
                require(entry.name.isNotEmpty() && !entry.name.startsWith('/') && '\\' !in entry.name) {
                    "unsafe ZIP path: ${entry.name}"
                }
                val pathForSegments = entry.name.removeSuffix("/")
                require(pathForSegments.isNotEmpty() &&
                    pathForSegments.split('/').none { it.isEmpty() || it == "." || it == ".." }
                ) {
                    "unsafe ZIP segment: ${entry.name}"
                }
                require(entry.method == ZipEntry.STORED || entry.method == ZipEntry.DEFLATED) {
                    "unsupported ZIP method for ${entry.name}"
                }
            }
            val regular = entries.filterNot { it.isDirectory }
            require(regular.map { it.name }.toSet().size == regular.size) { "duplicate regular ZIP path" }
            val manifests = regular.filter { it.name == "manifest.json" }
            require(manifests.size == 1) { "package must contain exactly one manifest.json" }
            val manifestBytes = zip.getInputStream(manifests.single()).use { input ->
                val output = ByteArrayOutputStream()
                input.copyTo(output, STREAM_BUFFER_BYTES)
                output.toByteArray()
            }
            val manifest = JsonSlurper().parse(manifestBytes) as? Map<*, *>
                ?: error("package manifest must be a JSON object")
            require(
                manifest.keys == setOf(
                    "formatMajor", "formatMinor", "publisherId", "packageId", "versionCode",
                    "versionName", "variant", "minAndroidSdk", "hostApiMajor", "minHostApiMinor",
                    "files", "nativeLibraries", "components",
                ),
            ) { "package manifest fields differ from format 1.0" }
            require(manifest.requiredNumber("formatMajor").toInt() == 1) { "unsupported manifest major" }
            require(manifest.requiredNumber("formatMinor").toInt() == 0) { "unsupported manifest minor" }
            require(manifest.requiredString("publisherId") == "com.ghostlock.firstparty") {
                "unexpected package publisher"
            }
            require(manifest.requiredString("packageId") == "com.ghostlock.diagnose") {
                "unexpected package identity"
            }
            require(manifest.requiredString("variant") == "android-arm64-v8a") {
                "unexpected package variant"
            }
            val declared = manifest.requiredList("files").associate { file ->
                require(file.keys == setOf("path", "sha256")) { "invalid files[] declaration" }
                file.requiredString("path") to file.requiredString("sha256")
            }
            require(declared.size == manifest.requiredList("files").size) { "duplicate files[] path" }
            val payload = regular.filterNot { it.name == "manifest.json" }
            require(payload.map { it.name }.toSet() == declared.keys) {
                "manifest files[] does not exactly cover package payload"
            }
            payload.forEach { entry ->
                val actual = zip.getInputStream(entry).use(::sha256Stream)
                require(actual == declared.getValue(entry.name)) { "payload digest mismatch: ${entry.name}" }
            }
            val dexPaths = declared.keys.filter { it.startsWith("code/") && it.endsWith(".dex") }
            require("code/classes.dex" in dexPaths) { "code/classes.dex is missing" }
            val dexOrdinals = dexPaths.map { path ->
                if (path == "code/classes.dex") 1 else {
                    require(path.matches(Regex("code/classes[2-9][0-9]*\\.dex"))) {
                        "invalid multidex path: $path"
                    }
                    path.removePrefix("code/classes").removeSuffix(".dex").toIntOrNull()
                        ?: error("multidex ordinal overflow: $path")
                }
            }.sorted()
            require(dexOrdinals == (1..dexOrdinals.size).toList()) { "multidex paths are not contiguous" }
            require(
                setOf(
                    "lib/arm64-v8a/libghostlock_diagnose.so",
                    "lib/arm64-v8a/libjnidispatch.so",
                ).all(declared::containsKey),
            ) { "required Diagnose native payload is incomplete" }
            val actualRoot = sha256(
                "GhostLockPluginContent/1".toByteArray(Charsets.UTF_8) +
                    byteArrayOf(0) + manifestBytes,
            )
            require(actualRoot == expectedRoot) { "semantic content root differs from producer output" }
        }
        logger.lifecycle("GLPLUGIN_ZIP=PASS ROOT={} ARCHIVE={}", expectedRoot, archive.name)
    }

    private fun sha256Stream(input: java.io.InputStream): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(STREAM_BUFFER_BYTES)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            if (count > 0) digest.update(buffer, 0, count)
        }
        return digest.digest().joinToString(separator = "") { byte ->
            "%02x".format(byte.toInt() and 0xff)
        }
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes)
        .joinToString(separator = "") { byte -> "%02x".format(byte.toInt() and 0xff) }

    private fun Map<*, *>.requiredString(key: String): String =
        requireNotNull(this[key] as? String) { "$key must be a string" }

    private fun Map<*, *>.requiredNumber(key: String): Number =
        requireNotNull(this[key] as? Number) { "$key must be a number" }

    private fun Map<*, *>.requiredList(key: String): List<Map<*, *>> =
        requireNotNull(this[key] as? List<*>) { "$key must be an array" }.map { value ->
            requireNotNull(value as? Map<*, *>) { "$key entries must be objects" }
        }

    private companion object {
        const val STREAM_BUFFER_BYTES = 16 * 1024
    }
}

private fun RandomAccessFile.readUInt16LeAt(offset: Long): Int {
    seek(offset)
    val first = read()
    val second = read()
    require(first >= 0 && second >= 0) { "unexpected EOF" }
    return first or (second shl 8)
}

private fun RandomAccessFile.readUInt32LeAt(offset: Long): Long {
    seek(offset)
    var value = 0L
    repeat(4) { byte ->
        val next = read()
        require(next >= 0) { "unexpected EOF" }
        value = value or (next.toLong() shl (byte * 8))
    }
    return value
}

private fun RandomAccessFile.readUInt64LeAt(offset: Long): Long {
    val value = readInt64LeAt(offset)
    require(value >= 0) { "ELF unsigned value exceeds supported file range" }
    return value
}

private fun RandomAccessFile.readInt64LeAt(offset: Long): Long {
    seek(offset)
    var value = 0L
    repeat(8) { byte ->
        val next = read()
        require(next >= 0) { "unexpected EOF" }
        value = value or (next.toLong() shl (byte * 8))
    }
    return value
}

private fun Map<*, *>.requiredString(key: String): String =
    requireNotNull(this[key] as? String) { "$key must be a string" }

private fun Map<*, *>.requiredNumber(key: String): Number =
    requireNotNull(this[key] as? Number) { "$key must be a number" }

private fun Map<*, *>.requiredList(key: String): List<Map<*, *>> =
    requireNotNull(this[key] as? List<*>) { "$key must be an array" }.map { value ->
        requireNotNull(value as? Map<*, *>) { "$key entries must be objects" }
    }

private fun Map<*, *>.requiredStringList(key: String): List<String> =
    requireNotNull(this[key] as? List<*>) { "$key must be an array" }.map { value ->
        requireNotNull(value as? String) { "$key entries must be strings" }
    }

private fun dexOrdinal(file: File): Int = if (file.name == "classes.dex") {
    1
} else {
    file.name.removePrefix("classes").removeSuffix(".dex").toInt()
}
}

layout.buildDirectory.set(rootProject.layout.buildDirectory.dir("plugin/packages/diagnose"))

val androidBuildToolsVersion = "37.0.0"
val windowsHost = System.getProperty("os.name").startsWith("Windows", ignoreCase = true)

android {
    namespace = "com.ghostlock.plugin.diagnose"
    ndkVersion = "30.0.16248370"
    buildToolsVersion = androidBuildToolsVersion
    compileSdk {
        version = release(37) {
            minorApiLevel = 2
        }
    }
    defaultConfig {
        minSdk = 31
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            buildStagingDirectory = rootProject.layout.buildDirectory.dir("cxx/diagnose").get().asFile
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
    testOptions {
        unitTests.isReturnDefaultValues = true
    }
    packaging {
        jniLibs.useLegacyPackaging = true
    }
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    compileOnly(project(":plugin:api"))
    implementation("net.java.dev.jna:jna:5.19.1@aar")

    testImplementation(project(":plugin:api"))
    testImplementation("junit:junit:4.13.2")
}

val pluginApiLibrary = configurations.create("pluginApiLibrary") {
    isCanBeConsumed = false
    isCanBeResolved = true
    attributes {
        attribute(Usage.USAGE_ATTRIBUTE, objects.named(Usage.JAVA_RUNTIME))
        attribute(Category.CATEGORY_ATTRIBUTE, objects.named(Category.LIBRARY))
        attribute(LibraryElements.LIBRARY_ELEMENTS_ATTRIBUTE, objects.named(LibraryElements.JAR))
    }
}
dependencies.add(pluginApiLibrary.name, project(":plugin:api"))

val sdkDirectory = androidComponents.sdkComponents.sdkDirectory
val ndkDirectory = androidComponents.sdkComponents.ndkDirectory
val llvmNm = ndkDirectory.map { ndk ->
    val prebuiltRoot = ndk.dir("toolchains/llvm/prebuilt").asFile
    val executableName = if (windowsHost) "llvm-nm.exe" else "llvm-nm"
    val candidates = prebuiltRoot.listFiles()
        .orEmpty()
        .asSequence()
        .filter(File::isDirectory)
        .map { hostDirectory -> File(hostDirectory, "bin/$executableName") }
        .filter { candidate ->
            candidate.isFile && (windowsHost || candidate.canExecute())
        }
        .map(File::getCanonicalFile)
        .sortedBy(File::getAbsolutePath)
        .toList()
    require(candidates.size == 1) {
        "expected exactly one executable $executableName under ${prebuiltRoot.absolutePath}, " +
            "found ${candidates.joinToString { it.absolutePath }}"
    }
    candidates.single()
}
fun androidBuildTool(baseName: String, windowsSuffix: String) = sdkDirectory.map { sdk ->
    val executableName = if (windowsHost) "$baseName$windowsSuffix" else baseName
    val executable = sdk.file("build-tools/$androidBuildToolsVersion/$executableName")
    val file = executable.asFile
    require(file.isFile && (windowsHost || file.canExecute())) {
        "Android build tool is missing or not executable: ${file.absolutePath}"
    }
    executable
}
val d8 = androidBuildTool("d8", ".bat")
val dexdump = androidBuildTool("dexdump", ".exe")

val packageGlPlugin = tasks.register<PackageGlPluginTask>("packageGlPlugin") {
    description = "Build the canonical arm64 Diagnose .glplugin.zip artifact."
    manifestSource.set(layout.projectDirectory.file("src/main/plugin/manifest-source.json"))
    licenseDirectory.set(layout.projectDirectory.dir("src/main/plugin/licenses/jna"))
    runtimeClasspath.from(configurations.named("releaseRuntimeClasspath"))
    apiClasspath.from(pluginApiLibrary)
    d8Executable.set(d8)
    dexdumpExecutable.set(dexdump)
    llvmNmExecutable.set(layout.file(llvmNm))
    workDirectory.set(layout.buildDirectory.dir("intermediates/glplugin/package"))
    archiveFile.set(
        layout.buildDirectory.file(
            "outputs/glplugin/com.ghostlock.diagnose-1-android-arm64-v8a.glplugin.zip",
        ),
    )
    contentRootFile.set(layout.buildDirectory.file("outputs/glplugin/plugin-content-root.txt"))
}
val verifyGlPluginZip = tasks.register<PackageGlPluginTask.VerifyGlPluginZipTask>("verifyGlPluginZip") {
    group = "verification"
    description = "Reopen and verify the final Diagnose .glplugin.zip artifact."
    archiveFile.set(packageGlPlugin.flatMap(PackageGlPluginTask::archiveFile))
    contentRootFile.set(packageGlPlugin.flatMap(PackageGlPluginTask::contentRootFile))
}
androidComponents.onVariants(androidComponents.selector().withBuildType("release")) { variant ->
    packageGlPlugin.configure {
        releaseAar.set(variant.artifacts.get(SingleArtifact.AAR))
        // SdkComponents documents this provider as execution-time-only; the getter itself is
        // available only after AGP has finalized the variant's Java compatibility settings.
        androidBootClasspath.from(androidComponents.sdkComponents.bootClasspath)
    }
}

androidComponents.beforeVariants(androidComponents.selector().withBuildType("release")) { variantBuilder ->
    variantBuilder.hostTests[HostTestBuilder.UNIT_TEST_TYPE]?.enable = true
}

val glPluginElements = configurations.create("glPluginElements") {
    isCanBeConsumed = true
    isCanBeResolved = false
    attributes {
        attribute(Usage.USAGE_ATTRIBUTE, objects.named("ghostlock-glplugin"))
        attribute(Category.CATEGORY_ATTRIBUTE, objects.named(Category.DOCUMENTATION))
        attribute(LibraryElements.LIBRARY_ELEMENTS_ATTRIBUTE, objects.named("glplugin-zip"))
    }
}
artifacts.add(glPluginElements.name, packageGlPlugin.flatMap(PackageGlPluginTask::archiveFile)) {
    builtBy(verifyGlPluginZip)
    type = "glplugin.zip"
}
