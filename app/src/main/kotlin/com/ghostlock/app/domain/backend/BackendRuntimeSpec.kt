package com.ghostlock.app.domain.backend

/**
 * Android- and renderer-neutral projection of one registered backend.
 *
 * @property key Stable backend identity used for selection.
 * @property cveLabel User-visible CVE label.
 * @property displayName Fully localized selector label resolved by the outer adapter.
 * @property statusText Fully localized status text resolved by the outer adapter.
 * @property isReady Whether the backend's current readiness declaration is ready.
 * @property selectable Whether the captured registry allows this backend to be selected.
 */
data class BackendRuntimeSpec(
    val key: String,
    val cveLabel: String,
    val displayName: String,
    val statusText: String,
    val isReady: Boolean,
    val selectable: Boolean,
)
