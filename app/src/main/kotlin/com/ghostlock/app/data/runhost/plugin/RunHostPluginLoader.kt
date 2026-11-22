package com.ghostlock.app.data.runhost.plugin

import android.os.Build
import android.os.ParcelFileDescriptor
import android.os.Process
import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.core.runhost.RunHostRequestValidation
import com.ghostlock.app.core.runhost.RunHostRequestValidator
import com.ghostlock.app.data.plugin.AndroidPluginArchiveVerifier
import com.ghostlock.app.data.plugin.VerifiedPluginArchive
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.runhost.host.RunHostRole
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.plugin.api.v1.DiagnosticBackendEntryPoint
import com.ghostlock.plugin.api.v1.DiagnosticBackendRun
import com.ghostlock.plugin.api.v1.PluginApi
import com.ghostlock.plugin.api.v1.PluginException
import com.ghostlock.plugin.api.v1.PluginFailureCode
import com.ghostlock.plugin.core.PluginAbortPolicy
import com.ghostlock.plugin.core.PluginContentSha256
import com.ghostlock.plugin.core.PluginExecutionDomain
import com.ghostlock.plugin.core.PluginManifestComponent
import com.ghostlock.plugin.core.PluginRunCapability
import com.ghostlock.plugin.core.PluginSelectionPolicy
import dalvik.system.DelegateLastClassLoader
import java.io.File
import java.io.FileInputStream
import java.lang.reflect.InvocationTargetException
import java.lang.reflect.Modifier
import java.security.MessageDigest
import org.apache.commons.compress.archivers.zip.ZipFile as CommonsZipFile

/** Successful package materialization bound to one idempotent execution owner. */
internal data class LoadedPluginBackend(
    val descriptor: BackendDescriptor,
    val flow: Flow,
    val resources: PluginRunResourceOwner,
)

/** Total loader result; a rejected tainted process must still follow the one-shot exit path. */
internal sealed interface PluginLoadResult {
    data class Ready(val backend: LoadedPluginBackend) : PluginLoadResult

    data class Rejected(
        val code: RunHostCode,
        val pluginTainted: Boolean = false,
    ) : PluginLoadResult
}

/**
 * RunHost-only verified archive copier, extractor and package-private classloader owner.
 *
 * No Host code loads native libraries. The returned plugin-defined backend invokes JNA/native through
 * the package classloader's single verified native search directory.
 */
internal class RunHostPluginLoader(
    private val verifier: AndroidPluginArchiveVerifier = AndroidPluginArchiveVerifier(),
    private val sdkInt: () -> Int = { Build.VERSION.SDK_INT },
    private val processIs64Bit: () -> Boolean = Process::is64Bit,
    private val machine: () -> String = { Os.uname().machine },
) {
    fun load(
        model: RunHostRequestModel,
        role: RunHostRole,
        workspace: AndroidRunWorkspace,
        archiveDescriptor: ParcelFileDescriptor,
        processDomainMatches: (ExecutionDomain) -> Boolean,
    ): PluginLoadResult {
        val copied = try {
            val expectedRoot = PluginContentSha256(model.pluginContentSha256)
            val packageRoot = File(
                workspace.pluginPackages,
                "00-" + expectedRoot.value.take(PACKAGE_ROOT_PREFIX_HEX),
            )
            createDirectory(packageRoot, OWNER_DIRECTORY_MODE)
            CopiedArchive(
                expectedRoot = expectedRoot,
                packageRoot = packageRoot,
                archive = copyArchive(archiveDescriptor, File(packageRoot, ARCHIVE_FILE_NAME)),
            )
        } catch (_: IllegalArgumentException) {
            return PluginLoadResult.Rejected(RunHostCode.INVALID_REQUEST)
        } catch (_: Throwable) {
            return PluginLoadResult.Rejected(RunHostCode.IO_FAILURE)
        } finally {
            archiveDescriptor.closeQuietly()
        }
        val expectedRoot = copied.expectedRoot
        val packageRoot = copied.packageRoot
        val archive = copied.archive

        val scratch = File(packageRoot, VERIFY_DIRECTORY)
        val verified = try {
            createDirectory(scratch, OWNER_DIRECTORY_MODE)
            verifier.verify(archive, scratch, expectedRoot)
        } catch (_: IllegalArgumentException) {
            return PluginLoadResult.Rejected(RunHostCode.ARTIFACT_INVALID)
        } catch (_: Throwable) {
            return PluginLoadResult.Rejected(RunHostCode.IO_FAILURE)
        } finally {
            if (scratch.exists()) runCatching { check(scratch.delete()) }
        }

        val component = verified.manifest.components.singleOrNull()
            ?: return PluginLoadResult.Rejected(RunHostCode.ARTIFACT_INVALID)
        if (component.backendKey != model.backendKey) {
            return PluginLoadResult.Rejected(RunHostCode.INVALID_REQUEST)
        }
        val compatibility = validateCompatibility(verified, component, role)
        if (compatibility != null) return PluginLoadResult.Rejected(compatibility)
        val descriptor = runCatching { component.toDescriptor() }
            .getOrElse { return PluginLoadResult.Rejected(RunHostCode.ARTIFACT_INVALID) }
        if (!runCatching { processDomainMatches(descriptor.executionDomain) }.getOrDefault(false)) {
            return PluginLoadResult.Rejected(RunHostCode.EXECUTION_DOMAIN_MISMATCH)
        }
        when (val shape = RunHostRequestValidator.validateCapability(model, descriptor.runCapability)) {
            RunHostRequestValidation.Valid -> Unit
            is RunHostRequestValidation.Invalid -> return PluginLoadResult.Rejected(shape.code)
        }

        val extracted = try {
            extractLoadFiles(archive, packageRoot, verified)
        } catch (_: IllegalArgumentException) {
            return PluginLoadResult.Rejected(RunHostCode.ARTIFACT_INVALID)
        } catch (_: Throwable) {
            return PluginLoadResult.Rejected(RunHostCode.IO_FAILURE)
        }
        val loader = try {
            DelegateLastClassLoader(
                extracted.dexFiles.joinToString(File.pathSeparator, transform = File::getAbsolutePath),
                extracted.nativeDirectory.absolutePath,
                PluginApiParentClassLoader(),
            )
        } catch (_: Throwable) {
            return PluginLoadResult.Rejected(RunHostCode.IO_FAILURE)
        }
        val resources = PluginRunResourceOwner()
        resources.markClassLoading(loader)
        return try {
            val entryClass = Class.forName(component.entryClass, false, loader)
            require(component.entryClass in verified.definedClasses) {
                "entry class is not defined by the package"
            }
            require(component.entryClass.startsWith(DIAGNOSE_ENTRY_NAMESPACE_PREFIX)) {
                "entry class is outside the Diagnose package namespace"
            }
            require(entryClass.classLoader === loader) { "entry class has the wrong defining loader" }
            require(!component.entryClass.startsWith(HOST_IMPLEMENTATION_PREFIX)) {
                "entry class is in the Host namespace"
            }
            require(DiagnosticBackendEntryPoint::class.java.isAssignableFrom(entryClass)) {
                "entry class does not implement the shared diagnostic contract"
            }
            require(Modifier.isPublic(entryClass.modifiers)) { "entry class is not public" }
            val constructor = entryClass.getConstructor()
            require(Modifier.isPublic(constructor.modifiers)) { "entry constructor is not public" }
            val entry = try {
                resources.withPluginContext {
                    constructor.newInstance() as DiagnosticBackendEntryPoint
                }
            } catch (problem: Throwable) {
                throw EntryPrepareFailure(problem)
            }
            val run: DiagnosticBackendRun = try {
                resources.withPluginContext {
                    requireNotNull(entry.createRun()) { "entry returned a null run" }
                }
            } catch (problem: Throwable) {
                throw EntryPrepareFailure(problem)
            }
            resources.register(run)
            PluginLoadResult.Ready(
                LoadedPluginBackend(
                    descriptor = descriptor,
                    flow = PluginBackendFlow(resources),
                    resources = resources,
                ),
            )
        } catch (problem: Throwable) {
            resources.close()
            PluginLoadResult.Rejected(
                code = mapEntryFailure(problem),
                pluginTainted = true,
            )
        }
    }

    /** Applies only facts that become authoritative after strict archive verification. */
    private fun validateCompatibility(
        verified: VerifiedPluginArchive,
        component: PluginManifestComponent,
        role: RunHostRole,
    ): RunHostCode? {
        val manifest = verified.manifest
        if (manifest.hostApiMajor != PluginApi.MAJOR || manifest.minHostApiMinor > PluginApi.MINOR) {
            return RunHostCode.NOT_READY
        }
        if (manifest.minAndroidSdk > sdkInt()) return RunHostCode.NOT_READY
        if (manifest.variant != ANDROID_ARM64_VARIANT ||
            !processIs64Bit() || machine() != AARCH64_MACHINE ||
            manifest.nativeLibraries.any { library -> library.abi != ARM64_ABI }
        ) {
            return RunHostCode.NOT_READY
        }
        if (component.entryContract != DIAGNOSTIC_ENTRY_CONTRACT ||
            component.runCapability != PluginRunCapability.DIAGNOSTIC_ONLY
        ) {
            return RunHostCode.NOT_READY
        }
        val expectedDomain = when (role) {
            RunHostRole.APP -> PluginExecutionDomain.APP
            RunHostRole.SHIZUKU_SHELL -> PluginExecutionDomain.SHIZUKU_SHELL
        }
        if (expectedDomain !in component.executionDomains) {
            return RunHostCode.EXECUTION_DOMAIN_MISMATCH
        }
        return null
    }

    /** Copies the read-only regular PFD through pread without trusting or changing its shared offset. */
    private fun copyArchive(source: ParcelFileDescriptor, target: File): File {
        val sourceStat = Os.fstat(source.fileDescriptor)
        val output = Os.open(
            target.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            READ_ONLY_FILE_MODE,
        )
        try {
            val buffer = ByteArray(STREAM_BUFFER_BYTES)
            var offset = 0L
            while (offset < sourceStat.st_size) {
                val requested = minOf(buffer.size.toLong(), sourceStat.st_size - offset).toInt()
                val count = Os.pread(source.fileDescriptor, buffer, 0, requested, offset)
                check(count > 0) { "plugin archive ended before fstat size" }
                writeFully(output, buffer, count)
                offset += count
            }
            check(Os.pread(source.fileDescriptor, buffer, 0, 1, offset) == 0) {
                "plugin archive grew while copying"
            }
            Os.fsync(output)
        } finally {
            Os.close(output)
        }
        Os.chmod(target.absolutePath, READ_ONLY_FILE_MODE)
        verifyOrdinaryReadOnlyFile(target)
        return target
    }

    /** Extracts only verified DEX/native entries and re-hashes each reopened 0400 output. */
    private fun extractLoadFiles(
        archive: File,
        packageRoot: File,
        verified: VerifiedPluginArchive,
    ): ExtractedPackage {
        val targets = verified.dexPaths + verified.nativeLibraryPaths
        val declared = verified.manifest.files.associateBy { it.path }
        val directories = targets.flatMap { path ->
            val directParent = requireNotNull(File(packageRoot, path).parentFile) {
                "load target has no parent directory"
            }
            generateSequence(directParent) { directory ->
                directory.parentFile?.takeUnless { it == packageRoot }
            }.toList()
        }
            .distinctBy(File::getAbsolutePath)
            .sortedBy { directory -> directory.toPath().nameCount }
        directories.forEach { directory -> createDirectory(directory, OWNER_DIRECTORY_MODE) }
        // Android's boot class path supplies Commons Compress; use its long-standing constructor
        // instead of a newer builder method that is absent from several supported platform images.
        @Suppress("DEPRECATION")
        CommonsZipFile(archive).use { zip ->
            targets.forEach { path ->
                val entry = requireNotNull(zip.getEntry(path)) { "verified load entry disappeared" }
                val target = File(packageRoot, path)
                val expected = requireNotNull(declared[path]).sha256
                val digest = MessageDigest.getInstance("SHA-256")
                val output = Os.open(
                    target.absolutePath,
                    OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                        OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
                    READ_ONLY_FILE_MODE,
                )
                try {
                    zip.getInputStream(entry).use { input ->
                        val buffer = ByteArray(STREAM_BUFFER_BYTES)
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            if (count == 0) continue
                            digest.update(buffer, 0, count)
                            writeFully(output, buffer, count)
                        }
                    }
                    Os.fsync(output)
                } finally {
                    Os.close(output)
                }
                Os.chmod(target.absolutePath, READ_ONLY_FILE_MODE)
                require(digest.digest().toLowerHex() == expected) { "extracted file digest mismatch" }
                require(hashReopened(target) == expected) { "reopened file digest mismatch" }
            }
        }
        directories.asReversed().forEach { directory ->
            Os.chmod(directory.absolutePath, SEARCH_ONLY_DIRECTORY_MODE)
            fsyncDirectory(directory)
        }
        Os.chmod(packageRoot.absolutePath, SEARCH_ONLY_DIRECTORY_MODE)
        fsyncDirectory(packageRoot)
        fsyncDirectory(requireNotNull(packageRoot.parentFile) { "package root has no parent directory" })
        val dexFiles = verified.dexPaths.map { path -> File(packageRoot, path) }
        val nativeDirectory = File(packageRoot, "lib/$ARM64_ABI")
        return ExtractedPackage(dexFiles, nativeDirectory)
    }

    private fun createDirectory(directory: File, mode: Int) {
        if (!directory.exists()) check(directory.mkdir()) { "cannot create plugin package directory" }
        val stat = Os.lstat(directory.absolutePath)
        check(stat.st_mode and OsConstants.S_IFMT == OsConstants.S_IFDIR) {
            "plugin package path is not a directory"
        }
        check(stat.st_uid == Process.myUid()) { "plugin package directory owner mismatch" }
        Os.chmod(directory.absolutePath, mode)
    }

    private fun verifyOrdinaryReadOnlyFile(file: File) {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        try {
            val stat = Os.fstat(descriptor)
            check(stat.st_mode and OsConstants.S_IFMT == OsConstants.S_IFREG) {
                "plugin package output is not a regular file"
            }
            check(stat.st_uid == Process.myUid()) { "plugin package output owner mismatch" }
            val flags = Os.fcntlInt(descriptor, OsConstants.F_GETFL, 0)
            check(flags and OsConstants.O_ACCMODE == OsConstants.O_RDONLY) {
                "plugin package output reopened writable"
            }
        } finally {
            Os.close(descriptor)
        }
    }

    private fun hashReopened(file: File): String {
        verifyOrdinaryReadOnlyFile(file)
        val raw = Os.open(
            file.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        return FileInputStream(raw).use { input ->
            val digest = MessageDigest.getInstance("SHA-256")
            val buffer = ByteArray(STREAM_BUFFER_BYTES)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                if (count > 0) digest.update(buffer, 0, count)
            }
            digest.digest().toLowerHex()
        }
    }

    private fun fsyncDirectory(directory: File) {
        val descriptor = Os.open(
            directory.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        try {
            check(Os.fstat(descriptor).st_mode and OsConstants.S_IFMT == OsConstants.S_IFDIR) {
                "plugin package fsync target is not a directory"
            }
            Os.fsync(descriptor)
        } finally {
            Os.close(descriptor)
        }
    }

    private fun writeFully(output: java.io.FileDescriptor, bytes: ByteArray, length: Int) {
        var written = 0
        while (written < length) {
            val count = Os.write(output, bytes, written, length - written)
            check(count > 0) { "plugin package output write made no progress" }
            written += count
        }
    }

    private fun mapEntryFailure(problem: Throwable): RunHostCode {
        if (problem is EntryPrepareFailure) {
            val prepareCause = problem.cause ?: problem
            val cause = (prepareCause as? InvocationTargetException)?.targetException ?: prepareCause
            return when (cause) {
                is PluginException -> when (cause.code()) {
                    PluginFailureCode.NOT_READY -> RunHostCode.NOT_READY
                    PluginFailureCode.IO_FAILURE -> RunHostCode.IO_FAILURE
                    PluginFailureCode.INVALID_INPUT,
                    PluginFailureCode.CANCELLED,
                    PluginFailureCode.CONTRACT_FAILURE,
                    -> RunHostCode.INTERNAL_CONTRACT_FAILURE
                }
                is UnsatisfiedLinkError -> RunHostCode.IO_FAILURE
                else -> RunHostCode.INTERNAL_CONTRACT_FAILURE
            }
        }
        val cause = (problem as? InvocationTargetException)?.targetException ?: problem
        return when (cause) {
            is PluginException -> when (cause.code()) {
                PluginFailureCode.INVALID_INPUT -> RunHostCode.INTERNAL_CONTRACT_FAILURE
                PluginFailureCode.NOT_READY -> RunHostCode.NOT_READY
                PluginFailureCode.IO_FAILURE -> RunHostCode.IO_FAILURE
                PluginFailureCode.CANCELLED,
                PluginFailureCode.CONTRACT_FAILURE,
                -> RunHostCode.INTERNAL_CONTRACT_FAILURE
            }
            is UnsatisfiedLinkError -> RunHostCode.IO_FAILURE
            is ClassNotFoundException,
            is NoSuchMethodException,
            is ClassCastException,
            is LinkageError,
            is IllegalArgumentException,
            -> RunHostCode.ARTIFACT_INVALID
            else -> RunHostCode.INTERNAL_CONTRACT_FAILURE
        }
    }

    private data class ExtractedPackage(
        val dexFiles: List<File>,
        val nativeDirectory: File,
    )

    private data class CopiedArchive(
        val expectedRoot: PluginContentSha256,
        val packageRoot: File,
        val archive: File,
    )

    private class EntryPrepareFailure(cause: Throwable) : RuntimeException(cause)

    private companion object {
        const val ARCHIVE_FILE_NAME = "archive.glplugin.zip"
        const val VERIFY_DIRECTORY = "verify"
        const val PACKAGE_ROOT_PREFIX_HEX = 16
        const val STREAM_BUFFER_BYTES = 16 * 1024
        const val READ_ONLY_FILE_MODE = 0b100000000
        const val SEARCH_ONLY_DIRECTORY_MODE = 0b101000000
        const val OWNER_DIRECTORY_MODE = 0b111000000
        const val ANDROID_ARM64_VARIANT = "android-arm64-v8a"
        const val ARM64_ABI = "arm64-v8a"
        const val AARCH64_MACHINE = "aarch64"
        const val DIAGNOSTIC_ENTRY_CONTRACT = "diagnostic-v1"
        const val DIAGNOSE_ENTRY_NAMESPACE_PREFIX = "com.ghostlock.plugin.diagnose."
        const val HOST_IMPLEMENTATION_PREFIX = "com.ghostlock.app."
    }
}

private fun PluginManifestComponent.toDescriptor(): BackendDescriptor = BackendDescriptor(
    key = backendKey,
    selectionPolicy = when (selectionPolicy) {
        PluginSelectionPolicy.SELECTABLE -> BackendSelectionPolicy.SELECTABLE
    },
    abortPolicy = when (abortPolicy) {
        PluginAbortPolicy.BEFORE_START_ONLY -> AbortPolicy.BEFORE_START_ONLY
    },
    executionDomain = when (executionDomains.toSet()) {
        setOf(PluginExecutionDomain.APP) -> ExecutionDomain.APP
        setOf(PluginExecutionDomain.SHIZUKU_SHELL) -> ExecutionDomain.SHIZUKU_SHELL
        setOf(PluginExecutionDomain.APP, PluginExecutionDomain.SHIZUKU_SHELL) ->
            ExecutionDomain.APP_OR_SHIZUKU
        else -> throw IllegalArgumentException("unsupported plugin execution domains")
    },
    runCapability = when (runCapability) {
        PluginRunCapability.DIAGNOSTIC_ONLY -> RunCapability.DIAGNOSTIC_ONLY
    },
)

private fun ParcelFileDescriptor.closeQuietly() {
    runCatching { close() }
}

private fun ByteArray.toLowerHex(): String = joinToString(separator = "") { byte ->
    (byte.toInt() and 0xff).toString(16).padStart(2, '0')
}
