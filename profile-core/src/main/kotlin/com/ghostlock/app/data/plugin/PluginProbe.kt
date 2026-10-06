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
    /** uint32 in the ABI: parsed unsigned, never through a signed domain. */
    val priority: UInt,
    val name: String,
)

/**
 * One `spec` row (frozen 16-column form, plugin-extract-spec-design §10): the
 * value the plugin needs from the extractor, HOW it may be obtained, and the
 * disassembly parameters when the method is `disasm`.
 *
 * Every `-` column stays NULL here: the DEFAULTS are native's authority (§10.1)
 * and are deliberately NOT replicated in Kotlin — the page says "default"
 * without inventing which default it is. [methods] is empty for `-`, i.e. the
 * default ladder, and non-empty means the exact try order the plugin declared.
 */
data class PluginExtractSpec(
    val name: String,
    val type: PluginParamType,
    val required: Boolean,
    /** Ordered try list; empty = the native default ladder. */
    val methods: List<String>,
    val anchor: String?,
    val scope: String?,
    val pattern: String?,
    val hit: Int?,
    val capture: String?,
    val width: String?,
    val signed: Boolean?,
    val base: String?,
    val maxScan: Long?,
    val doc: String,
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
    /** uint32 in the ABI. */
    val abiVersion: UInt,
    /** uint32 in the ABI (decimal, unsigned). */
    val size: UInt,
    val sha256: String,
    val stages: Set<String>,
    val requiredCaps: Set<String>,
    val hooks: List<PluginHook>,
    val params: List<PluginParam>,
    val extract: List<PluginParam>,
    /** P2 spec rows (frozen 16-column form); empty for every legacy module. */
    val specs: List<PluginExtractSpec> = emptyList(),
    val rejects: List<String>,
    /** uint32 in the ABI. */
    val hostAbiVersion: UInt,
    /** Absolute `<GHOSTLOCK_HOME>/countermeasures`; null when the probe ran with
     * no home (the header writes "-"). */
    val countermeasuresRoot: String?,
    val hostStages: Set<String>,
    val hostCaps: Set<String>,
    /**
     * Per-backend stage availability as the probe reports it (frozen P1
     * revision): backend short token -> available stage tokens, e.g.
     * `{"43499": ["pre_terminal"], "43284": ["post_terminal"]}`. The MATRIX is
     * native data and is never re-declared in Kotlin; an empty map means the
     * probe predates the header, so no stage is greyed.
     */
    val stageAvailability: Map<String, List<String>> = emptyMap(),
) {
    val usable: Boolean get() = rejects.isEmpty()

    val paramsByName: Map<String, PluginParam> get() = params.associateBy { it.name }

    /** Extract keys the module declares (P2 extractor projection). */
    val extractByName: Map<String, PluginParam> get() = extract.associateBy { it.name }
}

/**
 * Parser for the frozen probe TSV (contract-design §3.14.7.2).
 *
 * Header rows come first and are `key<TAB>value` (keys: host_abi,
 * countermeasures_root, host_stages, host_caps, stage_availability).
 * `stage_availability` carries the native per-backend matrix as
 * `<backend-short>:<stage>[,<stage>]*` groups separated by `;`; it is
 * OPTIONAL so a capture taken before that revision still parses (absence means
 * "unknown", never "nothing is available"). Description rows follow; their
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
    private val DESCRIPTION_KINDS = setOf("plugin", "hook", "param", "extract", "reject", "spec")

    /** Frozen value domains of the `spec` row (native design §10.1). */
    private val SPEC_METHODS = setOf("profile", "btf", "kallsyms", "disasm")
    private val SPEC_SCOPES = setOf("anchor", "text")
    private val SPEC_BASES = setOf("image", "anchor", "raw")
    private val SPEC_WIDTHS = setOf("1", "2", "4", "8", "auto")
    /** Frozen r5 set (plugin-extract-spec-design §11.2): symbol_va is gone. */
    private val SPEC_CAPTURE_KINDS = setOf("pc", "imm", "disp", "reg", "pcoff")
    private const val SPEC_COLUMNS = 16
    private const val SPEC_MAX_HIT = 64
    private const val SPEC_MAX_SCAN = 1024L * 1024L
    private val HEADER_KEYS =
        setOf("host_abi", "countermeasures_root", "host_stages", "host_caps", "stage_availability")
    private const val NONE = "-"

    /** `contract::kMaxHookNameLength`: names are bounded in BYTES, not chars. */
    private const val MAX_NAME_BYTES = 64

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

    /**
     * A required NAME column, with the loader's own bounds (audit D7): a name
     * that registration would refuse must not be published as valid, so the
     * consumer applies the same rules — non-empty, at most
     * [MAX_NAME_BYTES] UTF-8 bytes, and no control character (0x00-0x1F, 0x7F).
     */
    private fun name(value: String?, line: String, label: String): String {
        val text = value ?: fail(line, label + " name is missing")
        if (text.isEmpty() || text == NONE) fail(line, label + " name is missing")
        if (text.toByteArray(Charsets.UTF_8).size > MAX_NAME_BYTES) {
            fail(line, label + " name is longer than " + MAX_NAME_BYTES + " bytes")
        }
        if (text.any { it.code < 0x20 || it.code == 0x7F }) {
            fail(line, label + " name has a control character")
        }
        return text
    }

    private fun list(value: String): Set<String> =
        text(value)?.split(',')?.map { it.trim() }?.filter { it.isNotEmpty() }?.toSet() ?: emptySet()

    /**
     * Parses `<backend-short>:<stage>[,<stage>]*` groups separated by `;`.
     *
     * Fail-closed on shape only (the token VOCABULARY belongs to native): a
     * missing separator or colon, an empty backend or stage, a duplicated
     * backend group or a repeated stage inside one group throws with the line.
     * `-` means "the probe reports nothing", which greys nothing.
     */
    private fun availability(value: String, line: String): Map<String, List<String>> {
        text(value) ?: return emptyMap()
        val out = linkedMapOf<String, List<String>>()
        for (group in value.split(';')) {
            val separator = group.indexOf(':')
            if (separator <= 0) fail(line, "stage_availability needs <backend>:<stages>")
            val backend = group.substring(0, separator).trim()
            if (backend.isEmpty()) fail(line, "stage_availability has an empty backend")
            val stages = group.substring(separator + 1)
                .split(',')
                .map { it.trim() }
                .filter { it.isNotEmpty() }
            if (stages.isEmpty()) fail(line, "stage_availability has an empty stage list")
            require(stages.size == stages.distinct().size) {
                "plugin probe: stage_availability repeats a stage: " + line
            }
            require(out.put(backend, stages) == null) {
                "plugin probe: stage_availability repeats a backend: " + line
            }
        }
        return out
    }

    /* ---- spec row (frozen 16-column form) ---- */

    private fun spec(parts: List<String>, line: String): PluginExtractSpec {
        val specName = name(parts[2], line, "extract spec")
        val type = PluginParamType.fromText(parts[3]) ?: fail(line, "unknown extract spec type")
        val required = bit(parts[4], line)
        val methods = specMethods(parts[5], line)
        val anchor = specAnchor(parts[6], line)
        /* A disasm method without an anchor cannot be attempted at all: that is
         * a malformed declaration, never a silently skipped one. */
        if ("disasm" in methods && anchor == null) {
            fail(line, "a disasm extract spec needs an anchor")
        }
        val width = specToken(parts[11], SPEC_WIDTHS, line, "width")
        val signed = when (val raw = text(parts[12])) {
            null -> null
            "0" -> false
            "1" -> true
            else -> fail(line, "signed must be 0 or 1")
        }
        /* Frozen r5 cross-column rules: the declared type and signedness must
         * agree, and an inferred width cannot claim a sign. */
        if (signed != null) {
            if (type == PluginParamType.Int && !signed) fail(line, "int with signed=0 is refused")
            if (type == PluginParamType.UInt && signed) fail(line, "uint with signed=1 is refused")
            if (width == "auto" && signed) fail(line, "width=auto with signed=1 is refused")
        }
        return PluginExtractSpec(
            name = specName,
            type = type,
            required = required,
            methods = methods,
            anchor = anchor,
            scope = specToken(parts[7], SPEC_SCOPES, line, "scope"),
            pattern = specPattern(parts[8], line),
            hit = specHit(parts[9], line),
            capture = specCapture(parts[10], line),
            width = width,
            signed = signed,
            base = specToken(parts[13], SPEC_BASES, line, "base"),
            maxScan = specMaxScan(parts[14], line),
            doc = text(parts[15]) ?: "",
        )
    }

    /** Ordered try list; `-` = the native default ladder (empty list here). */
    private fun specMethods(raw: String, line: String): List<String> {
        val value = text(raw) ?: return emptyList()
        val methods = value.split(',').map { it.trim() }
        if (methods.isEmpty() || methods.any { it.isEmpty() }) {
            fail(line, "methods has an empty member")
        }
        if (methods.size != methods.distinct().size) fail(line, "methods repeats a member")
        for (method in methods) {
            if (method !in SPEC_METHODS) fail(line, "unknown extract method: " + method)
        }
        return methods
    }

    /** `sym:<name>` | `path:<profile-path>` | `pc:<hex>`; `-` = absent. */
    private fun specAnchor(raw: String, line: String): String? {
        val value = text(raw) ?: return null
        val separator = value.indexOf(':')
        if (separator <= 0) fail(line, "anchor needs <kind>:<value>")
        val kind = value.substring(0, separator)
        val rest = value.substring(separator + 1)
        if (kind !in setOf("sym", "path", "pc")) fail(line, "unknown anchor kind: " + kind)
        if (rest.isEmpty()) fail(line, "anchor has an empty value")
        if (kind == "pc" && rest.any { it !in "0123456789abcdefABCDEF" }) {
            fail(line, "pc anchor is not hex")
        }
        return value
    }

    /** `bytes:<hex with ?? wildcards>` | `insn:<1..3>;<insn>`; `-` = none. */
    private fun specPattern(raw: String, line: String): String? {
        val value = text(raw) ?: return null
        return when {
            value.startsWith("bytes:") -> {
                val body = value.removePrefix("bytes:").removePrefix("0x").removePrefix("0X")
                if (body.length < 2 || body.length % 2 != 0) fail(line, "byte pattern is not byte aligned")
                if (body.any { it !in "0123456789abcdefABCDEF?" }) {
                    fail(line, "byte pattern has a non-hex character")
                }
                /* Frozen r5: `??` is exactly one byte and the decoded length is a
                 * whole number of instructions (4-byte multiples). */
                val bytes = body.length / 2
                if (bytes % 2 != 0) fail(line, "byte pattern is not a multiple of 4 bytes")
                value
            }

            value.startsWith("insn:") -> {
                val body = value.removePrefix("insn:")
                val insns = body.split(';')
                if (insns.isEmpty() || insns.size > 3 || insns.any { it.isBlank() }) {
                    fail(line, "insn pattern needs 1..3 instructions")
                }
                value
            }

            else -> fail(line, "pattern needs bytes: or insn:")
        }
    }

    /** Decimal 1..64; `-` = absent (the default belongs to native). */
    private fun specHit(raw: String, line: String): Int? {
        val value = text(raw) ?: return null
        val hit = value.toIntOrNull() ?: fail(line, "hit is not a number")
        if (hit < 1 || hit > SPEC_MAX_HIT) fail(line, "hit is out of 1.." + SPEC_MAX_HIT)
        return hit
    }

    /** `[<insn_index>.]<kind>:<operand_index>`; `-` = absent (catch the hit). */
    private fun specCapture(raw: String, line: String): String? {
        val value = text(raw) ?: return null
        val head = value.substringBefore('.', "")
        val rest = if (value.contains('.')) value.substringAfter('.') else value
        if (head.isNotEmpty() && head.toIntOrNull() == null) {
            fail(line, "capture instruction index is not a number")
        }
        val separator = rest.indexOf(':')
        if (separator <= 0) fail(line, "capture needs <kind>:<operand_index>")
        val kind = rest.substring(0, separator)
        val operand = rest.substring(separator + 1)
        if (kind !in SPEC_CAPTURE_KINDS) fail(line, "unknown capture kind: " + kind)
        if (operand.toIntOrNull() == null || operand.toInt() < 0) {
            fail(line, "capture operand index is not a number")
        }
        return value
    }

    /** `0x…` or decimal, 1..1 MiB; `-` = absent (native default). */
    private fun specMaxScan(raw: String, line: String): Long? {
        val value = text(raw) ?: return null
        val parsed = if (value.startsWith("0x") || value.startsWith("0X")) {
            value.substring(2).toLongOrNull(16)
        } else {
            value.toLongOrNull()
        } ?: fail(line, "max_scan is not a number")
        if (parsed < 1L || parsed > SPEC_MAX_SCAN) fail(line, "max_scan is out of 1..1MiB")
        return parsed
    }

    private fun specToken(raw: String, allowed: Set<String>, line: String, label: String): String? {
        val value = text(raw) ?: return null
        if (value !in allowed) fail(line, "unknown " + label + ": " + value)
        return value
    }

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
        /* A required column: `-` means the producer had nothing to write, so it
         * is MISSING, never a parameter literally named "-"; and the name must
         * satisfy the loader's bounds. */
        val paramName = name(parts[2], line, "parameter")
        return PluginParam(
            name = paramName,
            type = type,
            required = bit(parts[4], line),
            defaultValue = defaultValue(type, parts[5], line),
            doc = text(parts[6]) ?: "",
        )
    }

    fun parse(text: String): PluginDescriptor {
        var id: String? = null
        var version = ""
        var abiVersion = 0u
        var size = 0u
        var sha256 = ""
        var stages: Set<String> = emptySet()
        var requiredCaps: Set<String> = emptySet()
        var hostAbiVersion: UInt? = null
        var hostStages: Set<String> = emptySet()
        var hostCaps: Set<String> = emptySet()
        var stageAvailability: Map<String, List<String>> = emptyMap()
        var root: String? = null
        val headerKeys = mutableSetOf<String>()
        val hooks = mutableListOf<PluginHook>()
        val params = mutableListOf<PluginParam>()
        val extract = mutableListOf<PluginParam>()
        val specs = mutableListOf<PluginExtractSpec>()
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
                    "host_abi" -> hostAbiVersion = parts[1].toUIntOrNull()
                        ?: fail(line, "host_abi is not an unsigned 32-bit number")

                    "countermeasures_root" -> root = text(parts[1])

                    "host_stages" -> hostStages = list(parts[1])
                    "host_caps" -> hostCaps = list(parts[1])
                    "stage_availability" -> stageAvailability = availability(parts[1], line)
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
                    abiVersion = parts[3].toUIntOrNull()
                        ?: fail(line, "abi_version is not an unsigned 32-bit number")
                    size = parts[4].toUIntOrNull()
                        ?: fail(line, "size is not an unsigned 32-bit number")
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
                        priority = parts[4].toUIntOrNull()
                            ?: fail(line, "hook priority is not an unsigned 32-bit number"),
                        name = name(parts[5], line, "hook"),
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

                /* Frozen 16-column extractor declaration (P2 spec). */
                "spec" -> {
                    val parts = columns(line, SPEC_COLUMNS)
                    ownerCheck(parts[1], id, line)
                    specs += spec(parts, line)
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
        require(specs.map { it.name }.toSet().size == specs.size) {
            "plugin probe: duplicate extract spec name for " + pluginId
        }
        /* Both forms write plugin.<id>.extract.<name>, so the namespace is shared
         * and a collision would make the two declarations fight over one value. */
        val collision = specs.map { it.name }.toSet() intersect extract.map { it.name }.toSet()
        require(collision.isEmpty()) {
            "plugin probe: extract spec collides with an extract row for " + pluginId +
                ": " + collision.sorted().joinToString(",")
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
            specs = specs,
            rejects = rejects,
            hostAbiVersion = hostAbiVersion
                ?: throw IllegalArgumentException("plugin probe: header has no host_abi"),
            countermeasuresRoot = root,
            hostStages = hostStages,
            hostCaps = hostCaps,
            stageAvailability = stageAvailability,
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
