package com.ghostlock.app.ui.backend

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationPort
import com.ghostlock.app.ui.component.backend.DefaultBackendConfigCard

/** Presentation boundary for rendering one selected backend's semantic configuration state. */
interface BackendConfigRenderer {
    /**
     * Renders the configuration card owned by one captured backend plugin.
     *
     * @param key Stable backend identity.
     */
    @Composable
    fun render(key: String)
}

/**
 * Registry-backed implementation confined to the application composition root.
 *
 * @param port Semantic configuration state and validated mutations supplied by AppContainer.
 */
class RegisteredBackendConfigRenderer(
    private val port: BackendConfigurationPort,
) : BackendConfigRenderer {
    /**
     * Emits one generic presentation card selected only by the neutral semantic subtype.
     *
     * Backend declarations remain renderer-free, and this renderer never imports or branches on a
     * concrete backend. Unknown and dynamic plugins use the same four semantic variants.
     */
    @Composable
    override fun render(key: String) {
        val states by port.states.collectAsState()
        LaunchedEffect(key) { port.refresh(key) }
        when (val state = states[key] ?: return) {
            is BackendConfiguration.Toggle -> BackendToggleConfigCard(
                state = state,
                onToggle = { enabled -> port.setBoolean(state.key, state.field, enabled) },
            )

            is BackendConfiguration.ChoiceDocument -> BackendChoiceDocumentConfigCard(
                state = state,
                onSelect = { value -> port.selectValue(state.key, state.field, value) },
                onImport = { uri -> port.importDocument(state.key, state.field, uri) },
            )

            is BackendConfiguration.Document -> BackendDocumentConfigCard(
                state = state,
                onImport = { uri -> port.importDocument(state.key, state.field, uri) },
            )

            is BackendConfiguration.Static -> DefaultBackendConfigCard(state.values)
        }
    }
}
