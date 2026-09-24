package com.ghostlock.app.data.profile

import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.copyValue
import com.ghostlock.app.data.deepMergeValues
import com.ghostlock.app.data.getLongAt
import com.ghostlock.app.data.mutableChild
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.data.valueMapOf

/**
 * Pure merge of the profile layers, lifted out of the Android controller so the
 * app and the Gradle exporter share one implementation. All inputs are already
 * loaded value maps; this class performs no IO.
 */
object ProfileMerger {
    private val RouteNames: List<String> = RouteKind.entries.map { it.token }

    /**
     * common execution preset -> built-in -> imported -> sparse override, then
     * the session CPU pair is forced in and the selected route groups are filled.
     */
    fun resolveMerged(
        deviceRelease: String,
        builtin: ValueMap?,
        imported: ValueMap?,
        overrides: ValueMap?,
        tuningExecution: ValueMap?,
        pair: CpuPairView,
        routePresets: Map<String, ValueMap>,
    ): ValueMap {
        val defaults = valueMapOf("release" to deviceRelease).apply {
            if (tuningExecution != null) put("execution", tuningExecution)
        }
        val resolved = mergeSource(
            mergeSource(
                if (builtin == null) defaults else deepMergeValues(defaults, builtin),
                imported,
            ),
            overrides,
        )
        resolved["schema_version"] = 1
        /* A manually chosen builtin still reports the device release so the
         * native release gate and the exported document stay coherent. */
        resolved["release"] = deviceRelease
        val selectedOverride = imported?.get("execution").asValueMap()?.get("selected_cpus").asValueMap()
            ?: overrides?.get("execution").asValueMap()?.get("selected_cpus").asValueMap()
        applySelectedCpus(resolved, pair, selectedOverride)
        fillRouteExecutionDefaults(resolved, routePresets)
        validateResolved(resolved, deviceRelease)
        return resolved
    }

    /** Reads the effective `selected_cpus` override from an imported/override layer. */
    fun selectedCpusOverride(imported: ValueMap?, overrides: ValueMap?): ValueMap? =
        imported?.get("execution").asValueMap()?.get("selected_cpus").asValueMap()
            ?: overrides?.get("execution").asValueMap()?.get("selected_cpus").asValueMap()

    private fun applySelectedCpus(
        resolved: ValueMap,
        pair: CpuPairView,
        selectedOverride: ValueMap?,
    ) {
        val execution = resolved.mutableChild("execution")
        if (selectedOverride == null) {
            execution["selected_cpus"] = valueMapOf(
                "main" to pair.main.toLong(),
                "consumer" to pair.consumer.toLong(),
            )
        } else {
            execution["selected_cpus"] = selectedOverride
        }
    }

    /** Fills the route-tuning groups absent from the profile from the presets. */
    fun fillRouteExecutionDefaults(profile: ValueMap, routePresets: Map<String, ValueMap>) {
        val execution = profile["execution"].asValueMap() ?: return
        val routes = execution.mutableChild("routes")
        for (route in RouteNames) {
            if (routes.containsKey(route)) continue
            val defaults = routePresets[route] ?: continue
            routes[route] = defaults
        }
    }

    /**
     * Route objects hold exactly one branch. An incoming branch that differs
     * from the base replaces it (the old geometry belongs to the old route),
     * while the same branch merges field-wise.
     */
    private fun mergeRouteObjects(base: ValueMap?, incoming: Map<*, *>?): ValueMap? {
        if (incoming == null) return base
        val incomingBranch = incoming.keys.filterIsInstance<String>()
            .firstOrNull { it in RouteNames } ?: return base
        val incomingBody = incoming[incomingBranch].asValueMap() ?: valueMapOf()
        val baseBranch = base?.keys?.toList()?.firstOrNull { it in RouteNames }
        return when {
            baseBranch == incomingBranch -> valueMapOf(
                incomingBranch to deepMergeValues(
                    base[incomingBranch].asValueMap() ?: valueMapOf(),
                    incomingBody,
                ),
            )

            else -> valueMapOf(incomingBranch to incomingBody)
        }
    }

    /** deepMerge plus branch-replacement semantics for route/fallback.route. */
    private fun mergeSource(base: ValueMap, incoming: ValueMap?): ValueMap {
        val baseRoute = base["route"].asValueMap()?.copyValue().asValueMap()
        val baseFallbackRoute = base["fallback"].asValueMap()
            ?.get("route").asValueMap()?.copyValue().asValueMap()
        val merged = deepMergeValues(base, incoming)
        if (incoming == null) return merged
        incoming["route"].asValueMap()?.let { route ->
            mergeRouteObjects(baseRoute, route)?.let { merged["route"] = it }
        }
        incoming["fallback"].asValueMap()?.get("route").asValueMap()?.let { fallbackRoute ->
            val fallback = merged["fallback"].asValueMap() ?: return@let
            mergeRouteObjects(baseFallbackRoute, fallbackRoute)?.let { fallback["route"] = it }
        }
        return merged
    }

    private fun validateResolved(profile: ValueMap, release: String) {
        require(profile["release"] == release) { "profile release mismatch" }
        require((profile["kernel_major"] as? Number)?.toInt() in 5..6) { "invalid kernel_major" }
        require(profile.containsKey("execution")) { "missing execution tuning" }
    }
}

/** Minimal CPU pair the merger needs; keeps the app's domain type out of core. */
data class CpuPairView(val main: Int, val consumer: Int)
