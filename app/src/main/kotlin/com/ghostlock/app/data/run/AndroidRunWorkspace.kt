package com.ghostlock.app.data.run

import android.content.Context
import android.os.Process
import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.core.runhost.RunHostIdentity
import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path

/**
 * Host-owned ordinary-file workspace for one one-shot RunHost instance.
 *
 * @property root Fresh private directory.
 * @property stagedInputs Directory containing copied request artifacts.
 * @property executables Directory containing host-published executable copies.
 * @property rootScript Directory containing the published script and result object.
 * @property pluginPackages Directory containing verified per-package code/native trees.
 * @property persistentStateDirectory Stable backend-private directory outside the per-run deletion tree.
 */
data class AndroidRunWorkspace(
    val root: File,
    val stagedInputs: File,
    val executables: File,
    val rootScript: File,
    val pluginPackages: File,
    val persistentStateDirectory: File,
    private val rootDevice: Long,
    private val rootInode: Long,
    private val rootUid: Int,
) {
    /**
     * Removes the workspace only after a terminal retention decision.
     *
     * @return True when no path remains; false preserves evidence for caller handling.
     */
    fun deleteAfterTerminal(): Boolean = runCatching {
        val rootPath = root.toPath()
        val initialRoot = verifiedEntry(rootPath, expectDirectory = true)
        check(initialRoot.device == rootDevice && initialRoot.inode == rootInode && initialRoot.uid == rootUid) {
            "run workspace root identity changed"
        }
        val entries = mutableListOf<WorkspaceEntryIdentity>()
        collectForDeletion(rootPath, entries)
        entries.asReversed().forEach { entry ->
            val current = verifiedEntry(entry.path, expectDirectory = entry.directory)
            check(current == entry) { "run workspace entry identity changed: " + entry.path.fileName }
            Files.delete(entry.path)
        }
        !root.exists()
    }.getOrDefault(false)

    /** Captures one no-follow tree after making only traversed directories owner-readable. */
    private fun collectForDeletion(
        path: Path,
        entries: MutableList<WorkspaceEntryIdentity>,
    ) {
        val directory = verifiedEntry(path, expectDirectory = true)
        entries += directory
        Os.chmod(path.toString(), OWNER_DIRECTORY_MODE)
        Files.newDirectoryStream(path).use { children ->
            children.toList().sortedBy { child -> child.fileName.toString() }.forEach { child ->
                val stat = Os.lstat(child.toString())
                check(stat.st_uid == rootUid && stat.st_dev == rootDevice) {
                    "run workspace entry owner or device changed: " + child.fileName
                }
                val type = stat.st_mode and OsConstants.S_IFMT
                when (type) {
                    OsConstants.S_IFDIR -> collectForDeletion(child, entries)
                    OsConstants.S_IFREG -> entries += WorkspaceEntryIdentity(
                        path = child,
                        device = stat.st_dev,
                        inode = stat.st_ino,
                        uid = stat.st_uid,
                        directory = false,
                    )
                    else -> error("run workspace contains a non-ordinary entry: " + child.fileName)
                }
            }
        }
    }

    /** Revalidates one exact ordinary entry without following a final symlink. */
    private fun verifiedEntry(path: Path, expectDirectory: Boolean): WorkspaceEntryIdentity {
        val stat = Os.lstat(path.toString())
        val expectedType = if (expectDirectory) OsConstants.S_IFDIR else OsConstants.S_IFREG
        check(stat.st_mode and OsConstants.S_IFMT == expectedType) {
            "run workspace entry type changed: " + path.fileName
        }
        check(stat.st_uid == rootUid && stat.st_dev == rootDevice) {
            "run workspace entry owner or device changed: " + path.fileName
        }
        return WorkspaceEntryIdentity(
            path = path,
            device = stat.st_dev,
            inode = stat.st_ino,
            uid = stat.st_uid,
            directory = expectDirectory,
        )
    }

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
                val plugins = File(root, "plugin")
                check(staged.mkdir() && executables.mkdir() && script.mkdir() && plugins.mkdir()) {
                    "cannot create run workspace children"
                }
                Os.chmod(staged.absolutePath, 0b111000000)
                Os.chmod(executables.absolutePath, 0b111000000)
                Os.chmod(script.absolutePath, 0b111000000)
                Os.chmod(plugins.absolutePath, OWNER_DIRECTORY_MODE)
                val rootStat = Os.lstat(root.absolutePath)
                check(rootStat.st_mode and OsConstants.S_IFMT == OsConstants.S_IFDIR) {
                    "run workspace root is not an ordinary directory"
                }
                check(rootStat.st_uid == Process.myUid()) { "run workspace root owner mismatch" }
                return AndroidRunWorkspace(
                    root = root,
                    stagedInputs = staged,
                    executables = executables,
                    rootScript = script,
                    pluginPackages = plugins,
                    persistentStateDirectory = persistentState,
                    rootDevice = rootStat.st_dev,
                    rootInode = rootStat.st_ino,
                    rootUid = rootStat.st_uid,
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
        private const val OWNER_DIRECTORY_MODE: Int = 0b111000000
    }
}

/** No-follow identity captured before reverse deletion. */
private data class WorkspaceEntryIdentity(
    val path: Path,
    val device: Long,
    val inode: Long,
    val uid: Int,
    val directory: Boolean,
)
