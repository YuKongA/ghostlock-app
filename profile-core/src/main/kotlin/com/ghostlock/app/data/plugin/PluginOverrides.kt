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

    /** Override tree path prefix of one plugin parameter. */
    fun path(id: String, name: String): String = "plugin." + id + "." + PARAMS + "." + name

    /**
     * The explicit overrides of [id], keyed by parameter name. [tree] is the
     * advanced override document (nested maps); an absent plugin section means
     * "no overrides".
     */
    fun params(tree: Map<*, *>, id: String, descriptor: PluginDescriptor): Map<String, PluginValue> {
        val plugin = (tree["plugin"] as? Map<*, *>) ?: return emptyMap()
        val section = (plugin[id] as? Map<*, *>) ?: return emptyMap()
        val raw = (section[PARAMS] as? Map<*, *>) ?: return emptyMap()
        val out = linkedMapOf<String, PluginValue>()
        for ((key, value) in raw) {
            val name = key as? String ?: fail("plugin override key is not text: " + key)
            val path = path(id, name)
            val param = descriptor.paramsByName[name]
                ?: fail("plugin " + id + " declares no such parameter: " + path)
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
