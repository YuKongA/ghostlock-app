package com.ghostlock.app.domain.device

/** Android-independent boundary for reading current device facts. */
interface DeviceStatusPort {
    /**
     * Reads one current device snapshot.
     *
     * @return Kernel and SELinux facts with named unknown values on probe failure.
     */
    fun read(): DeviceStatus
}
