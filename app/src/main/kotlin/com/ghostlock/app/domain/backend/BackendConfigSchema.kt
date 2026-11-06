package com.ghostlock.app.domain.backend

/** Stable scalar kinds accepted by a version-three backend profile field. */
enum class BackendConfigValueKind {
    BOOLEAN,
    INT,
    LONG,
    STRING,
}

/**
 * One backend-owned profile field declaration without storage or Android behavior.
 *
 * @property name Stable snake-case key within the backend profile section.
 * @property kind Canonical scalar kind.
 * @property required Whether a frozen run must contain the field.
 * @property description Human-readable semantic description; it is not a persistence key.
 * @property defaultValue Canonical textual default, or null when absence is meaningful.
 */
data class BackendConfigField(
    val name: String,
    val kind: BackendConfigValueKind,
    val required: Boolean,
    val description: String,
    val defaultValue: String? = null,
) {
    init {
        require(name.matches(NAME_PATTERN)) { "backend config field must be snake_case: $name" }
        require(name.length <= MAX_NAME_LENGTH) { "backend config field name is too long: $name" }
        require(description.length <= MAX_DESCRIPTION_LENGTH) {
            "backend config field description is too long: $name"
        }
        require(defaultValue == null || defaultValue.toByteArray(Charsets.UTF_8).size <= MAX_DEFAULT_BYTES) {
            "backend config field default is too large: $name"
        }
    }

    private companion object {
        val NAME_PATTERN: Regex = Regex("[a-z][a-z0-9_]*")
        const val MAX_NAME_LENGTH: Int = 64
        const val MAX_DESCRIPTION_LENGTH: Int = 4 * 1024
        const val MAX_DEFAULT_BYTES: Int = 8 * 1024
    }
}

/**
 * Immutable backend configuration schema stored only through the GLK profile authority.
 *
 * @property fields Declared fields in stable presentation and serialization order.
 * @note This type intentionally has no Context, SharedPreferences, save callback, or restore callback.
 */
data class BackendConfigSchema(
    val fields: List<BackendConfigField> = emptyList(),
) {
    init {
        require(fields.size <= MAX_FIELDS) { "too many backend config fields" }
        require(fields.map(BackendConfigField::name).toSet().size == fields.size) {
            "duplicate backend config field"
        }
    }

    /**
     * Resolves a declared field.
     *
     * @param name Stable field name.
     * @return Matching declaration, or null when the caller must reject the field.
     */
    fun field(name: String): BackendConfigField? = fields.firstOrNull { candidate -> candidate.name == name }

    companion object {
        private const val MAX_FIELDS: Int = 64

        /** Empty schema for a backend with no interactive configuration. */
        val EMPTY: BackendConfigSchema = BackendConfigSchema()
    }
}
