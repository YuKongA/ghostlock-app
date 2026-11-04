package com.ghostlock.app.ui.backend

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.ui.kit.ConfigItem
import com.ghostlock.app.ui.kit.LocalUiKit

/** Generic imported-document renderer with backend-projected facts and bounded details. */
@Composable
fun BackendDocumentConfigCard(
    state: BackendConfiguration.Document,
    onImport: (String) -> BackendConfigMutationResult,
) {
    val picker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            onImport(uri.toString())
        }
    }

    LocalUiKit.current.ConfigItems(
        buildList {
            add(
                ConfigItem.Link(
                    title = state.importTitle,
                    summary = state.documentSummary,
                    onClick = { picker.launch(state.mimeTypes.toTypedArray()) },
                ),
            )
            state.facts.forEach { fact ->
                add(ConfigItem.ReadOnly(title = fact.label, summary = fact.value))
            }
        },
    )
    state.details?.let { text ->
        Column(
            Modifier.fillMaxWidth().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(text = text, style = MaterialTheme.typography.bodySmall)
        }
    }
}
