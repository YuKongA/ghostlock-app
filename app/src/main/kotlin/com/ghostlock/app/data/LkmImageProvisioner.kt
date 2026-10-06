package com.ghostlock.app.data

import android.content.Context
import java.io.File

/**
 * Runtime side of the LKM packaging (native lkm::kSupportedKmis /
 * lkm-kmi-manifest.tsv): the APK carries one module per KMI label under
 * `assets/lkm/<label>/ghostlock.ko`, and a run copies the one that matches the
 * device's release to `<filesDir>/helper.ko` - which is exactly native's
 * `$GHOSTLOCK_HOME/helper.ko` (GHOSTLOCK_HOME is the app files dir).
 *
 * Label rule (manifest column 1): `<android token>-<major>.<minor>`, e.g. the
 * release `5.15.189-android13-8-00016-g...` yields `android13` + `5.15` =>
 * label `android13-5.15`. The matcher never spells a label: it DERIVES it, so a
 * new KMI only needs a manifest row and an asset directory.
 *
 * No-match policy: an EXPLICIT failure (null + caller-visible log), never a
 * silent fallback to another KMI - loading the wrong module is worse than not
 * loading one, and native already fail-closes on a missing helper.ko.
 */
internal object LkmImageProvisioner {
    /** Directory prefix inside the APK assets. */
    const val ASSET_ROOT = "lkm"

    /** The module file name inside the APK assets (as packaged). */
    const val ASSET_NAME = "ghostlock.ko"

    /** The file native loads from $GHOSTLOCK_HOME (the asset is copied to this name). */
    const val HELPER_NAME = "helper.ko"

    private val KERNEL_VERSION = Regex("^(\\d+\\.\\d+)\\.")
    private val ANDROID_TOKEN = Regex("-android(\\d+)(?:-|$)")

    /** Pure: the KMI label for a `uname -r` string, or null when it has no KMI. */
    fun labelFor(release: String): String? {
        val kernel = KERNEL_VERSION.find(release)?.groupValues?.get(1) ?: return null
        val android = ANDROID_TOKEN.find(release)?.groupValues?.get(1) ?: return null
        return "android" + android + "-" + kernel
    }

    /** Asset path of a label's module. */
    fun assetPath(label: String): String = ASSET_ROOT + "/" + label + "/" + ASSET_NAME

    /** The destination native reads: <filesDir>/helper.ko. */
    fun targetFile(filesDir: File): File = File(filesDir, HELPER_NAME)

    /** Pure: a copy is needed only when the bytes differ (or nothing is there). */
    fun needsCopy(existing: ByteArray?, incoming: ByteArray): Boolean =
        existing == null || !existing.contentEquals(incoming)

    /**
     * Copies the module for [release] into place and returns its label, or null
     * when the release has no label or the APK does not carry that label.
     */
    fun provision(context: Context, release: String): String? {
        val label = labelFor(release) ?: return null
        val bytes = runCatching {
            context.assets.open(assetPath(label)).use { stream -> stream.readBytes() }
        }.getOrNull() ?: return null
        val target = targetFile(context.filesDir)
        val existing = if (target.isFile) target.readBytes() else null
        if (needsCopy(existing, bytes)) {
            target.parentFile?.mkdirs()
            target.writeBytes(bytes)
        }
        return label
    }
}