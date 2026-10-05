package com.ghostlock.app.data.plugin

/**
 * P1 plugin path vocabulary (frozen in contract-design §3.14.7.4).
 *
 * Two related but distinct paths exist and must never be confused:
 *
 *  - the App-side location under GHOSTLOCK_HOME:
 *    `countermeasures/<id>/<version>/<file>.so` ([appRelativePath]);
 *  - the wire `plugin.<id>.module_path`, which is relative to the
 *    countermeasures ROOT (the frozen section: "文档中的 module_path 是相对路径
 *    （相对上述根目录）"), i.e. `<id>/<version>/<file>.so` ([modulePath]). The
 *    native loader composes it with its whitelist root and re-checks realpath and
 *    the ".." rule, so a document can never escape the app-private directory.
 *
 * The root literal is a cross-language contract value: the probe header exports
 * `countermeasures_root` as this same relative NAME ("countermeasures", or "-"
 * when the probe has no home) and [checkProbeRoot] is what the agreement test
 * uses; there is no second hand-written root anywhere. The absolute path is
 * never compared, because the probe process and the App need not share
 * GHOSTLOCK_HOME.
 */
object PluginPaths {
    /** Root directory name relative to GHOSTLOCK_HOME. */
    const val COUNTERMEASURES_ROOT: String = "countermeasures"

    /** Registry file name inside the no-backup root. */
    const val MANIFEST_FILE: String = "plugins.tsv"

    /** Probes/plugins may not exceed this many bytes (UI bound, mirrored by native). */
    const val MAX_MODULE_BYTES: Long = 64L * 1024L * 1024L

    private val ID = Regex("[a-z0-9][a-z0-9._-]{0,63}")
    private val VERSION = Regex("[A-Za-z0-9][A-Za-z0-9._+-]{0,63}")
    private val FILE_NAME = Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,127}\\.so")

    /** A stable plugin id: lower-case, no path separator, bounded. */
    fun isValidId(id: String): Boolean = ID.matches(id)

    /** A version directory name: no path separator, bounded. */
    fun isValidVersion(version: String): Boolean = VERSION.matches(version)

    /** A module file name; must be a .so and contain no separator. */
    fun isValidFileName(name: String): Boolean = FILE_NAME.matches(name)

    /** App-side layout under GHOSTLOCK_HOME (where the import is stored). */
    fun appRelativePath(id: String, version: String, fileName: String): String {
        require(isValidId(id)) { "invalid plugin id: " + id }
        require(isValidVersion(version)) { "invalid plugin version: " + version }
        require(isValidFileName(fileName)) { "invalid plugin file name: " + fileName }
        return COUNTERMEASURES_ROOT + "/" + id + "/" + version + "/" + fileName
    }

    /** The wire `module_path`: relative to the countermeasures root. */
    fun modulePath(id: String, version: String, fileName: String): String {
        require(isValidId(id)) { "invalid plugin id: " + id }
        require(isValidVersion(version)) { "invalid plugin version: " + version }
        require(isValidFileName(fileName)) { "invalid plugin file name: " + fileName }
        return id + "/" + version + "/" + fileName
    }

    /** True when [path] is a safe module path relative to the countermeasures root. */
    fun isSafeModulePath(path: String): Boolean {
        if (path.isBlank() || path.startsWith("/")) return false
        val parts = path.split('/')
        if (parts.size != 3) return false
        if (parts.any { it.isEmpty() || it == "." || it == ".." }) return false
        return isValidId(parts[0]) && isValidVersion(parts[1]) && isValidFileName(parts[2])
    }

    /** Outcome of comparing the probe header with the Kotlin root constant. */
    enum class ProbeRootCheck {
        /** The header equals the contract root. */
        Match,

        /** The probe ran with no GHOSTLOCK_HOME ("-"): nothing to compare. */
        Unverifiable,

        /** The header names a different directory: contract violation. */
        Mismatch,
    }

    /**
     * The probe header's `countermeasures_root` must be EXACTLY this root.
     *
     * It is the environment-independent directory NAME, not an absolute path:
     * the probe process and the App do not necessarily share GHOSTLOCK_HOME, so
     * comparing absolute paths would compare two environments. "-" means the
     * probe had no home; that is legal but unverifiable, and the caller records
     * it rather than treating it as a match.
     */
    fun checkProbeRoot(probeCountermeasuresRoot: String?): ProbeRootCheck {
        val raw = probeCountermeasuresRoot?.trim()
        if (raw.isNullOrEmpty() || raw == "-") return ProbeRootCheck.Unverifiable
        return if (raw == COUNTERMEASURES_ROOT) ProbeRootCheck.Match else ProbeRootCheck.Mismatch
    }

    /** Convenience predicate: anything but [ProbeRootCheck.Mismatch]. */
    fun probeRootMatches(probeCountermeasuresRoot: String?): Boolean =
        checkProbeRoot(probeCountermeasuresRoot) != ProbeRootCheck.Mismatch
}
