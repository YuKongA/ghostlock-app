package com.ghostlock.app.data.plugin

/** An ENABLED plugin plus the descriptor that describes it, before overrides. */
data class EnabledPlugin(
    val entry: PluginManifestEntry,
    val descriptor: PluginDescriptor,
)

/**
 * Extracts the user's plugin parameter overrides from the generic advanced
 * override tree (dotted paths, the App's single override mechanism).
 *
 * The descriptor decides the type: an override whose stored value cannot be
 * read as the declared type fails closed with its path, so a stale override can
 * never silently change a parameter's meaning. Only explicit overrides are
 * returned — even when one equals the declared default — because "the user
 * chose this" and "the plugin defaults to this" must stay distinguishable; the
 * default value itself remains the descriptor's authority (R1).
 */
object PluginOverrides {
    private const val PARAMS = "params"
    private const val EXTRACT = "extract"

    /** Override tree path of one plugin parameter: plugin.<id>.params.<name>. */
    fun path(id: String, name: String): String = "plugin." + id + "." + PARAMS + "." + name

    /** Override tree path of one extractor key: plugin.<id>.extract.<name>. */
    fun extractPath(id: String, name: String): String =
        "plugin." + id + "." + EXTRACT + "." + name

    /**
     * The explicit parameter overrides of [id], keyed by parameter name. [tree]
     * is the advanced override document (nested maps); an absent plugin section
     * means "no overrides".
     */
    fun params(tree: Map<*, *>, id: String, descriptor: PluginDescriptor): Map<String, PluginValue> =
        group(tree, id, PARAMS, "parameter", descriptor.paramsByName)

    /**
     * The extractor values of [id] (P2): `plugin.<id>.extract.<key>` in the same
     * override document. The descriptor's extract rows are the only authority
     * for the key names and types, so a value the module does not declare (or a
     * value the extractor never resolved) is refused rather than emitted.
     */
    fun extract(tree: Map<*, *>, id: String, descriptor: PluginDescriptor): Map<String, PluginValue> =
        group(tree, id, EXTRACT, "extract key", descriptor.extractByName)

    private fun group(
        tree: Map<*, *>,
        id: String,
        groupKey: String,
        label: String,
        declared: Map<String, PluginParam>,
    ): Map<String, PluginValue> {
        val plugin = (tree["plugin"] as? Map<*, *>) ?: return emptyMap()
        val section = (plugin[id] as? Map<*, *>) ?: return emptyMap()
        val raw = (section[groupKey] as? Map<*, *>) ?: return emptyMap()
        val out = linkedMapOf<String, PluginValue>()
        for ((key, value) in raw) {
            val name = key as? String ?: fail("plugin override key is not text: " + key)
            val path = "plugin." + id + "." + groupKey + "." + name
            val param = declared[name]
                ?: fail("plugin " + id + " declares no such " + label + ": " + path)
            out[name] = valueOf(param.type, value, path)
        }
        return out
    }

    /** Every contract violation is an IllegalArgumentException (fail closed). */
    private fun fail(reason: String): Nothing = throw IllegalArgumentException(reason)

    /** Reads one stored override as the declared type; fail-closed on mismatch. */
    private fun valueOf(type: PluginParamType, value: Any?, path: String): PluginValue = when (type) {
        PluginParamType.UInt -> {
            val number = (value as? Number)?.toLong() ?: fail("plugin override is not a number: " + path)
            require(number >= 0L) { "plugin override is negative: " + path }
            PluginValue.UInt(number.toULong())
        }

        PluginParamType.Int -> PluginValue.Int(
            (value as? Number)?.toLong() ?: fail("plugin override is not a number: " + path),
        )

        PluginParamType.Bool -> PluginValue.Bool(
            when (value) {
                is Boolean -> value
                is Number -> value.toLong() != 0L
                else -> fail("plugin override is not a boolean: " + path)
            },
        )

        PluginParamType.Str -> PluginValue.Str(
            value as? String ?: fail("plugin override is not text: " + path),
        )
    }
}
