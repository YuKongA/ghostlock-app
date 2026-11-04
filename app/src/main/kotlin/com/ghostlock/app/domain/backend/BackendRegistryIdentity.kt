package com.ghostlock.app.domain.backend

/**
 * Deterministic identity of one process-local backend registry membership snapshot.
 *
 * @property version Stable 64-bit content version used for a cheap first comparison.
 * @property contentHash Full lowercase SHA-256 of the canonical membership encoding.
 * @note Both values describe membership only; current selection and readiness are intentionally excluded.
 */
data class BackendRegistryIdentity(
    val version: Long,
    val contentHash: String,
)
