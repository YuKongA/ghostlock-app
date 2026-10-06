package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import org.junit.Assert.assertEquals
import java.io.File

/**
 * Shared helpers for the M3 (token list -> step queue) migration and its guards
 * (design doc step-queue-design 5-Q1/Q2; acceptance = E1 normalised plan
 * equivalence, NOT literal flatten/hex identity - the two forms deliberately
 * carry different canonical keys).
 *
 * The step sequence ALWAYS comes from the native export `stepset-steps.tsv`
 * (never a hand-written list), and the route ALWAYS from CombinationCatalog.
 */
internal object StepQueueEquivalence {

    /** backend -> tokens, from one `available { ... }` block. */
    data class TokenList(val backend: String, val tokens: List<String>)

    data class Plan(val backend: String, val route: String?, val steps: List<String>)

    private val tokenListLine =
        Regex("^\\s*([A-Za-z0-9_.]+)\\s*=\\s*\\[\\s*(\"[^\"]+\"(?:\\s*,\\s*\"[^\"]+\")*)\\s*]\\s*$")

    /** The native export: stepset<TAB>step_index<TAB>step_name. Fail-closed. */
    fun loadStepsetSteps(file: File): Map<String, List<String>> {
        check(file.isFile) { "missing stepset-steps table: " + file }
        val rows = file.readLines()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .map { line -> line.split('\t') }
        rows.forEach { parts ->
            check(parts.size == 3) { "stepset-steps.tsv needs 3 columns: " + parts.joinToString("|") }
        }
        return rows.groupBy({ it[0] }, { it[2] })
    }

    val stepsets: Map<String, List<String>> by lazy {
        loadStepsetSteps(File("src/test/resources/stepset-steps.tsv"))
    }

    fun backendKindOf(token: String): BackendKind = requireNotNull(
        BackendKind.entries.firstOrNull { token == it.token },
    ) { "unknown backend token " + token }

    /** Token lists declared inside `available { ... }` blocks (token form). */
    fun tokenLists(text: String): List<TokenList> {
        val out = mutableListOf<TokenList>()
        var inside = false
        for (line in text.lines()) {
            val trimmed = line.trim()
            if (trimmed == "available {") { inside = true; continue }
            if (inside && trimmed == "}") { inside = false; continue }
            if (!inside) continue
            val match = tokenListLine.find(line) ?: continue
            val tokens = Regex("\"([^\"]+)\"").findAll(match.groupValues[2])
                .map { it.groupValues[1] }.toList()
            out += TokenList(match.groupValues[1], tokens)
        }
        return out
    }

    /**
     * EXPLICIT migration predicate (never a fuzzy fallback): the object form
     * declares `available { <backend> { ... } }`, the token form `= [ ... ]`.
     */
    fun isMigrated(text: String): Boolean {
        var inside = false
        for (line in text.lines()) {
            val trimmed = line.trim()
            if (trimmed == "available {") { inside = true; continue }
            if (inside && trimmed == "}") { inside = false; continue }
            if (!inside) continue
            if (Regex("^[A-Za-z0-9_.]+\\s*\\{").containsMatchIn(trimmed)) return true
        }
        return false
    }

    /** Backend keys declared by the migrated (object) available block. */
    fun backendsOf(text: String): List<String> {
        var inside = false
        val out = mutableListOf<String>()
        for (line in text.lines()) {
            val trimmed = line.trim()
            if (trimmed == "available {") { inside = true; continue }
            if (inside && trimmed == "}") { inside = false; continue }
            if (!inside) continue
            val match = Regex("^([A-Za-z0-9_.]+)\\s*\\{").find(trimmed)
            if (match != null) out += match.groupValues[1]
        }
        return out
    }

    /** The legacy fixture's selection: the flat backend.<id>.steps token. */
    fun flatTokenOf(text: String, backend: String): String? {
        val canonical = ProfileLayout.canonicalize(
            requireNotNull(HoconSupport.parseValue(text).asValueMap()),
        )
        return ProfileLayout.flatten(canonical)["backend." + backend + ".steps"] as? String
    }

    /** Expected side: catalogue (route) + the native stepset table (steps). */
    fun tokenPlan(backend: String, token: String): Plan {
        val spec = requireNotNull(CombinationCatalog.resolve(backendKindOf(backend), token)) {
            "token " + token + " is not in the combination catalogue"
        }
        return Plan(backend, spec.route?.token, requireNotNull(stepsets[spec.steps.token]))
    }

    /** Actual side: re-parsed from the document text (object/queue form). */
    fun queuePlan(text: String, backend: String): Plan {
        val canonical = ProfileLayout.canonicalize(
            requireNotNull(HoconSupport.parseValue(text).asValueMap()),
        )
        val owner = requireNotNull(canonical["backend"].asValueMap()?.get(backend).asValueMap()) {
            "no backend owner for " + backend
        }
        val route = (owner["route"] as? String) ?: (owner["queue_route"] as? String)
        val queue = requireNotNull(owner["queue"] as? List<*>) { "no queue for " + backend }
        val steps = queue.map { element ->
            val map = element as? Map<*, *>
            (map?.get("step") as? String) ?: error("queue element without step: " + element)
        }
        return Plan(backend, route, steps)
    }

    /**
     * M3 guard predicate, extracted so it can be falsified in isolation: a
     * migrated asset must carry ONLY the queue (native rejects a backend section
     * holding both queue and steps, src/core/profile/glkv3_parse.cpp:407-419).
     */
    fun hasTokenBesideQueue(text: String): Boolean {
        if (!isMigrated(text)) return false
        return text.lines().any { line ->
            val trimmed = line.trim()
            trimmed.startsWith("steps = \"") || trimmed.startsWith("steps=\"")
        }
    }

    /** The checker: pure, so a defect on either side is visible (falsifiable). */
    fun assertPlanEquals(expected: Plan, actual: Plan) {
        assertEquals("backend", expected.backend, actual.backend)
        assertEquals("route", expected.route, actual.route)
        assertEquals("step sequence", expected.steps, actual.steps)
    }
}