package com.ghostlock.app.data.plugin

import android.os.Build
import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.composition.backend.PluginObjectRef
import com.ghostlock.plugin.core.PluginContentSha256
import java.nio.file.Files
import java.nio.file.LinkOption

/** Main-process PFD lease for one exact immutable catalog object. */
internal class PluginObjectPfdLease(
    val pluginContentSha256: PluginContentSha256,
    internal val master: ParcelFileDescriptor,
) : AutoCloseable {
    private val lock = Any()
    private var closed = false

    fun duplicate(): ParcelFileDescriptor = synchronized(lock) {
        check(!closed) { "plugin object lease is closed" }
        ParcelFileDescriptor.dup(master.fileDescriptor)
    }

    override fun close() {
        val toClose = synchronized(lock) {
            if (closed) return
            closed = true
            master
        }
        toClose.close()
    }
}

/** Opens only an exact, reverified catalog object and never exposes its filesystem path. */
internal class PluginObjectSource(
    private val store: AndroidPluginCatalogStore,
) {
    fun open(ref: PluginObjectRef): PluginObjectPfdLease {
        val stored = store.reopenVerifiedObject(ref.pluginContentSha256)
        val manifest = stored.verified.manifest
        require(manifest.packageIdentity == ref.packageIdentity) {
            "plugin object package identity differs from cutoff"
        }
        require(manifest.variant == ref.variant) { "plugin object variant differs from cutoff" }
        require(manifest.versionCode == ref.versionCode) {
            "plugin object version differs from cutoff"
        }

        val raw = Os.open(
            stored.file.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        val master = try {
            ParcelFileDescriptor.dup(raw)
        } finally {
            Os.close(raw)
        }
        try {
            val stat = Os.fstat(master.fileDescriptor)
            val descriptorIsRegular = stat.st_mode and OsConstants.S_IFMT == OsConstants.S_IFREG
            val hostShimIsRegular = Build.FINGERPRINT == "robolectric" &&
                Files.isRegularFile(stored.file.toPath(), LinkOption.NOFOLLOW_LINKS)
            require(descriptorIsRegular || hostShimIsRegular) {
                "plugin object descriptor is not a regular file"
            }
            Os.lseek(master.fileDescriptor, 0L, OsConstants.SEEK_SET)
            return PluginObjectPfdLease(ref.pluginContentSha256, master)
        } catch (problem: Throwable) {
            runCatching { master.close() }
            throw problem
        }
    }
}
