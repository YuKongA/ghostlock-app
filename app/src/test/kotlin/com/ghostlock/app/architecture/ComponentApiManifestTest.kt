package com.ghostlock.app.architecture

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Locks all 74 current component files and their public/internal reusable declaration identities. */
class ComponentApiManifestTest {
    /** Verifies file coverage, canonical API keys, signatures, and one-to-one mappings. */
    @Test
    fun `component manifest covers every reusable declaration`() {
        val result = D0ToolTestSupport.verify("component")
        assertEquals(result.output, 0, result.exitCode)
        assertTrue(result.output, result.output.contains("COMPONENT_FILES=74"))
        assertTrue(result.output, result.output.contains("COMPONENT_UNCLASSIFIED=0"))
        assertTrue(result.output, result.output.contains("API_KEY_COLLISIONS=0"))
        assertTrue(result.output, result.output.contains("API_MAPPING_INVALID=0"))
    }
}
