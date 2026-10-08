package com.ghostlock.app.data.component

import com.ghostlock.app.data.StepSetKind

/**
 * stepset -> ordered step sequence, for read-only consumers (UI).
 *
 * SINGLE AUTHORITY: the sequence is READ from the native-exported manifest
 * [RESOURCE] (runtime copy in this module), which `make -C src
 * stepset-steps-manifest` regenerates from `contract/step_catalog.hpp` and the
 * executor orders in `backend/cve_2026_43499/steps.hpp`; the native host test
 * `src/core/tests/stepset_steps_manifest_test.cpp` pins that same text and both
 * committed copies must stay byte-identical. No step name is written here: the
 * manifest is the authority for membership AND order (`step_index` ascending).
 */
object StepSequenceCatalog {
    /** The native-exported manifest, on the runtime classpath. */
    const val RESOURCE: String = "stepset-steps.tsv"

    /** Columns: stepset, step_index, step_name. */
    private const val COLUMNS = 3

    private data class Row(val stepset: String, val index: Int, val step: String)

    private val rows: List<Row> by lazy {
        val stream = StepSequenceCatalog::class.java.classLoader
            ?.getResourceAsStream(RESOURCE)
            ?: throw IllegalArgumentException("missing stepset manifest resource: " + RESOURCE)
        parse(stream.bufferedReader().use { it.readText() })
    }

    private fun parse(text: String): List<Row> = buildList {
        text.lineSequence().forEachIndexed { position, raw ->
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("#")) return@forEachIndexed
            val parts = line.split('\t')
            require(parts.size == COLUMNS) {
                RESOURCE + ":" + (position + 1) + ": expected " + COLUMNS +
                    " tab separated columns, got " + parts.size
            }
            val index = parts[1].toIntOrNull()
            require(index != null && index >= 0) {
                RESOURCE + ":" + (position + 1) +
                    ": step_index must be a non-negative integer: " + parts[1]
            }
            add(Row(parts[0], index, parts[2]))
        }
    }

    /** Every stepset token the manifest declares, first-appearance order. */
    val declaredStepsets: List<String> by lazy { rows.map { it.stepset }.distinct() }

    /**
     * The ordered HOCON step tokens of [kind] (step_index ascending). An
     * unmapped stepset is a HARD failure, never an empty list: a silent empty
     * sequence would render an empty queue instead of reporting the drift.
     */
    fun stepsOf(kind: StepSetKind): List<String> {
        val mine = rows.filter { it.stepset == kind.token }
        require(mine.isNotEmpty()) {
            "the stepset manifest has no rows for " + kind.token + " (" + RESOURCE +
                "); regenerate with: make -C src stepset-steps-manifest"
        }
        return mine.sortedBy { it.index }.map { it.step }
    }
}

/** Read-only view: the ordered step tokens of this stepset (see [StepSequenceCatalog]). */
fun StepSetKind.stepNames(): List<String> = StepSequenceCatalog.stepsOf(this)
