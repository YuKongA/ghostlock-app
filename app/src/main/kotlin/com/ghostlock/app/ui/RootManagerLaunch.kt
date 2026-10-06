package com.ghostlock.app.ui

import androidx.annotation.StringRes
import com.ghostlock.app.R
import com.ghostlock.app.data.component.FrontendKind

/**
 * The root manager a run leaves behind, and the App package whose UI presents it.
 *
 * The PACKAGE NAME is not an App invention. Native owns it in
 * `lkm_image.cpp:343-348` (`default_root_package`), which delegates to the owner
 * schema's `root_package_convention`: "KernelSU -> me.weishu.kernelsu, every
 * other kind -> empty" — an unknown manager is NEVER guessed. This enum is the
 * ONE App-side mirror of that table; root-manager P1 (folkpatch/custom) adds a
 * row here plus the matching row in the schema, and nothing else changes.
 */
enum class RootManager(
    val token: String,
    val packageName: String,
    @StringRes val labelRes: Int,
) {
    /**
     * KernelSU: the ONLY package native names for a root program
     * (`schema.hpp:69-70` kCve2026_43284RootPackageKernelSU, returned by
     * `lkm_image.cpp:346` default_root_package for RootProgramKind::KernelSU).
     */
    KernelSU("kernelsu", "me.weishu.kernelsu", R.string.root_manager_kernelsu),

    /**
     * The "pr" build of the same manager. Evidence: the ksud discovery glob in
     * `root_script.cpp:40` (a `/data/app` path containing `me.weishu.kernelsu.pr`
     * then `lib/arm64/libksud.so`) and the App's own manifest query for it.
     */
    KernelSuPr("kernelsu_pr", "me.weishu.kernelsu.pr", R.string.root_manager_kernelsu_pr),

    /** Evidence: the ksud discovery glob at `root_script.cpp:46`. */
    ReSukiSU("resukisu", "com.resukisu.resukisu", R.string.root_manager_resukisu),

    /** Evidence: the ksud discovery glob at `root_script.cpp:49`. */
    KowSU("kowxu", "com.kowx712.supermanager", R.string.root_manager_kowxu),
    ;

    companion object {
        /**
         * The manager a run with [terminal] leaves behind. Both wired terminals
         * (ADR-0006: `root_child` and `umh_forward`) hand the device to the
         * KernelSU module, so this is total today; anything unknown maps to null
         * rather than to a guessed package.
         */
        fun of(terminal: FrontendKind?): RootManager? = when (terminal) {
            FrontendKind.RootChild, FrontendKind.UmhForward -> KernelSU
            null -> null
        }

        /** EXACT token lookup, for a selection stored in the draft. */
        fun resolve(token: String?): RootManager? =
            token?.let { value -> entries.firstOrNull { it.token == value } }
    }
}

/** What the App does with the root manager UI once a run has finished. */
sealed interface RootManagerAction {
    /** Open [packageName]'s launcher activity (the caller adds NEW_TASK). */
    data class Launch(val packageName: String) : RootManagerAction

    /** A manager applies but has no launchable UI: the page must SAY so. */
    data object Hint : RootManagerAction

    /** Nothing to do: the run did not succeed, produced no root state, or no
     * manager applies. */
    data object Skip : RootManagerAction
}

/**
 * The whole "open it / say so / do nothing" rule, as a PURE function so it is
 * unit-tested without Android:
 *
 *  - [succeeded]: the App's EXISTING success criterion — the exploit process
 *    exited 0 ([GhostlockViewModel.runExploit]'s `code == 0`). No second
 *    success notion is invented here;
 *  - [rootProduced]: this run actually handed the device to the root manager.
 *    `force_attack_test` ignores an already loaded KernelSU and DISCARDS the
 *    root child, so it succeeds while leaving no root state to look at —
 *    opening the manager UI there would be misleading, hence Skip;
 *  - [packageName]: the manager of THIS run ([RootManager.packageName]); null =
 *    no manager applies to this run;
 *  - [launchable]: `packageManager.getLaunchIntentForPackage(packageName) != null`
 *    — the caller performs that query, because only it has a Context.
 */
fun rootManagerAction(
    succeeded: Boolean,
    rootProduced: Boolean,
    packageName: String?,
    launchable: Boolean,
): RootManagerAction = when {
    !succeeded -> RootManagerAction.Skip
    !rootProduced -> RootManagerAction.Skip
    packageName == null -> RootManagerAction.Skip
    launchable -> RootManagerAction.Launch(packageName)
    else -> RootManagerAction.Hint
}
