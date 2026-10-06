package com.ghostlock.app.data

import java.nio.charset.StandardCharsets

/**
 * S4 R4 cve_2026_43284 policy/handshake field universe.
 *
 * The three policy paths are wire type `str` (UTF-8, <= 256 bytes, absolute
 * path) and the three handshake knobs are wire type `uint` backed by the native
 * `uint32_t` members, so their validated domain is 0..0xFFFFFFFF. Presence is
 * carried by key occurrence: an absent field means "use the native default", so
 * an empty draft is valid and is *not* stored as a value.
 *
 * This object is the single Kotlin authority for the paths the advanced editor
 * materialises for a 43284 selection; the GLKv3 manifest owns their wire types.
 */
internal object Cve2026_43284Fields {
    /** Native section name (GLKv3 and HOCON agree). */
    const val Section = "backend.cve_2026_43284"

    /** Native `glkv3::kMaxStringBytes` / [Glkv3Encoder.MAX_STRING_BYTES]. */
    const val MaxTextBytes = 256

    /** Native `uint32_t` upper bound for the handshake tuning. */
    const val UInt32Max = 0xFFFF_FFFFL

    /**
     * HOCON refactor: kmi / lkm_path / carrier_path are native-side conventions
     * (derived from the release and $GHOSTLOCK_HOME, or the device's vendor
     * library) — the profile and the App no longer provide them, and the layout
     * rejects them on sight. Everything the editor still surfaces lives under
     * `execution.*`.
     */
    val StringPaths: List<String> = emptyList()

    /** Wire type `uint` backed by a native `uint32_t` (0..0xFFFFFFFF). */
    val UInt32Paths: List<String> = listOf(
        "$Section.execution.wait_timeout_ms",
        "$Section.execution.module_poll_attempts",
        "$Section.execution.module_poll_interval_ms",
    )

    /** Every 43284 field the advanced editor surfaces for a 43284 selection. */
    val EditablePaths: List<String> = StringPaths + UInt32Paths

    /** No editable 43284 string field is left: the conventions moved native-side
     * (see [StringPaths]). */
    val FilesystemPaths: List<String> = emptyList()

    fun isTextInvalid(path: String, text: String): Boolean {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return false
        if (trimmed.toByteArray(StandardCharsets.UTF_8).size > MaxTextBytes) return true
        return path in FilesystemPaths && !trimmed.startsWith("/")
    }
}

/**
 * Pure, path-aware field validation shared by the advanced and execution
 * editors (and directly unit-testable):
 *
 * - the 43284 text fields accept an empty value or bounded UTF-8 text; the two
 *   filesystem paths (carrier_path / lkm_path) must also be absolute;
 * - the 43284 handshake knobs additionally reject values outside the native
 *   `uint32_t` range;
 * - every other leaf keeps the historical "must parse as a Long" rule.
 */
internal fun isFieldInputInvalid(path: String, text: String): Boolean {
    if (path in Cve2026_43284Fields.StringPaths) {
        return Cve2026_43284Fields.isTextInvalid(path, text)
    }
    val value = text.trim().toLongOrNull() ?: return true
    if (path in Cve2026_43284Fields.UInt32Paths) {
        return value < 0L || value > Cve2026_43284Fields.UInt32Max
    }
    return false
}
