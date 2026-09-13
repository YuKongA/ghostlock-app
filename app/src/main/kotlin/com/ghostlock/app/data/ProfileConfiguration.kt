package com.ghostlock.app.data

import android.content.Context
import com.ghostlock.app.domain.model.CpuPair
import org.json.JSONArray
import org.json.JSONObject
import org.json.JSONTokener
import java.io.File

/** Kotlin-owned profile loading, sparse override merging and run transport. */
internal object ProfileConfiguration {
    private const val BuiltinDirectory = "kernel_profiles"

    // TODO(profile-ui): Show source/version and merge diffs; add validated advanced
    // editing, recommended-core apply, reset, import/export and rollback workflows.

    fun resolve(
        context: Context,
        importedFile: File,
        release: String,
        selectedCpus: CpuPair,
    ): String {
        val index = context.assets.open("$BuiltinDirectory/index.json").bufferedReader().use { reader ->
            JSONObject(reader.readText())
        }
        require(index.optInt("schema_version") == 1) { "unsupported profile schema" }
        val builtinEntry = findProfile(index.getJSONArray("profiles"), release)
        val builtin = builtinEntry?.let { entry ->
            context.assets.open("$BuiltinDirectory/${entry.getString("file")}")
                .bufferedReader().use { reader -> JSONObject(reader.readText()) }
                .also {
                    require(it.optInt("schema_version") == 1) { "unsupported profile schema" }
                    require(it.optString("release") == release) { "profile index release mismatch" }
                }
        }
        val imported = readProfiles(importedFile)?.let { findProfile(it, release) }
        require(builtin != null || imported != null) { "no profile for kernel: $release" }
        val executionDefaults = context.assets.open("$BuiltinDirectory/defaults.json")
            .bufferedReader().use { reader -> JSONObject(reader.readText()).getJSONObject("execution") }
        val defaults = JSONObject().apply {
            put("release", release)
            // Execution policy is schema-wide for now; S08 will consume per-profile overrides.
            put("execution", executionDefaults)
        }
        val resolved = deepMerge(
            if (builtin == null) defaults else deepMerge(defaults, builtin),
            imported,
        )
        resolved.put("schema_version", 1)
        resolved.getJSONObject("execution")
            .put("selected_cpus", JSONObject().apply {
                put("main", selectedCpus.primary)
                put("consumer", selectedCpus.consumer)
            })
        validateResolved(resolved, release)
        return resolved.toString(2)
    }

    private fun readProfiles(file: File): JSONArray? {
        if (!file.isFile) return null
        return when (val value = JSONTokener(file.readText()).nextValue()) {
            is JSONArray -> value
            is JSONObject -> value.optJSONArray("profiles")
                ?: JSONArray().put(value)
            else -> null
        }
    }

    private fun findProfile(profiles: JSONArray, release: String): JSONObject? =
        (0 until profiles.length()).asSequence()
            .mapNotNull(profiles::optJSONObject)
            .firstOrNull { it.optString("release") == release }

    private fun deepMerge(base: JSONObject, override: JSONObject?): JSONObject {
        if (override == null) return base
        override.keys().forEach { key ->
            val incoming = override.opt(key)
            val current = base.opt(key)
            if (incoming is JSONObject && current is JSONObject) {
                base.put(key, deepMerge(current, incoming))
            } else if (incoming != null && incoming !== JSONObject.NULL) {
                base.put(key, incoming)
            }
        }
        return base
    }

    private fun validateResolved(profile: JSONObject, release: String) {
        require(profile.optString("release") == release) { "profile release mismatch" }
        require(profile.optInt("kernel_major") in 5..6) { "invalid kernel_major" }
        require(profile.optLong("off_init_task") != 0L) { "missing off_init_task" }
        require(profile.optLong("off_init_cred") != 0L) { "missing off_init_cred" }
        require(profile.has("execution")) { "missing execution tuning" }
    }
}
