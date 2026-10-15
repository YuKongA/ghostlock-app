package com.ghostlock.app.data.nativeaccess

/**
 * A buffer that may hold credentials (the 43284 session frame is the one that does).
 *
 * Rules it exists to enforce: it can be handed out exactly once, it never appears in a log or a
 * toString, and the copy it holds is zeroed as soon as it is taken. The JVM cannot promise the
 * GC has no older copy -- that is stated here rather than pretended away -- but the array this
 * object owns is gone the moment it is consumed.
 */
class SensitiveFrame(secret: ByteArray) {
    private var held: ByteArray? = secret.copyOf()

    init {
        /* The caller's array is theirs; it is cleared here because they asked this object to be
         *  the owner from now on, and a second live copy is the thing this class prevents. */
        secret.fill(0)
    }

    /** @return the bytes, once. The second call is an error, not an empty buffer. */
    @Synchronized
    fun consume(): ByteArray {
        val current = checkNotNull(held) { "sensitive frame already consumed" }
        val outbound = current.copyOf()
        current.fill(0)
        held = null
        return outbound
    }

    @Synchronized
    fun consumed(): Boolean = held == null

    @Synchronized
    override fun toString(): String = "SensitiveFrame(redacted, size=" + (held?.size ?: 0) + ")"
}
