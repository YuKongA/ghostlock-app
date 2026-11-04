package com.ghostlock.app.data.device

import android.system.Os
import com.ghostlock.app.domain.device.KernelVersion

/** Reads and parses the Android device's Linux release string. */
class KernelVersionReader {
    /**
     * Reads the raw uname release.
     *
     * @return Current kernel release string.
     */
    fun readRelease(): String = Os.uname().release

    /**
     * Parses the first `major.minor.sublevel` triplet from a release string.
     *
     * @param release Raw uname release.
     * @return Parsed version or KernelVersion.UNKNOWN when the triplet is absent or invalid.
     */
    fun parse(release: String): KernelVersion {
        val match = VERSION_PATTERN.find(release) ?: return KernelVersion.UNKNOWN
        val major = match.groupValues[1].toIntOrNull() ?: return KernelVersion.UNKNOWN
        val patchLevel = match.groupValues[2].toIntOrNull() ?: return KernelVersion.UNKNOWN
        val subLevel = match.groupValues[3].toIntOrNull() ?: return KernelVersion.UNKNOWN
        return KernelVersion(major, patchLevel, subLevel)
    }

    private companion object {
        val VERSION_PATTERN = "(\\d+)\\.(\\d+)\\.(\\d+)".toRegex()
    }
}
