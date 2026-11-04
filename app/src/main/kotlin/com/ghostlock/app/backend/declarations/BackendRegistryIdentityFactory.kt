package com.ghostlock.app.backend.declarations

import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import java.security.MessageDigest

/** Builds the cross-process identity for one explicitly captured backend membership list. */
object BackendRegistryIdentityFactory {
    /**
     * Canonically encodes registry order, stable key, selectability, and Flow symbol.
     *
     * @param registryEntries Immutable list captured once by the calling composition root.
     * @return Stable version plus full content hash for cross-process comparison.
     */
    fun capture(registryEntries: List<BackendSpec>): BackendRegistryIdentity {
        val canonical = buildString {
            append(CANONICAL_FORMAT)
            registryEntries.forEachIndexed { index, spec ->
                appendField(index.toString())
                appendField(spec.key)
                appendField(if (spec.selectable) "1" else "0")
                appendField(spec.flowSymbol?.canonical.orEmpty())
                append('\n')
            }
        }.toByteArray(Charsets.UTF_8)
        val version = canonical.fold(FNV_OFFSET_BASIS) { hash, byte ->
            (hash xor byte.toLong().and(0xffL)) * FNV_PRIME
        }
        val digest = MessageDigest.getInstance("SHA-256").digest(canonical)
        return BackendRegistryIdentity(
            version = version,
            contentHash = digest.joinToString(separator = "") { value ->
                value.toUByte().toString(radix = 16).padStart(2, '0')
            },
        )
    }

    /** Appends one length-prefixed field so dynamic keys cannot create delimiter collisions. */
    private fun StringBuilder.appendField(value: String) {
        append(value.length)
        append(':')
        append(value)
        append('|')
    }

    private const val CANONICAL_FORMAT = "ghostlock-backend-registry-v1\n"
    private const val FNV_OFFSET_BASIS = -3750763034362895579L
    private const val FNV_PRIME = 1099511628211L
}
