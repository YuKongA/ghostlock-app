package com.ghostlock.app.backend.declarations

import android.content.Context

/** Supported persisted value kinds for a backend-owned configuration schema. */
enum class BackendConfigType {
    BOOLEAN,
    INT,
    LONG,
    STRING,
}

/** One validated configuration key without a persistence namespace. */
data class BackendConfigKey(
    val name: String,
    val type: BackendConfigType,
    val defaultValue: Any,
) {
    init {
        require(name.matches(NAME_REGEX)) { "backend config key must be snake_case: $name" }
        require(!name.contains(':')) { "backend config key must not carry a namespace: $name" }
        require(name.length <= MAX_NAME_LENGTH) { "backend config key too long: $name" }
        require(defaultValueMatches(type, defaultValue)) {
            "backend config default type mismatch: $name"
        }
        if (defaultValue is String) {
            require(defaultValue.length <= MAX_STRING_LENGTH) {
                "backend config string default too large: $name"
            }
        }
    }

    private companion object {
        val NAME_REGEX = Regex("[a-z][a-z0-9_]*")
        const val MAX_NAME_LENGTH = 64
        const val MAX_STRING_LENGTH = 8 * 1024

        fun defaultValueMatches(type: BackendConfigType, value: Any): Boolean = when (type) {
            BackendConfigType.BOOLEAN -> value is Boolean
            BackendConfigType.INT -> value is Int
            BackendConfigType.LONG -> value is Long
            BackendConfigType.STRING -> value is String
        }
    }
}

/** Complete configuration schema declared by one backend plugin. */
data class BackendConfigSchema(
    val keys: List<BackendConfigKey> = emptyList(),
) {
    init {
        require(keys.size <= MAX_KEYS) { "too many backend config keys" }
        require(keys.map { it.name }.toSet().size == keys.size) {
            "duplicate backend config key"
        }
    }

    /** @return Declared key named [name], or null when access must be rejected. */
    fun key(name: String): BackendConfigKey? = keys.firstOrNull { it.name == name }

    companion object {
        private const val MAX_KEYS = 64
        val EMPTY: BackendConfigSchema = BackendConfigSchema()
    }
}

/** Fail-closed schema or persistence contract violation. */
class BackendConfigViolation(message: String) : IllegalArgumentException(message)

/**
 * Typed configuration store contract supplied to backend plugins by an outer data adapter.
 *
 * Android context remains explicit because existing backend restore hooks update legacy stores; the
 * persistence mechanism and SharedPreferences implementation are not exposed through this contract.
 */
interface BackendConfigStore {
    val context: Context
    val cve: String
    val schema: BackendConfigSchema

    fun contains(name: String): Boolean
    fun getBoolean(name: String): Boolean
    fun getInt(name: String): Int
    fun getLong(name: String): Long
    fun getString(name: String): String
    fun putBoolean(name: String, value: Boolean)
    fun putInt(name: String, value: Int)
    fun putLong(name: String, value: Long)
    fun putString(name: String, value: String)
    fun clear()
    fun fullKey(name: String): String
}
