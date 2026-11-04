package com.ghostlock.app.ui.component.rebootlistpopup

import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.ListPopupDefaults
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.ListPopupColumn
import top.yukonga.miuix.kmp.basic.PopupPositionProvider
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Close2
import top.yukonga.miuix.kmp.overlay.OverlayListPopup
import top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme

@Composable
fun RebootListPopupMiuix(
    options: List<RebootListOption>,
    onReboot: (String) -> Unit,
    modifier: Modifier = Modifier,
    alignment: PopupPositionProvider.Align = PopupPositionProvider.Align.TopEnd
) {
    val showTopPopup = remember { mutableStateOf(false) }
    IconButton(
        modifier = modifier,
        onClick = { showTopPopup.value = true },
        holdDownState = showTopPopup.value
    ) {
        Icon(
            imageVector = MiuixIcons.Close2,
            contentDescription = stringResource(id = R.string.reboot),
            tint = colorScheme.onBackground
        )
    }
    OverlayListPopup(
        show = showTopPopup.value,
        popupPositionProvider = ListPopupDefaults.MenuPositionProvider,
        alignment = alignment,
        onDismissRequest = {
            showTopPopup.value = false
        },
        content = {
            ListPopupColumn {
                options.forEachIndexed { idx, option ->
                    RebootDropdownItem(
                        option = option,
                        showTopPopup = showTopPopup,
                        optionSize = options.size,
                        index = idx,
                        onReboot = onReboot
                    )
                }
            }
        }
    )
}

@Composable
fun RebootDropdownItem(
    option: RebootListOption,
    showTopPopup: MutableState<Boolean>,
    optionSize: Int,
    index: Int,
    onReboot: (String) -> Unit,
) {
    com.ghostlock.app.ui.component.miuix.DropdownItem(
        text = stringResource(option.labelRes),
        optionSize = optionSize,
        onSelectedIndexChange = {
            showTopPopup.value = false
            onReboot(option.reason)
        },
        index = index
    )
}
