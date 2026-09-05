package com.ghostlock.app.domain

import com.ghostlock.app.domain.model.KernelOffsets
import com.ghostlock.app.domain.model.SupportedKernels
import com.ghostlock.app.domain.usecase.OffsetMatching
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OffsetMatchingTest {
    private fun builtinWithLayoutScalars(): Map.Entry<String, Map<String, Long>> =
        SupportedKernels.BUILTIN.entries.firstOrNull {
            "compact_waiter" in it.value && "mm_struct_sz" in it.value
        } ?: error("no built-in table carries compact_waiter and mm_struct_sz")

    private fun entryFor(release: String, builtin: Map<String, Long>): KernelOffsets = KernelOffsets(
        release = release,
        scalars = listOf("pselect_waiter_shift", "compact_waiter", "mm_struct_sz")
            .associateWith { builtin[it] },
        symbols = emptyMap(),
        structFields = emptyMap(),
    )

    @Test
    fun identicalEntryMatches() {
        val builtin = builtinWithLayoutScalars()
        assertTrue(OffsetMatching.matchesBuiltin(entryFor(builtin.key, builtin.value), SupportedKernels.BUILTIN))
    }

    @Test
    fun differingCompactWaiterCountsAsDifferent() {
        val builtin = builtinWithLayoutScalars()
        val entry = entryFor(builtin.key, builtin.value)
        val scalars = entry.scalars.toMutableMap().apply { this["compact_waiter"] = getValue("compact_waiter")!! xor 1L }
        assertFalse(OffsetMatching.matchesBuiltin(entry.copy(scalars = scalars), SupportedKernels.BUILTIN))
    }

    @Test
    fun differingMmStructSzCountsAsDifferent() {
        val builtin = builtinWithLayoutScalars()
        val entry = entryFor(builtin.key, builtin.value)
        val scalars = entry.scalars.toMutableMap().apply { this["mm_struct_sz"] = getValue("mm_struct_sz")!! + 0x100 }
        assertFalse(OffsetMatching.matchesBuiltin(entry.copy(scalars = scalars), SupportedKernels.BUILTIN))
    }

    @Test
    fun nullLayoutScalarsStayIgnored() {
        val builtin = builtinWithLayoutScalars()
        val entry = entryFor(builtin.key, builtin.value)
        val scalars = entry.scalars.toMutableMap().apply {
            this["compact_waiter"] = null
            this["mm_struct_sz"] = null
        }
        assertTrue(OffsetMatching.matchesBuiltin(entry.copy(scalars = scalars), SupportedKernels.BUILTIN))
    }

    @Test
    fun unknownReleaseNeverMatches() {
        val builtin = builtinWithLayoutScalars()
        assertFalse(OffsetMatching.matchesBuiltin(entryFor("not-a-kernel", builtin.value), SupportedKernels.BUILTIN))
    }
}
