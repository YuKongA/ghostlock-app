package com.ghostlock.app.ui.backend

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ghostlock.app.domain.backend.BackendChoiceKind
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.ui.kit.ConfigItem
import com.ghostlock.app.ui.kit.LocalUiKit

/** Generic choice plus document-import renderer driven only by neutral semantic state. */
@Composable
internal fun BackendChoiceDocumentConfigCard(
    state: BackendConfiguration.ChoiceDocument,
    onSelect: (String?) -> BackendConfigMutationResult,
    onImport: (String) -> BackendConfigMutationResult,
) {
    var mutationError by remember { mutableStateOf(false) }

    fun choose(index: Int) {
        val option = state.options.getOrNull(index) ?: run {
            mutationError = true
            return
        }
        if (option.kind == BackendChoiceKind.IMPORTED) {
            mutationError = false
            return
        }
        mutationError = onSelect(option.value) != BackendConfigMutationResult.APPLIED
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            mutationError = onImport(uri.toString()) != BackendConfigMutationResult.APPLIED
        }
    }

    LocalUiKit.current.ConfigItems(
        listOf(
            ConfigItem.Choice(
                title = state.title,
                summary = state.summary,
                items = state.options.map { option -> option.label },
                selectedIndex = state.selectedIndex,
                onSelect = ::choose,
            ),
            ConfigItem.Link(
                title = state.importTitle,
                summary = state.importSummary,
                onClick = { picker.launch(state.mimeTypes.toTypedArray()) },
            ),
        ),
    )
    if (state.invalid || mutationError) {
        Text(
            text = state.errorText,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            color = MaterialTheme.colorScheme.error,
        )
    }
}
