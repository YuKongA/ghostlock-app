package com.ghostlock.app.domain.backend

/** Canonical value class of a backend parameter or kernel-layout requirement. */
enum class BackendParameterKind {
    U64,
    ADDRESS,
    OFFSET,
    SIZE,
    MASK,
    CPU,
    STRING,
    TOKEN_LIST,
}

/** Sharing scope of one parameter declaration. */
enum class BackendParameterScope {
    COMMON_KERNEL_ABI,
    BACKEND_PROFILE,
    BACKEND_TUNING,
}

/** Canonical provider for a parameter value. */
enum class BackendParameterSource {
    PROFILE,
    COMPILE_CONSTANT,
    RUNTIME_DISCOVERED,
}

/**
 * One neutral parameter requirement.
 *
 * @property name Stable snake-case key.
 * @property kind Value class.
 * @property scope Sharing scope.
 * @property source Canonical provider.
 * @property required Whether freezing a production run requires a value.
 * @property description Semantic explanation without target-specific values.
 */
data class BackendParameterRequirement(
    val name: String,
    val kind: BackendParameterKind,
    val scope: BackendParameterScope,
    val source: BackendParameterSource,
    val required: Boolean = true,
    val description: String = "",
) {
    init {
        require(name.matches(NAME_PATTERN)) { "backend parameter must be snake_case: $name" }
        require(name.length <= MAX_NAME_LENGTH) { "backend parameter name is too long: $name" }
    }

    private companion object {
        val NAME_PATTERN: Regex = Regex("[a-z][a-z0-9_]*")
        const val MAX_NAME_LENGTH: Int = 64
    }
}

/** Immutable parameter requirements declared by one backend. */
data class BackendParameterRequirements(
    val parameters: List<BackendParameterRequirement> = emptyList(),
) {
    init {
        require(parameters.size <= MAX_PARAMETERS) { "too many backend parameters" }
        require(parameters.map(BackendParameterRequirement::name).toSet().size == parameters.size) {
            "duplicate backend parameter"
        }
    }

    /**
     * Resolves a declared parameter.
     *
     * @param name Stable parameter name.
     * @return Matching declaration, or null for an undeclared name.
     */
    fun parameter(name: String): BackendParameterRequirement? =
        parameters.firstOrNull { candidate -> candidate.name == name }

    companion object {
        private const val MAX_PARAMETERS: Int = 256

        /** Empty parameter set. */
        val EMPTY: BackendParameterRequirements = BackendParameterRequirements()
    }
}
