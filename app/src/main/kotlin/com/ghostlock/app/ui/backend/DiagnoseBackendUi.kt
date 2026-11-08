package com.ghostlock.app.ui.backend

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.ui.icon.IconPair
import com.ghostlock.app.ui.icon.Icons
import com.ghostlock.app.ui.kit.ConfigItem
import com.ghostlock.app.ui.kit.LocalUiKit

/** Named Android information surface for the built-in Diagnose backend. */
internal object DiagnoseBackendUi : BackendUi {
    override val categoryLabelRes: Int = R.string.backend_diagnose_category
    override val displayNameRes: Int = R.string.backend_diagnose_name
    override val icon: IconPair = Icons.Filled.BugReport

    /** Renders current diagnostic-chain facts without creating a configuration authority. */
    @Composable
    override fun Content(availability: BackendAvailability) {
        val statusTitle = stringResource(R.string.backend_runtime_status)
        val items = buildList {
            add(
                ConfigItem.ReadOnly(
                    title = stringResource(R.string.backend_diagnose_native_title),
                    summary = stringResource(R.string.backend_diagnose_native_summary),
                ),
            )
            add(
                ConfigItem.ReadOnly(
                    title = stringResource(R.string.backend_diagnose_handoff_title),
                    summary = stringResource(R.string.backend_diagnose_handoff_summary),
                ),
            )
            availability.reasons.forEach { reason ->
                add(ConfigItem.ReadOnly(title = statusTitle, summary = reason.message))
            }
        }
        LocalUiKit.current.ConfigItems(items)
    }
}
