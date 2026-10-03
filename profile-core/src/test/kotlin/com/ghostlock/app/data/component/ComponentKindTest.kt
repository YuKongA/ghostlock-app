package com.ghostlock.app.data.component

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Pins the App-side component model to the native wire/catalog authority. */
class ComponentKindTest {
    @Test
    fun `frontend ids match the native wire values`() {
        assertEquals(1, FrontendKind.RootChild.wire)
        assertEquals(2, FrontendKind.UmhForward.wire)
        assertEquals("root_child", FrontendKind.RootChild.token)
        assertEquals("umh_forward", FrontendKind.UmhForward.token)
    }

    @Test
    fun `backend ids match the native wire values`() {
        assertEquals(1, BackendKind.Cve2026_43499.wire)
        assertEquals(2, BackendKind.Cve2026_64560.wire)
        assertEquals(6, BackendKind.Cve2026_43284.wire)
        assertEquals("cve_2026_43499", BackendKind.Cve2026_43499.token)
        assertEquals("cve_2026_43284", BackendKind.Cve2026_43284.token)
    }

    @Test
    fun `only implemented components are available`() {
        assertTrue(ComponentAvailability.frontendAvailable(FrontendKind.RootChild))
        assertFalse(ComponentAvailability.frontendAvailable(FrontendKind.UmhForward))
        assertTrue(ComponentAvailability.backendAvailable(BackendKind.Cve2026_43499))
        assertFalse(ComponentAvailability.backendAvailable(BackendKind.Cve2026_64560))
        assertFalse(ComponentAvailability.backendAvailable(BackendKind.Cve2026_43284))
    }

    @Test
    fun `unavailable backend is never selected`() {
        assertEquals(
            BackendKind.Cve2026_43499,
            BackendKind.selectableOrFallback(BackendKind.Cve2026_43284),
        )
        assertEquals(
            BackendKind.Cve2026_43499,
            BackendKind.selectableOrFallback(BackendKind.Cve2026_64560),
        )
        assertEquals(
            BackendKind.Cve2026_43499,
            BackendKind.selectableOrFallback(null),
        )
        assertEquals(
            BackendKind.Cve2026_43499,
            BackendKind.selectableOrFallback(BackendKind.Cve2026_43499),
        )
    }

    @Test
    fun `stored backend spellings are migrated`() {
        assertEquals(BackendKind.Cve2026_43499, BackendKind.fromStored("cve_2026_43499"))
        assertEquals(BackendKind.Cve2026_43499, BackendKind.fromStored("Cve2026_43499"))
        assertEquals(BackendKind.Cve2026_43499, BackendKind.fromStored("1"))
        assertEquals(BackendKind.Cve2026_43284, BackendKind.fromStored("6"))
        assertEquals(BackendKind.Cve2026_43284, BackendKind.fromStored("cve_2026_43284"))
        assertNull(BackendKind.fromStored("bogus"))
        assertNull(BackendKind.fromStored(""))
        assertNull(BackendKind.fromStored(null))
    }
}
