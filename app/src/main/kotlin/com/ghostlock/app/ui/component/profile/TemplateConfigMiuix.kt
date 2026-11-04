package com.ghostlock.app.ui.component.profile

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Create
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.OverlayDropdownPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * @author weishu
 * @date 2023/10/21.
 */
@Composable
fun TemplateConfigMiuix(
    modifier: Modifier = Modifier,
    profile: ProfileDraft,
    templates: List<ProfileTemplateOption>,
    onTemplateSelected: (id: String) -> Unit,
    onViewTemplate: (id: String) -> Unit = {},
    onManageTemplate: () -> Unit = {},
) {
    val noTemplates = templates.isEmpty()

    if (noTemplates) {
        ArrowPreference(
            modifier = modifier,
            title = stringResource(R.string.app_profile_template_create),
            startAction = {
                Icon(
                    Icons.Rounded.Create,
                    null,
                    modifier = Modifier.padding(end = 16.dp),
                    tint = MiuixTheme.colorScheme.onBackground
                )
            },
            onClick = onManageTemplate,
        )
    } else {
        val template = profile.rootTemplate ?: templates[0].id

        Column(modifier = modifier) {
            OverlayDropdownPreference(
                title = stringResource(R.string.profile_template),
                items = templates.map(ProfileTemplateOption::name),
                selectedIndex = templates.indexOfFirst { it.id == template }.takeIf { it >= 0 } ?: 0,
                onSelectedIndexChange = { index ->
                    if (index !in templates.indices) return@OverlayDropdownPreference
                    onTemplateSelected(templates[index].id)
                },
                maxHeight = 280.dp
            )
            ArrowPreference(
                title = stringResource(R.string.app_profile_template_view),
                onClick = { onViewTemplate(template) }
            )
        }
    }
}
