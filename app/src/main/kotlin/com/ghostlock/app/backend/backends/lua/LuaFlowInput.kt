package com.ghostlock.app.backend.backends.lua

/**
 * One typed Lua operation invocation after frontend parsing and validation.
 *
 * @property operation Stable catalog key.
 * @property arguments Bounded scalar arguments; no Java object, class name, symbol, pointer, or path lookup.
 */
data class LuaOperationCall(
    val operation: String,
    val arguments: List<String>,
)

/**
 * Immutable operation plan produced by a Lua frontend.
 *
 * @property scriptArtifactId Logical identity of the source script used for provenance.
 * @property calls Ordered typed operation calls.
 */
data class LuaFlowInput(
    val scriptArtifactId: String,
    val calls: List<LuaOperationCall>,
)
