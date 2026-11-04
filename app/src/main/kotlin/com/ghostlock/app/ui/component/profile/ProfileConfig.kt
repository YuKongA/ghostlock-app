package com.ghostlock.app.ui.component.profile

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode

@Composable
fun AppProfileConfig(
    modifier: Modifier = Modifier,
    fixedName: Boolean,
    enabled: Boolean,
    profile: ProfileDraft,
    defaultUmountModules: Boolean,
    onProfileChange: (ProfileDraft) -> Unit,
) {
    when (LocalUiMode.current) {
        UiMode.Miuix -> AppProfileConfigMiuix(
            modifier = modifier,
            fixedName = fixedName,
            enabled = enabled,
            profile = profile,
            defaultUmountModules = defaultUmountModules,
            onProfileChange = onProfileChange
        )

        UiMode.Material -> AppProfileConfigMaterial(
            modifier = modifier,
            fixedName = fixedName,
            enabled = enabled,
            profile = profile,
            defaultUmountModules = defaultUmountModules,
            onProfileChange = onProfileChange
        )
    }
}

@Composable
fun RootProfileConfig(
    modifier: Modifier = Modifier,
    fixedName: Boolean,
    enabled: Boolean = true,
    profile: ProfileDraft,
    isPolicyValid: (String) -> Boolean = { true },
    onProfileChange: (ProfileDraft) -> Unit,
) {
    when (LocalUiMode.current) {
        UiMode.Miuix -> RootProfileConfigMiuix(
            modifier = modifier,
            fixedName = fixedName,
            enabled = enabled,
            profile = profile,
            isPolicyValid = isPolicyValid,
            onProfileChange = onProfileChange
        )

        UiMode.Material -> RootProfileConfigMaterial(
            modifier = modifier,
            enabled = enabled,
            profile = profile,
            isPolicyValid = isPolicyValid,
            onProfileChange = onProfileChange
        )
    }
}

@Composable
fun TemplateConfig(
    modifier: Modifier = Modifier,
    profile: ProfileDraft,
    templates: List<ProfileTemplateOption>,
    onTemplateSelected: (id: String) -> Unit,
    onViewTemplate: (id: String) -> Unit = {},
    onManageTemplate: () -> Unit = {},
) {
    when (LocalUiMode.current) {
        UiMode.Miuix -> TemplateConfigMiuix(
            modifier = modifier,
            profile = profile,
            templates = templates,
            onTemplateSelected = onTemplateSelected,
            onViewTemplate = onViewTemplate,
            onManageTemplate = onManageTemplate,
        )

        UiMode.Material -> TemplateConfigMaterial(
            profile = profile,
            templates = templates,
            onTemplateSelected = onTemplateSelected,
            onViewTemplate = onViewTemplate,
            onManageTemplate = onManageTemplate,
        )
    }
}
