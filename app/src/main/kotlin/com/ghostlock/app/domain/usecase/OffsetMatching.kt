package com.ghostlock.app.domain.usecase

import com.ghostlock.app.domain.model.KernelOffsets

/** Compares imported offsets with the generated built-in kernel tables. */
object OffsetMatching {
    const val MtkDefaultPhysLoad = 0x80000000L

    fun matchesBuiltin(entry: KernelOffsets, builtins: Map<String, Map<String, Long>>): Boolean {
        val builtin = builtins[entry.release] ?: return false
        if (fieldDiffers(builtin, entry.scalars, "pselect_waiter_shift")) return false
        if (fieldDiffers(builtin, entry.scalars, "compact_waiter")) return false
        if (fieldDiffers(builtin, entry.scalars, "mm_struct_sz")) return false
        entry.scalars["kernel_phys_load"]?.let { phys ->
            if (phys != MtkDefaultPhysLoad && fieldDiffers(builtin, entry.scalars, "kernel_phys_load")) {
                return false
            }
        }
        return !objectFieldDiffers(builtin, entry.symbols) &&
                !objectFieldDiffers(builtin, entry.structFields)
    }

    fun fieldDiffers(builtin: Map<String, Long>, fields: Map<String, Long?>, key: String): Boolean {
        val value = fields[key] ?: return false
        val expected = builtin[key] ?: return false
        return value != expected
    }

    fun objectFieldDiffers(builtin: Map<String, Long>, fields: Map<String, Long?>): Boolean =
        fields.any { (key, value) -> value != null && builtin[key]?.let { it != value } == true }
}
