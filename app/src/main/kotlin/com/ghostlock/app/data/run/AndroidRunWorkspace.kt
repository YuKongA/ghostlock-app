package com.ghostlock.app.data.run

import android.content.Context
import android.os.Process
import android.system.Os
import com.ghostlock.app.backend.transport.RunHostIdentity
import java.io.File
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes

/**
 * Host-owned ordinary-file workspace for one one-shot RunHost instance.
 *
 * @property root Fresh private directory.
 * @property stagedInputs Directory containing copied request artifacts.
 * @property executables Directory containing host-published executable copies.
 * @property rootScript Directory containing the published script and result object.
 * @property persistentStateDirectory Stable backend-private directory outside the per-run deletion tree.
 */
data class AndroidRunWorkspace(
    val root: File,
    val stagedInputs: File,
    val executables: File,
    val rootScript: File,
    val persistentStateDirectory: File,
) {
    /**
     * Removes the workspace only after a terminal retention decision.
     *
     * @return True when no path remains; false preserves evidence for caller handling.
     */
    fun deleteAfterTerminal(): Boolean = runCatching {
        val entries = mutableListOf<Path>()
        Files.walkFileTree(root.toPath(), object : SimpleFileVisitor<Path>() {
            override fun preVisitDirectory(
                directory: Path,
                attributes: BasicFileAttributes,
            ): FileVisitResult {
                check(!Files.isSymbolicLink(directory)) { "workspace contains a linked directory" }
                entries.add(directory)
                return FileVisitResult.CONTINUE
            }

            override fun visitFile(file: Path, attributes: BasicFileAttributes): FileVisitResult {
                check(!attributes.isSymbolicLink) { "workspace contains a symbolic link" }
                entries.add(file)
                return FileVisitResult.CONTINUE
            }
        })
        entries.asReversed().forEach { entry ->
            check(Files.deleteIfExists(entry)) { "cannot delete run workspace entry: " + entry.fileName }
        }
        !root.exists()
    }.getOrDefault(false)

    companion object {
        /**
         * Creates a fresh workspace below the app process private directory.
         *
         * @param context Host process context.
         * @param identity Compound run identity.
         * @return Fresh workspace with mode 0700 directories.
         */
        fun createApp(
            context: Context,
            identity: RunHostIdentity,
            backendKey: String,
        ): AndroidRunWorkspace = createUnder(
            parent = File(context.noBackupFilesDir, "runhost"),
            identity = identity,
            backendKey = backendKey,
        )

        /**
         * Creates a shell-owned workspace below the platform shell staging directory.
         *
         * @param packageName Application package namespace used only as a fixed directory component.
         * @param identity Compound run identity.
         * @return Fresh shell-owned workspace with mode 0700 directories.
         */
        fun createShizukuShell(
            packageName: String,
            identity: RunHostIdentity,
            backendKey: String,
        ): AndroidRunWorkspace {
            require(packageName.matches(PACKAGE_PATTERN)) { "invalid package workspace namespace" }
            return createUnder(
                parent = File(SHELL_WORKSPACE_PARENT, packageName + "-runhost"),
                identity = identity,
                backendKey = backendKey,
            )
        }

        /** Creates one fresh identity directory beneath an already selected physical host root. */
        private fun createUnder(
            parent: File,
            identity: RunHostIdentity,
            backendKey: String,
        ): AndroidRunWorkspace {
            require(backendKey.matches(BACKEND_KEY_PATTERN)) { "invalid backend state namespace" }
            ensurePrivateDirectory(parent)
            val stateRoot = File(parent, "backend-state")
            ensurePrivateDirectory(stateRoot)
            val persistentState = File(stateRoot, backendKey)
            ensurePrivateDirectory(persistentState)
            val root = File(parent, identity.runId + "-" + identity.generation)
            check(root.mkdir()) { "run workspace already exists or cannot be created" }
            try {
                Os.chmod(root.absolutePath, 0b111000000)
                val staged = File(root, "input")
                val executables = File(root, "executable")
                val script = File(root, "root-script")
                check(staged.mkdir() && executables.mkdir() && script.mkdir()) {
                    "cannot create run workspace children"
                }
                Os.chmod(staged.absolutePath, 0b111000000)
                Os.chmod(executables.absolutePath, 0b111000000)
                Os.chmod(script.absolutePath, 0b111000000)
                return AndroidRunWorkspace(
                    root = root,
                    stagedInputs = staged,
                    executables = executables,
                    rootScript = script,
                    persistentStateDirectory = persistentState,
                )
            } catch (problem: Throwable) {
                runCatching {
                    root.walkBottomUp().forEach { entry ->
                        if (!Files.isSymbolicLink(entry.toPath())) entry.delete()
                    }
                }
                throw problem
            }
        }

        /** Ensures a private ordinary directory without accepting a symlink. */
        private fun ensurePrivateDirectory(directory: File) {
            if (!directory.exists()) check(directory.mkdir()) { "cannot create RunHost workspace root" }
            check(Files.isDirectory(directory.toPath(), LinkOption.NOFOLLOW_LINKS)) {
                "RunHost workspace root is not a directory"
            }
            check(!Files.isSymbolicLink(directory.toPath())) { "RunHost workspace root is a symlink" }
            check(Os.stat(directory.absolutePath).st_uid == Process.myUid()) {
                "RunHost workspace root is not owned by the host process"
            }
            Os.chmod(directory.absolutePath, 0b111000000)
        }

        private val PACKAGE_PATTERN: Regex = Regex("[A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)+")
        private val BACKEND_KEY_PATTERN: Regex = Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
        private const val SHELL_WORKSPACE_PARENT: String = "/data/local/tmp"
    }
}
