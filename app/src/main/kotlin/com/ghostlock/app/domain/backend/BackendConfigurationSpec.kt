package com.ghostlock.app.domain.backend

/** Single persistence authority accepted for backend configuration. */
enum class BackendConfigurationAuthority {
    /** Configuration is part of the canonical GLK profile version-three document. */
    GLK_PROFILE_V3,
}

/**
 * Immutable configuration capability carried by [BackendSpec].
 *
 * @property authority Canonical persistence authority.
 * @property schema Neutral field schema.
 * @property documentMimeTypes Optional accepted import document types.
 * @note Mutation and Android document I/O belong to outer adapters, not this declaration.
 */
data class BackendConfigurationSpec(
    val authority: BackendConfigurationAuthority = BackendConfigurationAuthority.GLK_PROFILE_V3,
    val schema: BackendConfigSchema = BackendConfigSchema.EMPTY,
    val documentMimeTypes: List<String> = emptyList(),
) {
    init {
        require(documentMimeTypes.size <= MAX_MIME_TYPES) { "too many backend document MIME types" }
        require(documentMimeTypes.all { value -> value.isNotBlank() && value.length <= MAX_MIME_LENGTH }) {
            "invalid backend document MIME type"
        }
        require(documentMimeTypes.toSet().size == documentMimeTypes.size) {
            "duplicate backend document MIME type"
        }
    }

    private companion object {
        const val MAX_MIME_TYPES: Int = 16
        const val MAX_MIME_LENGTH: Int = 128
    }
}
