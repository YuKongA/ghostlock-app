package com.ghostlock.app.ui.backend

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.ui.icon.IconPair
import com.ghostlock.app.ui.icon.Icons
import com.ghostlock.app.ui.kit.ConfigItem
import com.ghostlock.app.ui.kit.LocalUiKit

/** Disabled placeholder for the future glbackend ZIP plus Lua and WebUI package boundary. */
internal object CustomBackendUi : BackendUi {
    override val categoryLabelRes: Int = R.string.backend_custom_category
    override val displayNameRes: Int = R.string.backend_custom_name
    override val icon: IconPair = Icons.Filled.Extension

    /** Renders only the disabled package status and never impersonates an implemented WebUI. */
    @Composable
    override fun Content(availability: BackendAvailability) {
        val statusTitle = stringResource(R.string.backend_runtime_status)
        val localizedDisabled = stringResource(R.string.backend_custom_disabled_summary)
        val summaries = availability.reasons.map { reason ->
            when (reason.code) {
                CUSTOM_DISABLED_REASON -> localizedDisabled
                else -> reason.message
            }
        }.ifEmpty { listOf(localizedDisabled) }
        LocalUiKit.current.ConfigItems(
            buildList {
                add(
                    ConfigItem.ReadOnly(
                        title = stringResource(R.string.backend_custom_package_title),
                        summary = stringResource(R.string.backend_custom_package_summary),
                    ),
                )
                summaries.forEach { summary ->
                    add(ConfigItem.ReadOnly(title = statusTitle, summary = summary))
                }
            },
        )
    }

    private const val CUSTOM_DISABLED_REASON: String = "lua-operation-catalog-empty"
}
