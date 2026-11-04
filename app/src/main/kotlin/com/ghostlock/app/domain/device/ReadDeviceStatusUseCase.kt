package com.ghostlock.app.domain.device

/**
 * Reads current device status through the domain port.
 *
 * @param port Android implementation supplied by AppContainer.
 */
class ReadDeviceStatusUseCase(
    private val port: DeviceStatusPort,
) {
    /** @return One current device snapshot from the injected port. */
    operator fun invoke(): DeviceStatus = port.read()
}
