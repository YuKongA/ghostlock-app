package com.ghostlock.app.ui.component.profile

import com.ghostlock.app.ui.icon.Icons as ProjectIcons

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem
import com.ghostlock.app.ui.component.profile.dialogs.SingleSelectDialog

@Composable
fun TemplateConfigMaterial(
    profile: ProfileDraft,
    templates: List<ProfileTemplateOption>,
    onTemplateSelected: (id: String) -> Unit,
    onViewTemplate: (id: String) -> Unit = {},
    onManageTemplate: () -> Unit = {},
) {
    val showDialog = remember { mutableStateOf(false) }
    val template = profile.rootTemplate ?: ""
    val noTemplates = templates.isEmpty()

    val selectedTemplate = remember(template, templates) {
        templates.find { it.id == template } ?: templates.firstOrNull()
    }

    if (showDialog.value && !noTemplates) {
        SingleSelectDialog(
            title = stringResource(R.string.profile_template),
            items = templates,
            selectedItem = selectedTemplate ?: templates.first(),
            itemTitle = { it.name },
            onConfirm = { selected ->
                onTemplateSelected(selected.id)
                showDialog.value = false
            },
            onDismiss = { showDialog.value = false }
        )
    }

    val selectedTemplateName = selectedTemplate?.name ?: "None"

    SegmentedColumn(
        modifier = Modifier.padding(horizontal = 16.dp),
        content = buildList {
            add {
                SegmentedListItem(
                    headlineContent = { Text(stringResource(R.string.profile_template)) },
                    supportingContent = { Text(selectedTemplateName) },
                    trailingContent = {
                        if (noTemplates) {
                            IconButton(onClick = onManageTemplate) {
                                Icon(ProjectIcons.Filled.Create.material, contentDescription = null)
                            }
                        }
                    },
                    onClick = {
                        if (!noTemplates) {
                            showDialog.value = true
                        }
                    }
                )
            }
            if (template.isNotEmpty()) add {
                SegmentedListItem(
                    headlineContent = { Text(stringResource(R.string.app_profile_template_view)) },
                    trailingContent = {
                        Icon(ProjectIcons.AutoMirrored.Filled.ReadMore.material, contentDescription = null)
                    },
                    onClick = { onViewTemplate(template) }
                )
            }
        }
    )
}
