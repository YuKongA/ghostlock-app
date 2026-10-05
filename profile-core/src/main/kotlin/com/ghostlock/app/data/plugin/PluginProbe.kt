package com.ghostlock.app.data.plugin

/**
 * P1 plugin descriptor vocabulary (interface freeze 2026-10-05, contract-design
 * §3.14.7).
 *
 * These types mirror the native probe output: the probe is the only authority
 * for what a shared object declares, and this parser is the Kotlin half of that
 * contract. Nothing here is hard-coded from a specific plugin.
 */

/** Parameter wire types; both sides use the GLKv3 WireKind literals. */
enum class PluginParamType(val text: String) {
    UInt("uint"),
    Int("int"),
    Bool("bool"),
    Str("str"),
    ;

    companion object {
        fun fromText(text: String): PluginParamType? = entries.firstOrNull { it.text == text }
    }
}

/** A typed parameter value, as written into the document. */
sealed interface PluginValue {
    data class UInt(val value: ULong) : PluginValue
    data class Int(val value: Long) : PluginValue
    data class Bool(val value: Boolean) : PluginValue
    data class Str(val value: String) : PluginValue

    val type: PluginParamType
        get() = when (this) {
            is UInt -> PluginParamType.UInt
            is Int -> PluginParamType.Int
            is Bool -> PluginParamType.Bool
            is Str -> PluginParamType.Str
        }
}

/** One declared parameter (or extract requirement) of a plugin. */
data class PluginParam(
    val name: String,
    val type: PluginParamType,
    val required: Boolean,
    /** Null = no default; a required parameter with no default must be supplied. */
    val defaultValue: PluginValue?,
    val doc: String,
)

/** One declared hook; diagnostics and stage gating only. */
data class PluginHook(
    val trigger: String,
    val stage: String,
    val priority: Int,
    val name: String,
)

/**
 * The probe's view of one plugin plus the host facts it was read against.
 *
 * [rejects] non-empty means the host would refuse the module at registration
 * (reserved capability/trigger/stage, hash or ABI mismatch): the UI greys the row
 * and the configuration is never written.
 */
data class PluginDescriptor(
    val id: String,
    val version: String,
    val abiVersion: Int,
    val size: Long,
    val sha256: String,
    val stages: Set<String>,
    val requiredCaps: Set<String>,
    val hooks: List<PluginHook>,
    val params: List<PluginParam>,
    val extract: List<PluginParam>,
    val rejects: List<String>,
    val hostAbiVersion: Int,
    /** Absolute `<GHOSTLOCK_HOME>/countermeasures`; null when the probe ran with
     * no home (the header writes "-"). */
    val countermeasuresRoot: String?,
    val hostStages: Set<String>,
    val hostCaps: Set<String>,
) {
    val usable: Boolean get() = rejects.isEmpty()

    val paramsByName: Map<String, PluginParam> get() = params.associateBy { it.name }
}

/**
 * Parser for the frozen probe TSV (contract-design §3.14.7.2).
 *
 * Header rows come first and are `key<TAB>value` (keys: host_abi,
 * countermeasures_root, host_stages, host_caps). Description rows follow; their
 * first column is the kind:
 *
 *   plugin<TAB>id<TAB>version<TAB>abi_version<TAB>size<TAB>sha256<TAB>stages<TAB>required_caps
 *   hook<TAB>id<TAB>trigger<TAB>stage<TAB>priority<TAB>name
 *   param<TAB>id<TAB>name<TAB>type<TAB>required<TAB>default<TAB>doc
 *   extract<TAB>id<TAB>name<TAB>type<TAB>required<TAB>default<TAB>doc
 *   reject<TAB>id<TAB>reason
 *
 * `required` is 0/1, an empty list/default is "-", a reject row may carry "-" as
 * the id when the path could not be parsed (contract line 830).
 *
 * Fail-closed: a malformed line, an unknown header key or column value, a header
 * row after the description block, a missing/duplicated plugin row or a row
 * naming another plugin throws with the offending line text.
 */
object PluginProbe {
    private val DESCRIPTION_KINDS = setOf("plugin", "hook", "param", "extract", "reject")
    private val HEADER_KEYS = setOf("host_abi", "countermeasures_root", "host_stages", "host_caps")
    private const val NONE = "-"

    /** Every contract violation is an IllegalArgumentException (fail closed). */
    private fun fail(line: String, reason: String): Nothing =
        throw IllegalArgumentException("plugin probe: " + reason + ": " + line)

    private fun columns(line: String, expected: Int): List<String> {
        val parts = line.split('\t')
        require(parts.size == expected) {
            "plugin probe line needs " + expected + " tab-separated columns: " + line
        }
        return parts
    }

    private fun text(value: String): String? = value.takeIf { it != NONE && it.isNotEmpty() }

    private fun list(value: String): Set<String> =
        text(value)?.split(',')?.map { it.trim() }?.filter { it.isNotEmpty() }?.toSet() ?: emptySet()

    private fun bit(value: String, line: String): Boolean = when (value) {
        "1" -> true
        "0" -> false
        else -> fail(line, "required must be 0 or 1")
    }

    private fun defaultValue(type: PluginParamType, raw: String, line: String): PluginValue? {
        val value = text(raw) ?: return null
        return when (type) {
            PluginParamType.UInt -> PluginValue.UInt(
                value.toULongOrNull() ?: fail(line, "uint default is not a number"),
            )

            PluginParamType.Int -> PluginValue.Int(
                value.toLongOrNull() ?: fail(line, "int default is not a number"),
            )

            PluginParamType.Bool -> PluginValue.Bool(bit(value, line))
            PluginParamType.Str -> PluginValue.Str(value)
        }
    }

    private fun param(parts: List<String>, line: String): PluginParam {
        val type = PluginParamType.fromText(parts[3])
            ?: fail(line, "unknown parameter type")
        val name = parts[2]
        if (name.isBlank()) fail(line, "empty parameter name")
        return PluginParam(
            name = name,
            type = type,
            required = bit(parts[4], line),
            defaultValue = defaultValue(type, parts[5], line),
            doc = text(parts[6]) ?: "",
        )
    }

    fun parse(text: String): PluginDescriptor {
        var id: String? = null
        var version = ""
        var abiVersion = 0
        var size = 0L
        var sha256 = ""
        var stages: Set<String> = emptySet()
        var requiredCaps: Set<String> = emptySet()
        var hostAbiVersion: Int? = null
        var hostStages: Set<String> = emptySet()
        var hostCaps: Set<String> = emptySet()
        var root: String? = null
        val headerKeys = mutableSetOf<String>()
        val hooks = mutableListOf<PluginHook>()
        val params = mutableListOf<PluginParam>()
        val extract = mutableListOf<PluginParam>()
        val rejects = mutableListOf<String>()
        var descriptionStarted = false

        for (raw in text.lineSequence()) {
            val line = raw.trimEnd('\r')
            if (line.isBlank()) continue
            val kind = line.substringBefore('\t')
            if (kind !in DESCRIPTION_KINDS) {
                require(!descriptionStarted) {
                    "plugin probe: header row after the description block: " + line
                }
                val parts = columns(line, 2)
                require(headerKeys.add(parts[0])) {
                    "plugin probe: duplicate header key: " + line
                }
                require(parts[0] in HEADER_KEYS) { "plugin probe: unknown header key: " + line }
                when (parts[0]) {
                    "host_abi" -> hostAbiVersion = parts[1].toIntOrNull()
                        ?: fail(line, "host_abi is not a number")

                    "countermeasures_root" -> root = text(parts[1])

                    "host_stages" -> hostStages = list(parts[1])
                    "host_caps" -> hostCaps = list(parts[1])
                }
                continue
            }
            descriptionStarted = true
            when (kind) {
                "plugin" -> {
                    val parts = columns(line, 8)
                    if (id != null) fail(line, "duplicate plugin row")
                    val pluginId = parts[1]
                    if (pluginId.isBlank() || pluginId == NONE) fail(line, "empty plugin id")
                    id = pluginId
                    version = text(parts[2]) ?: ""
                    abiVersion = parts[3].toIntOrNull() ?: fail(line, "abi_version is not a number")
                    size = parts[4].toLongOrNull() ?: fail(line, "size is not a number")
                    sha256 = text(parts[5]) ?: fail(line, "sha256 is missing")
                    require(Regex("[0-9a-f]{64}").matches(sha256)) {
                        "plugin probe: sha256 must be 64 lower-case hex chars: " + line
                    }
                    stages = list(parts[6])
                    requiredCaps = list(parts[7])
                }

                "hook" -> {
                    val parts = columns(line, 6)
                    ownerCheck(parts[1], id, line)
                    hooks += PluginHook(
                        trigger = text(parts[2]) ?: fail(line, "hook trigger is missing"),
                        stage = text(parts[3]) ?: fail(line, "hook stage is missing"),
                        priority = parts[4].toIntOrNull() ?: fail(line, "hook priority is not a number"),
                        name = text(parts[5]) ?: fail(line, "hook name is missing"),
                    )
                }

                "param" -> {
                    val parts = columns(line, 7)
                    ownerCheck(parts[1], id, line)
                    params += param(parts, line)
                }

                "extract" -> {
                    val parts = columns(line, 7)
                    ownerCheck(parts[1], id, line)
                    extract += param(parts, line)
                }

                "reject" -> {
                    val parts = columns(line, 3)
                    ownerCheck(parts[1], id, line)
                    rejects += text(parts[2]) ?: fail(line, "reject reason is missing")
                }
            }
        }

        val pluginId = id
            ?: throw IllegalArgumentException("plugin probe: no plugin row")
        require("countermeasures_root" in headerKeys) {
            "plugin probe: header has no countermeasures_root"
        }
        require(params.map { it.name }.toSet().size == params.size) {
            "plugin probe: duplicate parameter name for " + pluginId
        }
        require(extract.map { it.name }.toSet().size == extract.size) {
            "plugin probe: duplicate extract name for " + pluginId
        }
        return PluginDescriptor(
            id = pluginId,
            version = version,
            abiVersion = abiVersion,
            size = size,
            sha256 = sha256,
            stages = stages,
            requiredCaps = requiredCaps,
            hooks = hooks,
            params = params,
            extract = extract,
            rejects = rejects,
            hostAbiVersion = hostAbiVersion
                ?: throw IllegalArgumentException("plugin probe: header has no host_abi"),
            countermeasuresRoot = root,
            hostStages = hostStages,
            hostCaps = hostCaps,
        )
    }

    /**
     * Read-only helper for the failed-probe path: the reject reasons of a
     * non-zero exit, in output order. [parse] stays strict (a reject-only
     * output has no plugin row and is refused); this helper never throws, so the
     * import flow can map a rejection to a user-visible reason.
     */
    fun rejections(text: String): List<String> = text.lineSequence()
        .map { it.trimEnd('\r') }
        .filter { it.startsWith("reject\t") }
        .mapNotNull { line ->
            val parts = line.split('\t')
            if (parts.size != 3) null else text(parts[2])
        }
        .toList()

    /** A row names its plugin; "-" (unparseable path) and rows before the plugin
     * row are accepted, any other id is a contract violation. */
    private fun ownerCheck(owner: String?, id: String?, line: String) {
        if (owner == null || owner == NONE || id == null) return
        if (owner != id) fail(line, "row belongs to another plugin")
    }
}
