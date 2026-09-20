package com.ghostlock.app.domain.usecase

import com.ghostlock.app.domain.model.KernelOffsets
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OffsetMatchingTest {

    @Test
    fun `explicit kernel_phys_load differing from an omitted builtin is not a match`() {
        val entry = offsets(scalars = mapOf("kernel_phys_load" to 2818572288L))
        val builtins = mapOf("5.15.0-test" to emptyMap<String, Long>())

        assertFalse(OffsetMatching.matchesBuiltin(entry, builtins))
    }

    @Test
    fun `kernel_phys_load equal to the builtin is a match`() {
        val entry = offsets(scalars = mapOf("kernel_phys_load" to 2818572288L))
        val builtins = mapOf("5.15.0-test" to mapOf("kernel_phys_load" to 2818572288L))

        assertTrue(OffsetMatching.matchesBuiltin(entry, builtins))
    }

    @Test
    fun `null kernel_phys_load is ignored`() {
        val entry = offsets(scalars = mapOf("kernel_phys_load" to null))
        val builtins = mapOf("5.15.0-test" to emptyMap<String, Long>())

        assertTrue(OffsetMatching.matchesBuiltin(entry, builtins))
    }

    @Test
    fun `scalar absent from the builtin does not block a match`() {
        val entry = offsets(scalars = mapOf("mcast.lock_slots_offset" to 128L))
        val builtins = mapOf("5.15.0-test" to emptyMap<String, Long>())

        assertTrue(OffsetMatching.matchesBuiltin(entry, builtins))
    }

    @Test
    fun `scalar differing from the builtin is not a match`() {
        val entry = offsets(scalars = mapOf("compact_waiter" to 2L))
        val builtins = mapOf("5.15.0-test" to mapOf("compact_waiter" to 1L))

        assertFalse(OffsetMatching.matchesBuiltin(entry, builtins))
    }

    @Test
    fun `unknown release is not a match`() {
        assertFalse(OffsetMatching.matchesBuiltin(offsets(), mapOf("other" to emptyMap())))
    }

    private fun offsets(
        scalars: Map<String, Long?> = emptyMap(),
        symbols: Map<String, Long?> = emptyMap(),
        structFields: Map<String, Long?> = emptyMap(),
    ) = KernelOffsets(
        release = "5.15.0-test",
        scalars = scalars,
        symbols = symbols,
        structFields = structFields,
    )
}
