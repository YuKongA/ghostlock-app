package com.ghostlock.app.ui.component.rebootlistpopup

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.component.CapabilityGate
import com.ghostlock.app.ui.component.dialog.rememberConfirmDialog

data class RebootListOption(
    @param:StringRes val labelRes: Int,
    val reason: String,
)

fun getRebootListOption(supportsUserspaceReboot: Boolean): List<RebootListOption> {
    return buildList {
        add(RebootListOption(R.string.reboot, ""))
        if (supportsUserspaceReboot) {
            add(RebootListOption(R.string.reboot_userspace, "userspace"))
        }
        add(RebootListOption(R.string.reboot_soft, "soft_reboot"))
        add(RebootListOption(R.string.reboot_recovery, "recovery"))
        add(RebootListOption(R.string.reboot_bootloader, "bootloader"))
        add(RebootListOption(R.string.reboot_download, "download"))
        add(RebootListOption(R.string.reboot_edl, "edl"))
    }
}

/** Wraps a caller-owned reboot action with optional confirmation for a plain reboot. */
@Composable
fun rememberRebootAction(
    confirmPlainReboot: Boolean,
    onReboot: (String) -> Unit,
): (String) -> Unit {
    val title = stringResource(R.string.reboot)
    val message = stringResource(R.string.jailbreak_reboot_warning)
    val currentOnReboot by rememberUpdatedState(onReboot)
    val confirmDialog = rememberConfirmDialog(onConfirm = { currentOnReboot("") })

    return remember(title, message, confirmDialog, confirmPlainReboot) {
        { reason ->
            if (confirmPlainReboot && reason.isEmpty()) {
                confirmDialog.showConfirm(title = title, content = message)
            } else {
                currentOnReboot(reason)
            }
        }
    }
}

@Composable
fun RebootListPopup(
    available: Boolean,
    supportsUserspaceReboot: Boolean,
    confirmPlainReboot: Boolean,
    onReboot: (String) -> Unit,
) {
    val options = remember(supportsUserspaceReboot) {
        getRebootListOption(supportsUserspaceReboot)
    }
    val guardedReboot = rememberRebootAction(confirmPlainReboot, onReboot)
    CapabilityGate(available) {
        when (LocalUiMode.current) {
            UiMode.Miuix -> RebootListPopupMiuix(options = options, onReboot = guardedReboot)
            UiMode.Material -> RebootListPopupMaterial(options = options, onReboot = guardedReboot)
        }
    }
}
