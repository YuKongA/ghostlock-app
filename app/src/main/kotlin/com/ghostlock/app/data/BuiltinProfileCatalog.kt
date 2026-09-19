package com.ghostlock.app.data

import android.content.Context

/**
 * Runtime catalogue of the bundled profiles, loaded from kernel_profiles/
 * index.conf on first use. Keeps the assets as the single source of truth
 * (replaces the former build-time SupportedKernels code generation).
 */
internal class BuiltinProfileCatalog(private val context: Context) {
    private val assetLoader = AssetConfigLoader(context)

    private data class Entry(
        val release: String,
        val recommendShizuku: Boolean,
        val fields: Map<String, Long>,
    )

    private val entries: List<Entry> by lazy { loadEntries() }

    val unames: Set<String> by lazy { entries.mapTo(linkedSetOf()) { it.release } }

    val recommendShizuku: Set<String> by lazy {
        entries.filter { it.recommendShizuku }.mapTo(linkedSetOf()) { it.release }
    }

    val builtin: Map<String, Map<String, Long>> by lazy {
        entries.associate { it.release to it.fields }
    }

    private fun loadEntries(): List<Entry> = runCatching {
        val index = readAsset("$BuiltinDirectory/index.conf")
            ?.let { HoconSupport.parseValue(it).asValueMap() } ?: return emptyList()
        val profiles = index["profiles"].asValueList() ?: return emptyList()
        profiles.mapNotNull { raw ->
            val entry = raw.asValueMap() ?: return@mapNotNull null
            val release = entry["release"] as? String ?: ""
            val file = entry["file"] as? String ?: ""
            if (release.isEmpty() || file.isEmpty()) return@mapNotNull null
            val profile = readAsset("$BuiltinDirectory/$file")
                ?.let { HoconSupport.parseValue(it).asValueMap() } ?: return@mapNotNull null
            val fields = flatten(profile)
            Entry(release, recommendShizuku = fields["recommend_shizuku"] == 1L, fields = fields)
        }
    }.getOrDefault(emptyList())

    private fun readAsset(path: String): String? =
        assetLoader.load(path).takeIf { it.isNotBlank() }

    /** Flat field map mirroring the former build-time generator. */
    private fun flatten(profile: ValueMap): Map<String, Long> {
        val fields = linkedMapOf<String, Long>()
        for ((key, raw) in profile) {
            when (key) {
                "schema_version", "release", "execution" -> Unit

                "route" -> raw.asValueMap()?.forEach { (name, branchRaw) ->
                    branchRaw.asValueMap()?.forEach { (field, value) ->
                        (value as? Number)?.toLong()?.let { fields[routeFieldKey(name, field)] = it }
                    }
                }

                "fallback" -> raw.asValueMap()?.let { fallback ->
                    val to = fallback["to"] as? String ?: ""
                    val branch = fallback["route"].asValueMap()?.get(to).asValueMap() ?: return@let
                    branch.forEach { (field, value) ->
                        (value as? Number)?.toLong()
                            ?.let { fields.putIfAbsent(routeFieldKey(to, field), it) }
                    }
                }

                "kernelsnitch", "task_struct", "cred", "offset" ->
                    raw.asValueMap()?.forEach { (field, value) ->
                        (value as? Number)?.toLong()?.let { fields["$key.$field"] = it }
                    }

                "symbols", "struct_fields" ->
                    raw.asValueMap()?.forEach { (field, value) ->
                        (value as? Number)?.toLong()?.let { fields[field] = it }
                    }

                else -> (raw as? Number)?.toLong()?.let { fields[key] = it }
            }
        }
        return fields
    }

    private fun routeFieldKey(route: String, field: String): String = when {
        route == "tcp_zerocopy" && field == "compact_waiter" -> "compact_waiter"
        route == "select_stack" && field == "waiter_shift" -> "pselect_waiter_shift"
        route == "multicast_waiter" -> "mcast.$field"
        else -> "$route.$field"
    }

    private companion object {
        const val BuiltinDirectory = "kernel_profiles"
    }
}
