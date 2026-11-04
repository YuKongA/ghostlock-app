package com.ghostlock.app.data.finish

import android.content.Context
import com.ghostlock.app.core.finish.FinishConfig
import com.ghostlock.app.domain.finish.FinishSettings
import com.ghostlock.app.domain.finish.FinishSettingsPort

/**
 * Android persistence adapter over the existing authoritative FinishConfig state.
 *
 * @param context Application context used by the existing finish preference format.
 * @note Save copies only the two exposed fields and preserves payloads, manager, safe mode, and relay state.
 */
class FinishSettingsAdapter(
    context: Context,
) : FinishSettingsPort {
    private val applicationContext = context.applicationContext

    /** @return Current persisted finish options after loading the authoritative config once. */
    override fun read(): FinishSettings {
        FinishConfig.load(applicationContext)
        val state = FinishConfig.state
        return FinishSettings(
            restoreSelinux = state.restoreSelinux,
            softRebootAfterFinish = state.softRebootAfterFinish,
        )
    }

    /** Persists only non-payload settings while retaining every other authoritative field. */
    override fun save(settings: FinishSettings) {
        val current = FinishConfig.state
        FinishConfig.update(
            applicationContext,
            current.copy(
                restoreSelinux = settings.restoreSelinux,
                softRebootAfterFinish = settings.softRebootAfterFinish,
            ),
        )
    }
}
