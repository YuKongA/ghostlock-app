package com.ghostlock.app.ui.backend

import androidx.compose.runtime.Composable
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.ui.icon.IconPair
import com.ghostlock.app.ui.icon.Icons
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** UI projection behavior without a second backend registry or presentation DTO. */
class BackendUiCatalogTest {
    /** A disabled first entry falls back to the first selectable entry. */
    @Test
    fun selectedEntryUsesSelectableFallback() {
        val catalog = catalog(selectedKey = { "CUSTOM" })
        assertEquals("DIAGNOSE", catalog.selectedEntry()?.descriptor?.key)
    }

    /** Exact selected keys preserve UI identity, localization, icons, and readiness callbacks. */
    @Test
    fun exactSelectionResolvesUiOwnedFacts() {
        val catalog = catalog(selectedKey = { "DIAGNOSE" })
        val entry = requireNotNull(catalog.entry("DIAGNOSE"))
        assertEquals("text-2", catalog.displayName(entry))
        assertEquals("text-1", catalog.categoryLabel(entry))
        assertTrue(catalog.availabilityFor("DIAGNOSE").isReady)
        assertFalse(catalog.availabilityFor("CUSTOM").isReady)
        assertTrue(entry.ui.icon === Icons.Filled.BugReport)
    }

    private fun catalog(selectedKey: () -> String?): BackendUiCatalog = BackendUiCatalog(
        entries = listOf(
            entry("CUSTOM", BackendSelectionPolicy.DISABLED),
            entry("DIAGNOSE", BackendSelectionPolicy.SELECTABLE),
        ),
        selectedKey = selectedKey,
        availability = { key ->
            if (key == "DIAGNOSE") BackendAvailability.Ready()
            else BackendAvailability.NotReady(
                listOf(com.ghostlock.app.domain.backend.BackendAvailabilityReason("disabled", "Disabled")),
            )
        },
        text = { resource -> "text-$resource" },
    )

    private fun entry(key: String, selection: BackendSelectionPolicy): BackendUiEntry = BackendUiEntry(
        descriptor = BackendDescriptor(
            key = key,
            selectionPolicy = selection,
            abortPolicy = AbortPolicy.BEFORE_START_ONLY,
            executionDomain = ExecutionDomain.APP,
            runCapability = if (key == "DIAGNOSE") {
                RunCapability.DIAGNOSTIC_ONLY
            } else {
                RunCapability.PRODUCT_ROOT
            },
        ),
        ui = TestBackendUi,
    )

    private object TestBackendUi : BackendUi {
        override val categoryLabelRes: Int = 1
        override val displayNameRes: Int = 2
        override val icon: IconPair = Icons.Filled.BugReport

        /** Test-only empty content; catalog tests exercise identity and readiness only. */
        @Composable
        override fun Content(availability: BackendAvailability) = Unit
    }
}
