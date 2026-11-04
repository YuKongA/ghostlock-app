package com.ghostlock.app.domain.finish

/**
 * Renderer- and storage-neutral finish options that do not alter payload selection.
 *
 * @property restoreSelinux Whether finish restores the captured SELinux state.
 * @property softRebootAfterFinish Whether finish requests a soft reboot after handoff.
 */
data class FinishSettings(
    val restoreSelinux: Boolean,
    val softRebootAfterFinish: Boolean,
)
