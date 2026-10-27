package com.ghostlock.app.boot

import android.content.Context
import androidx.core.content.edit
import java.io.File

/** Duplicate-broadcast and single-unconfirmed-boot guard for autoroot. */
internal object AutorootBootGuard {
    private const val PreferencesName = "ghostlock_autoroot_guard"
    private const val AttemptCountKey = "attempt_count"
    private const val LastBootKey = "last_boot_id"
    private const val MaxUnconfirmedBoots = 1

    internal enum class Decision { RUN, DUPLICATE, TRIPPED }

    private fun preferences(context: Context) = context.createDeviceProtectedStorageContext()
        .getSharedPreferences(PreferencesName, Context.MODE_PRIVATE)

    @Synchronized
    fun beginBoot(context: Context): Decision {
        val bootId = currentBootId() ?: return Decision.TRIPPED
        val prefs = preferences(context)
        if (prefs.getString(LastBootKey, null) == bootId) return Decision.DUPLICATE
        val attempts = prefs.getInt(AttemptCountKey, 0)
        if (attempts >= MaxUnconfirmedBoots) return Decision.TRIPPED
        check(prefs.edit().putString(LastBootKey, bootId)
            .putInt(AttemptCountKey, attempts + 1).commit()) {
            "could not persist autoroot boot attempt"
        }
        return Decision.RUN
    }

    @Synchronized
    fun confirmBoot(context: Context) {
        check(preferences(context).edit().putInt(AttemptCountKey, 0).commit()) {
            "could not persist autoroot boot confirmation"
        }
    }

    @Synchronized
    fun reset(context: Context) {
        preferences(context).edit {
            remove(AttemptCountKey)
            remove(LastBootKey)
        }
    }

    private fun currentBootId(): String? = runCatching {
        File("/proc/sys/kernel/random/boot_id").readText().trim().takeIf { it.isNotEmpty() }
    }.getOrNull()
}
