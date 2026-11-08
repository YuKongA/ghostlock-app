package com.ghostlock.app.composition.backend

import com.ghostlock.app.backend.declarations.BackendRuntimeEntry
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.ui.backend.BackendUiEntry

/**
 * Immutable registry implementation over one outer definition list.
 *
 * @param definitions Built-in definitions in stable UI order.
 */
internal class BackendDefinitionRegistry(definitions: List<BackendDefinition>) {
    private val definitions: List<BackendDefinition> = definitions.toList().also { entries ->
        require(entries.map { definition -> definition.descriptor.key }.toSet().size == entries.size) {
            "duplicate immutable backend key"
        }
    }

    /** Domain descriptors projected from the sole definition snapshot. */
    val descriptors: List<BackendDescriptor> = this.definitions.map(BackendDefinition::descriptor)

    /** Backend runtime entries projected from the same definition snapshot. */
    val runtimeEntries: List<BackendRuntimeEntry> = this.definitions.map(BackendDefinition::runtimeEntry)

    /** UI entries projected from the same definition snapshot. */
    val uiEntries: List<BackendUiEntry> = this.definitions.map(BackendDefinition::uiEntry)

    /** Resolves a stable descriptor by exact key. */
    fun descriptor(key: String?): BackendDescriptor? = descriptors.firstOrNull { candidate ->
        candidate.key == key
    }

    /** Resolves an execution projection by exact key. */
    fun runtime(key: String): BackendRuntimeEntry? = runtimeEntries.firstOrNull { candidate ->
        candidate.descriptor.key == key
    }
}

/**
 * Single same-APK backend membership authority.
 *
 * @note No real attack backend is registered; reconnecting one requires its own approved vertical plan.
 */
internal object BackendRegistry {
    private val registry = BackendDefinitionRegistry(
        listOf(
            CustomBackendDefinition.definition,
            DiagnoseBackendDefinition.definition,
        ),
    )

    /** Domain descriptors in stable registry order. */
    val descriptors: List<BackendDescriptor> = registry.descriptors

    /** UI projections derived from the same immutable definition snapshot. */
    val uiEntries: List<BackendUiEntry> = registry.uiEntries

    /** Resolves a stable descriptor by exact key. */
    fun descriptor(key: String?): BackendDescriptor? = registry.descriptor(key)

    /** Resolves a backend-only runtime projection for execution callers. */
    fun runtime(key: String): BackendRuntimeEntry? = registry.runtime(key)

    /** Resolves current resource-free readiness by exact stable key. */
    fun availability(key: String): BackendAvailability? = runtime(key)?.availability()

    /** Number of selectable product-root entries; the disconnected-backend state deliberately permits zero. */
    val productionBackendCount: Int = descriptors.count { descriptor ->
        descriptor.runCapability == RunCapability.PRODUCT_ROOT && descriptor.selectable
    }
}
