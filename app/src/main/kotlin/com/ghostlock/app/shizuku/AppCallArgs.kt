package com.ghostlock.app.shizuku

/**
 * R2b: pure app-call argv assembly.
 *
 * The native CLI carries transport, run control, safety and observability only;
 * the selection and every policy value come from the GLKv3 document written to
 * stdin. Production callers pass the defaults, so the argv stays byte-for-byte
 * the historical [binary, --ghostlock-app-call, --enable-status-record] form
 * plus the pre-existing optional flags.
 *
 * The dev entry passes [allowDevTarget]: it appends the dev-only carrier opt-in
 * LAST and nothing else changes, so a dev replay sends the same document
 * through the same pipeline as production. The flag never authorises data; it
 * only relaxes the carrier-path prefix check for a one-shot file.
 *
 * Kept free of Android APIs so the argv contract is unit-testable on the JVM.
 */
internal fun appCallArgs(
    binaryPath: String,
    forceAttack: Boolean = false,
    debugDir: String? = null,
    allowDevTarget: Boolean = false,
): List<String> = buildList {
    add(binaryPath)
    add("--ghostlock-app-call")
    add("--enable-status-record")
    if (forceAttack) add("--force-attack")
    if (!debugDir.isNullOrEmpty()) addAll(listOf("--dump-kernel-log", debugDir))
    if (allowDevTarget) add("--allow-dev-target")
}
