package com.ghostlock.app.data

import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import java.io.File

/** Finds root-manager candidates and orders them for ksud resolution. */
object ManagerScanner {
    const val PREF_KEY = "manager_package"

    /** Built-in KernelSU-family managers, priority order. */
    val BUILTIN = listOf(
        "me.weishu.kernelsu.pr",
        "me.weishu.kernelsu",
        "com.sukisu.ultra.pr",
        "com.sukisu.ultra",
        "com.sukisu.nova.pr",
        "com.sukisu.nova",
        "com.resukisu.resukisu",
        "org.bakasu.bakasu",
        "com.kowx712.supermanager",
    )

    private val NOVA_PACKAGES = setOf("com.sukisu.nova", "com.sukisu.nova.pr")
    private val KSUD_MARKER = "userspace/ksud/src".toByteArray()

    /**
     * SukiSU Nova renamed its bundled ksud to a randomized lib name. Resolve the
     * conventional name first, then identify Nova's binary by its ksud marker.
     */
    fun findKsud(packageName: String, dir: String?): File? {
        if (dir == null) return null
        val named = File(dir, "libksud.so")
        if (named.isFile) return named
        if (packageName !in NOVA_PACKAGES) return null
        return File(dir).listFiles { f ->
            f.isFile && f.name.startsWith("lib") && f.name.endsWith(".so") && f.length() <= 64L * 1024 * 1024
        }?.firstOrNull { it.containsKsudMarker() }
    }

    private fun File.containsKsudMarker(): Boolean = runCatching {
        val marker = KSUD_MARKER
        val buffer = ByteArray(64 * 1024)
        var tailSize = 0
        var found = false
        inputStream().use { input ->
            while (!found) {
                val read = input.read(buffer, tailSize, buffer.size - tailSize)
                if (read <= 0) break
                val size = tailSize + read
                if (containsMarker(buffer, size, marker)) {
                    found = true
                    break
                }
                tailSize = minOf(marker.size - 1, size)
                if (tailSize > 0) {
                    System.arraycopy(buffer, size - tailSize, buffer, 0, tailSize)
                }
            }
        }
        found
    }.getOrDefault(false)

    private fun containsMarker(bytes: ByteArray, size: Int, marker: ByteArray): Boolean {
        val lastStart = size - marker.size
        if (lastStart < 0) return false
        for (start in 0..lastStart) {
            var i = 0
            while (i < marker.size && bytes[start + i] == marker[i]) i++
            if (i == marker.size) return true
        }
        return false
    }

    data class Candidate(
        val packageName: String,
        val label: String,
        val hasKsud: Boolean,
        val builtin: Boolean,
    )

    /**
     * All non-system apps, compatible first. Relies on QUERY_ALL_PACKAGES;
     * if that permission is ever dropped, filter to BUILTIN here.
     */
    @Suppress("DEPRECATION")
    fun scan(pm: PackageManager, selfPackage: String): List<Candidate> =
        pm.getInstalledPackages(0)
            .asSequence()
            .mapNotNull { pi -> pi.applicationInfo?.let { pi to it } }
            .filter { (pi, _) -> pi.packageName != selfPackage }
            .filter { (_, ai) -> (ai.flags and ApplicationInfo.FLAG_SYSTEM) == 0 }
            .map { (pi, ai) ->
                val dir = ai.nativeLibraryDir
                Candidate(
                    packageName = pi.packageName,
                    label = ai.loadLabel(pm).toString(),
                    hasKsud = findKsud(pi.packageName, dir) != null,
                    builtin = pi.packageName in BUILTIN,
                )
            }
            .sortedWith(sortComparator())
            .toList()

    /** Pure sort, extracted so it is testable without a PackageManager. */
    fun sortComparator() = compareByDescending<Candidate> { it.builtin && it.hasKsud }
        .thenByDescending { it.hasKsud }
        .thenBy { it.label.lowercase() }

    /** Chosen package first, then the rest of the built-ins in priority order. */
    fun resolvePackages(chosen: String?): List<String> =
        if (chosen.isNullOrBlank()) BUILTIN
        else listOf(chosen) + BUILTIN.filter { it != chosen }
}
