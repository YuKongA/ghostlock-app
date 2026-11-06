package com.ghostlock.app.backend.backends.lua

import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.domain.run.StageResult

/** Approved coarse operation callable from the Lua frontend. */
internal fun interface LuaOperation {
    /**
     * Executes one typed call.
     *
     * @param input Uniform backend run input.
     * @param arguments Bounded validated scalar arguments.
     * @return Typed operation stage fact.
     */
    suspend fun execute(input: BackendFlowInput, arguments: List<String>): StageResult
}

/** One immutable operation registration. */
internal data class LuaOperationRegistration(
    val key: String,
    val maximumArguments: Int,
    val operation: LuaOperation,
) {
    init {
        require(key.matches(KEY_PATTERN)) { "invalid Lua operation key: $key" }
        require(maximumArguments in 0..MAX_ARGUMENTS) { "invalid Lua operation argument limit" }
    }

    private companion object {
        val KEY_PATTERN: Regex = Regex("[a-z][a-z0-9._-]{0,63}")
        const val MAX_ARGUMENTS: Int = 32
    }
}

/**
 * Immutable typed operation allowlist for the Lua frontend.
 *
 * @note No reflection, constructor bridge, arbitrary native symbol, JNA pointer, or dynamic registration exists.
 */
internal class LuaOperationCatalog(
    registrations: List<LuaOperationRegistration>,
) {
    private val byKey: Map<String, LuaOperationRegistration>

    init {
        require(registrations.map(LuaOperationRegistration::key).toSet().size == registrations.size) {
            "duplicate Lua operation key"
        }
        byKey = registrations.associateBy(LuaOperationRegistration::key)
    }

    /** Resolves an exact approved operation, or null when the frontend must reject the call. */
    fun resolve(key: String): LuaOperationRegistration? = byKey[key]

    companion object {
        /** Empty catalog used until a real backend-private operation is approved. */
        val EMPTY: LuaOperationCatalog = LuaOperationCatalog(emptyList())
    }
}
