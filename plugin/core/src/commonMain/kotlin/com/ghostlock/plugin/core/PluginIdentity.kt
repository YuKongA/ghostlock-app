package com.ghostlock.plugin.core

import kotlinx.serialization.Serializable

private val pluginIdentifierPattern = Regex("[a-z0-9][a-z0-9._-]{0,95}")
private val backendKeyPattern = Regex("[A-Za-z0-9][A-Za-z0-9._:-]{0,95}")
private val sha256Pattern = Regex("[0-9a-f]{64}")
private val javaBinaryNamePattern =
    Regex("[A-Za-z_$][A-Za-z0-9_$]*(\\.[A-Za-z_$][A-Za-z0-9_$]*)+")

data class PluginPackageIdentity(
    val publisherId: String,
    val packageId: String,
) {
    init {
        requirePluginIdentifier(publisherId, "publisherId")
        requirePluginIdentifier(packageId, "packageId")
    }
}

data class PluginComponentIdentity(
    val packageIdentity: PluginPackageIdentity,
    val componentId: String,
) {
    init {
        requirePluginIdentifier(componentId, "componentId")
    }
}

@Serializable
@JvmInline
value class PluginContentSha256(val value: String) {
    init {
        require(sha256Pattern.matches(value)) {
            "pluginContentSha256 must be 64 lowercase hexadecimal characters"
        }
    }

    override fun toString(): String = value
}

internal fun requirePluginIdentifier(value: String, field: String): String {
    require(pluginIdentifierPattern.matches(value)) {
        "$field must match [a-z0-9][a-z0-9._-]{0,95}"
    }
    return value
}

internal fun requireBackendKey(value: String): String {
    require(backendKeyPattern.matches(value)) {
        "backendKey must match [A-Za-z0-9][A-Za-z0-9._:-]{0,95}"
    }
    return value
}

internal fun requireSha256(value: String, field: String): String {
    require(sha256Pattern.matches(value)) {
        "$field must be 64 lowercase hexadecimal characters"
    }
    return value
}

internal fun requirePrintableUtf8(
    value: String,
    field: String,
    maximumBytes: Int,
    allowEmpty: Boolean = false,
): String {
    require(allowEmpty || value.isNotEmpty()) { "$field must not be empty" }
    require(value.encodeToByteArray().size <= maximumBytes) {
        "$field exceeds $maximumBytes UTF-8 bytes"
    }
    var index = 0
    while (index < value.length) {
        val character = value[index]
        require(!character.isISOControl() && character != '\u2028' && character != '\u2029') {
            "$field must be single-line printable text"
        }
        when {
            character.isHighSurrogate() -> {
                require(index + 1 < value.length && value[index + 1].isLowSurrogate()) {
                    "$field contains an unpaired surrogate"
                }
                index += 2
            }
            character.isLowSurrogate() -> throw IllegalArgumentException(
                "$field contains an unpaired surrogate",
            )
            else -> index++
        }
    }
    return value
}

internal fun requireEntryPath(value: String, field: String = "path"): String {
    requirePrintableUtf8(value, field, maximumBytes = 240)
    require(!value.startsWith('/')) { "$field must be relative" }
    require('\\' !in value) { "$field must use forward slashes" }
    val segments = value.split('/')
    require(segments.none { it.isEmpty() || it == "." || it == ".." }) {
        "$field contains an unsafe segment"
    }
    return value
}

internal fun requireJavaBinaryName(value: String): String {
    requirePrintableUtf8(value, "entryClass", maximumBytes = 240)
    require(javaBinaryNamePattern.matches(value)) { "entryClass is not a Java binary name" }
    return value
}

private fun Char.isHighSurrogate(): Boolean = code in 0xd800..0xdbff

private fun Char.isLowSurrogate(): Boolean = code in 0xdc00..0xdfff
