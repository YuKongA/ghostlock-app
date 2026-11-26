package com.ghostlock.app.data

import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import java.io.File

/** Finds root-manager candidates and orders them for ksud resolution. */
object ManagerScanner {
    const val PREF_KEY = "manager_package"

    /** Built-in KernelSU-family and APatch managers, priority order. */
    val BUILTIN = listOf(
        "me.weishu.kernelsu.pr",
        "me.weishu.kernelsu",
        "com.sukisu.ultra.pr",
        "com.sukisu.ultra",
        "com.resukisu.resukisu",
        "org.bakasu.bakasu",
        "com.kowx712.supermanager",
        "me.bmax.apatch",
    )

    private val DAEMON_LIBS = listOf("libksud.so", "libapd.so")

    /** Returns the first supported manager daemon binary in [nativeLibDir], or null. */
    fun findDaemonBinary(nativeLibDir: String?): File? =
        nativeLibDir?.takeIf { it.isNotBlank() }?.let { dir ->
            DAEMON_LIBS.firstNotNullOfOrNull { File(dir, it).takeIf(File::isFile) }
        }

    /** First manager daemon binary (`libksud.so` / `libapd.so`) in [resolvePackages] order. */
    fun resolveDaemonBinary(pm: PackageManager, chosen: String?): File? =
        resolvePackages(chosen).firstNotNullOfOrNull { pkg ->
            runCatching { findDaemonBinary(pm.getApplicationInfo(pkg, 0).nativeLibraryDir) }.getOrNull()
        }

    /** True when [binary] is APatch's apd daemon (`libapd.so` / `apd`). */
    fun isApatch(binary: File?): Boolean =
        binary?.name == "libapd.so" || binary?.name == "apd"

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
                Candidate(
                    packageName = pi.packageName,
                    label = ai.loadLabel(pm).toString(),
                    hasKsud = findDaemonBinary(ai.nativeLibraryDir) != null,
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
