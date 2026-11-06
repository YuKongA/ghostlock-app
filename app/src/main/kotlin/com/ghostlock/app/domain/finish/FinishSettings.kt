package com.ghostlock.app.domain.finish

/** Stable identity of the built-in SELinux restoration action. */
const val RESTORE_SELINUX_FINISH_ID: String = "finish-restore-selinux"

/** Stable identity of the built-in soft reboot action. */
const val SOFT_REBOOT_FINISH_ID: String = "finish-soft-reboot"

/**
 * UI projection over the typed [FinishDefinition].
 *
 * @property restoreSelinux Whether the safety-critical SELinux restoration item is present.
 * @property softRebootAfterFinish Whether the final durable reboot-handoff item is present.
 */
data class FinishSettings(
    val restoreSelinux: Boolean,
    val softRebootAfterFinish: Boolean,
) {
    /**
     * Replaces only built-in UI-owned items while preserving future unrecognized typed actions.
     *
     * @param current Current canonical definition.
     * @return Definition containing the requested built-ins in deterministic order.
     */
    fun applyTo(current: FinishDefinition): FinishDefinition {
        val retained = current.items.filterNot {
            it.id == RESTORE_SELINUX_FINISH_ID || it.id == SOFT_REBOOT_FINISH_ID
        }
        val builtIns = buildList {
            if (restoreSelinux) {
                add(
                    FinishItemDefinition(
                        id = RESTORE_SELINUX_FINISH_ID,
                        kind = FinishKind.Command,
                        severity = FinishSeverity.SafetyCritical,
                        alwaysRun = true,
                        args = listOf("/system/bin/setenforce", "1"),
                    ),
                )
            }
            if (softRebootAfterFinish) {
                add(
                    FinishItemDefinition(
                        id = SOFT_REBOOT_FINISH_ID,
                        kind = FinishKind.Reboot,
                        severity = FinishSeverity.SafetyCritical,
                        alwaysRun = true,
                    ),
                )
            }
        }
        return FinishDefinition(retained + builtIns)
    }

    companion object {
        /** @return UI flags projected from exact built-in item identities. */
        fun from(definition: FinishDefinition): FinishSettings = FinishSettings(
            restoreSelinux = definition.items.any { it.id == RESTORE_SELINUX_FINISH_ID },
            softRebootAfterFinish = definition.items.any { it.id == SOFT_REBOOT_FINISH_ID },
        )
    }
}
