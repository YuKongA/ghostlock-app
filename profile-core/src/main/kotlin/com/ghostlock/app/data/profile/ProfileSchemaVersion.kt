package com.ghostlock.app.data.profile

/**
 * Canonical configuration schema version.
 *
 * The HOCON asset files, the in-memory model and the GLKv3 wire all carry the
 * **same** number (3) so the three layers cannot be confused with each other.
 * Do not introduce a second number: bump this one and the wire kSchemaVersion
 * together, or not at all.
 */
const val GHOSTLOCK_PROFILE_SCHEMA_VERSION = 3

/**
 * The last legacy version (written by app 1.2 and earlier).
 *
 * Accepted on read and normalized to [GHOSTLOCK_PROFILE_SCHEMA_VERSION] by
 * `LegacyProfileConverter`; it is the single migration point. Any other value
 * is rejected with the version that was actually seen.
 */
const val GHOSTLOCK_PROFILE_LEGACY_SCHEMA_VERSION = 1
