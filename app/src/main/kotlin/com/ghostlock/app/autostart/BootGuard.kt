package com.ghostlock.app.autostart

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import java.io.File

/**
 * Boot-loop guard for the boot auto-start.
 *
 * Every boot is identified by `/proc/sys/kernel/random/boot_id`. When the
 * [BootAutoStartReceiver] launches a headless run it counts the boot; a run
 * that completes normally marks the boot as good once the device has stayed
 * alive for [AutoStartService.ConfirmationDelayMs] after the run finished.
 * Three reboots in a row without a good-boot mark (the likely signature of a
 * panic or bootloop caused by the exploit) disable auto-start and leave a
 * one-shot notice for the UI.
 *
 * The transitions are pure functions in [BootGuardTransitions]; [BootGuard]
 * only persists [BootGuardState] into the shared `ghostlock_prefs`.
 */
data class BootGuardState(
    val enabled: Boolean = false,
    /** Counted boots since the last good-boot mark. */
    val attemptCount: Int = 0,
    /** Boot id of the last counted attempt; dedups repeated boot broadcasts. */
    val attemptBootId: String = "",
    /** Boot id explicitly marked as good. */
    val goodBootId: String = "",
    /** The guard tripped and the UI has not warned the user yet. */
    val noticePending: Boolean = false,
)

enum class BootDecision {
    /** Auto-start is off (or the boot id is unreadable): run nothing. */
    DISABLED,

    /** This boot has already been counted or confirmed: ignore a repeat broadcast. */
    ALREADY_HANDLED,

    /** This boot was counted as an attempt: launch the headless run. */
    RUN,

    /** MaxUnconfirmedBoots reboots passed unconfirmed: auto-start got disabled. */
    GUARD_TRIPPED,
}

/** Pure boot-guard state machine; no Android types so it is host-testable. */
object BootGuardTransitions {
    /** Consecutive unconfirmed reboots tolerated before auto-start is disabled. */
    const val MaxUnconfirmedBoots = 3

    fun nextOnBoot(state: BootGuardState, bootId: String): Pair<BootGuardState, BootDecision> = when {
        !state.enabled -> state to BootDecision.DISABLED
        state.goodBootId == bootId || state.attemptBootId == bootId -> state to BootDecision.ALREADY_HANDLED
        state.attemptCount >= MaxUnconfirmedBoots -> state.copy(
            enabled = false,
            attemptCount = 0,
            noticePending = true,
        ) to BootDecision.GUARD_TRIPPED
        else -> state.copy(
            attemptCount = state.attemptCount + 1,
            attemptBootId = bootId,
        ) to BootDecision.RUN
    }

    /** Null when the state needs no change (boot has no counted attempt or is already good). */
    fun nextOnConfirm(state: BootGuardState, bootId: String): BootGuardState? =
        if (bootId.isNotEmpty() && state.attemptBootId == bootId && state.goodBootId != bootId) {
            state.copy(attemptCount = 0, goodBootId = bootId)
        } else {
            null
        }

    fun nextOnEnable(state: BootGuardState, bootId: String): BootGuardState = state.copy(
        enabled = true,
        attemptCount = 0,
        attemptBootId = bootId,
        goodBootId = bootId,
        noticePending = false,
    )

    fun nextOnDisable(state: BootGuardState): BootGuardState =
        state.copy(enabled = false, attemptCount = 0)
}

/** SharedPreferences store + entry points for [BootGuardTransitions]. */
class BootGuard(
    private val preferences: SharedPreferences,
    private val bootId: () -> String,
) {
    fun read(): BootGuardState = with(preferences) {
        BootGuardState(
            enabled = getBoolean(PrefEnabled, false),
            attemptCount = getInt(PrefAttemptCount, 0),
            attemptBootId = getString(PrefAttemptBoot, "").orEmpty(),
            goodBootId = getString(PrefGoodBoot, "").orEmpty(),
            noticePending = getBoolean(PrefNotice, false),
        )
    }

    /** The receiver's single entry point: count this boot and decide what to do. */
    fun onBoot(): BootDecision {
        val current = bootId()
        if (current.isEmpty()) return BootDecision.DISABLED
        val (next, decision) = BootGuardTransitions.nextOnBoot(read(), current)
        write(next)
        return decision
    }

    /** Writes the good-boot mark when the current boot has a counted attempt. */
    fun confirmCurrentBoot() {
        val next = BootGuardTransitions.nextOnConfirm(read(), bootId()) ?: return
        write(next)
    }

    /** True when a completed run should arm the good-boot timer for this boot. */
    fun needsConfirmation(): Boolean =
        BootGuardTransitions.nextOnConfirm(read(), bootId()) != null

    fun enable() = write(BootGuardTransitions.nextOnEnable(read(), bootId()))

    fun disable() = write(BootGuardTransitions.nextOnDisable(read()))

    /** True exactly once after the guard tripped, so the UI can warn the user. */
    fun consumeNotice(): Boolean {
        val state = read()
        if (!state.noticePending) return false
        write(state.copy(noticePending = false))
        return true
    }

    private fun write(state: BootGuardState) = preferences.edit {
        putBoolean(PrefEnabled, state.enabled)
        putInt(PrefAttemptCount, state.attemptCount)
        putString(PrefAttemptBoot, state.attemptBootId)
        putString(PrefGoodBoot, state.goodBootId)
        putBoolean(PrefNotice, state.noticePending)
    }

    companion object {
        /** Guard state shares `ghostlock_prefs` with the app settings. */
        private const val PreferencesName = "ghostlock_prefs"
        private const val PrefEnabled = "autostart_enabled"
        private const val PrefAttemptCount = "autostart_attempt_count"
        private const val PrefAttemptBoot = "autostart_attempt_boot"
        private const val PrefGoodBoot = "autostart_good_boot"
        private const val PrefNotice = "autostart_notice"

        fun of(context: Context): BootGuard = BootGuard(
            context.applicationContext.getSharedPreferences(PreferencesName, Context.MODE_PRIVATE),
            ::currentBootId,
        )

        /** Kernel boot identifier, unique per boot; empty when unreadable. */
        fun currentBootId(): String = runCatching {
            File("/proc/sys/kernel/random/boot_id").readText().trim()
        }.getOrDefault("")
    }
}
