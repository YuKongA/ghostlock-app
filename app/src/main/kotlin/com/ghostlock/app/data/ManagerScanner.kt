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
        "com.resukisu.resukisu",
        "org.bakasu.bakasu",
        "com.kowx712.supermanager",
    )

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
                    hasKsud = dir != null && File(dir, "libksud.so").isFile,
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
