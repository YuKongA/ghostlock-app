package com.ghostlock.app.backend.backends.lua

/**
 * Typed request prepared by the Lua frontend after parsing and catalog validation.
 *
 * @property flowInput Immutable approved operation plan.
 */
data class FlowLuaRequest(
    val flowInput: LuaFlowInput,
)
