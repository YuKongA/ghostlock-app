package com.ghostlock.app.domain.device

/**
 * Parsed Linux kernel version used by domain and presentation decisions.
 *
 * @property major Kernel major version.
 * @property patchLevel Kernel minor/patch level.
 * @property subLevel Kernel sublevel.
 * @note Unknown releases are represented by `-1.-1.-1` rather than an Android exception.
 */
data class KernelVersion(
    val major: Int,
    val patchLevel: Int,
    val subLevel: Int,
) {
    /** @return Stable dotted representation used by the existing UI. */
    override fun toString(): String = "$major.$patchLevel.$subLevel"

    /**
     * Reports the existing GhostLock GKI eligibility heuristic.
     *
     * @return True for Linux 6+ and Linux 5.10+.
     */
    fun isGKI(): Boolean = major > 5 || major == 5 && patchLevel >= 10

    companion object {
        val UNKNOWN = KernelVersion(-1, -1, -1)
    }
}

/**
 * Storage- and Android-neutral device facts consumed by the home presentation model.
 *
 * @property kernelVersion Parsed kernel version.
 * @property kernelRelease Raw uname release string.
 * @property selinuxStatus Enforcing, Permissive, Disabled, or Unknown.
 * @property deviceModel Commercial model label resolved by the Android adapter.
 * @property fingerprint Android build fingerprint.
 * @property seccompStatus Current process seccomp mode, or `-1` when unavailable.
 */
data class DeviceStatus(
    val kernelVersion: KernelVersion,
    val kernelRelease: String,
    val selinuxStatus: String,
    val deviceModel: String,
    val fingerprint: String,
    val seccompStatus: Int,
)
