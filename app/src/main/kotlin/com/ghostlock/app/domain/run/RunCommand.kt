package com.ghostlock.app.domain.run

/**
 * Requests one execution of the currently selected backend by stable identity.
 *
 * @property backendKey Stable key captured from the current run-session projection.
 * @note The outer adapter re-resolves this key immediately before dispatch and fails closed on drift.
 */
data class RunCommand(
    val backendKey: String,
)
