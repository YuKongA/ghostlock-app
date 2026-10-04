package com.ghostlock.app.shizuku

/**
 * Dev-only escape hatch for the staged cve_2026_43284 entry: it relaxes the
 * carrier path prefix check so a one-shot file under /data/local/tmp can be
 * exercised. The native parser accepts it only together with
 * --run-cve-2026-43284, and it is appended last so the default (false) argv is
 * byte-for-byte identical to the pre-flag form.
 */
internal const val ALLOW_DEV_TARGET_FLAG = "--allow-dev-target"

/**
 * Pure argv assembly for the explicit staged cve_2026_43284 runner. Kept free
 * of Android APIs so the flag wiring can be unit-tested on the JVM.
 *
 * @param binaryPath absolute path of the libghostlock.so executable
 * @param modulePath kernel module (.ko) path handed to the native entry
 * @param targetPath target file the staged step writes through
 * @param stage one of plan/write/trigger/full
 * @param allowDevTarget when true, append --allow-dev-target at the end
 */
internal fun staged43284Args(
    binaryPath: String,
    modulePath: String,
    targetPath: String,
    stage: String,
    allowDevTarget: Boolean,
): List<String> = buildList {
    add(binaryPath)
    add("--run-cve-2026-43284")
    add(modulePath)
    add(targetPath)
    add("--stage=" + stage)
    if (allowDevTarget) add(ALLOW_DEV_TARGET_FLAG)
}
