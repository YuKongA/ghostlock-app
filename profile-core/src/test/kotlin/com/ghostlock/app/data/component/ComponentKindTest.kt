package com.ghostlock.app.data.component

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
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
    }

    @Test
    fun `only implemented components are available`() {
        assertTrue(ComponentAvailability.frontendAvailable(FrontendKind.RootChild))
        assertFalse(ComponentAvailability.frontendAvailable(FrontendKind.UmhForward))
        assertTrue(ComponentAvailability.backendAvailable(BackendKind.Cve2026_43499))
        assertFalse(ComponentAvailability.backendAvailable(BackendKind.Cve2026_64560))
    }
}
