package com.ghostlock.app.domain.usecase

import com.ghostlock.app.domain.model.KernelOffsets

/** Compares imported offsets with the generated built-in kernel tables. */
object OffsetMatching {
    fun matchesBuiltin(entry: KernelOffsets, builtins: Map<String, Map<String, Long>>): Boolean {
        val builtin = builtins[entry.release] ?: return false
        entry.scalars["kernel_phys_load"]?.let { phys ->
            if (phys != builtin["kernel_phys_load"]) return false
        }
        if (entry.scalars.any { (key, value) ->
                value != null && builtin[key]?.let { it != value } == true
            }) return false
        return !objectFieldDiffers(builtin, entry.symbols) &&
                !objectFieldDiffers(builtin, entry.structFields)
    }

    fun objectFieldDiffers(builtin: Map<String, Long>, fields: Map<String, Long?>): Boolean =
        fields.any { (key, value) -> value != null && builtin[key]?.let { it != value } == true }
}
