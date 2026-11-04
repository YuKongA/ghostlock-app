package com.ghostlock.app.domain.finish

/** Reads current finish settings through the domain boundary. */
class ReadFinishSettingsUseCase(
    private val port: FinishSettingsPort,
) {
    /** @return Current finish option snapshot. */
    operator fun invoke(): FinishSettings = port.read()
}
