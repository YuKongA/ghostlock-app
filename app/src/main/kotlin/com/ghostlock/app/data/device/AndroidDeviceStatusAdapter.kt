package com.ghostlock.app.data.device

import android.os.Build
import android.system.Os
import com.ghostlock.app.domain.device.DeviceStatus
import com.ghostlock.app.domain.device.DeviceStatusPort

/**
 * Android implementation of the device-status domain port.
 *
 * @param kernelVersionReader Linux release reader/parser.
 * @param selinuxStatusReader Ordered SELinux probe.
 * @param deviceNameReader Commercial model-name reader.
 */
class AndroidDeviceStatusAdapter(
    private val kernelVersionReader: KernelVersionReader = KernelVersionReader(),
    private val selinuxStatusReader: SELinuxStatusReader = SELinuxStatusReader(),
    private val deviceNameReader: AndroidDeviceNameReader = AndroidDeviceNameReader(),
) : DeviceStatusPort {
    /** @return One current kernel/SELinux snapshot with named unknown fallbacks. */
    override fun read(): DeviceStatus {
        val release = runCatching(kernelVersionReader::readRelease).getOrDefault("")
        return DeviceStatus(
            kernelVersion = kernelVersionReader.parse(release),
            kernelRelease = release,
            selinuxStatus = selinuxStatusReader.read(),
            deviceModel = deviceNameReader.read(),
            fingerprint = Build.FINGERPRINT,
            seccompStatus = runCatching {
                Os.prctl(PR_GET_SECCOMP, 0, 0, 0, 0)
            }.getOrDefault(-1),
        )
    }

    private companion object {
        const val PR_GET_SECCOMP = 21
    }
}
