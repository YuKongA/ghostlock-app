package com.ghostlock.app.core

/** A session request owns a defensive copy of its profile snapshot. */
class AccessSessionRequest(val backendId: String, profileFrame: ByteArray) {
    private var frame: ByteArray? = profileFrame.copyOf()

    init { require(backendId.isNotBlank()) }

    /** Consume once for backend initialization, clearing the retained snapshot. */
    @Synchronized
    fun consumeProfileFrame(): ByteArray {
        val held = checkNotNull(frame) { "profile frame already consumed" }
        val outbound = held.copyOf()
        held.fill(0)
        frame = null
        return outbound
    }
}

/**
 * Establishes a session and obtains native-authoritative capability descriptors.
 *
 * The factory must not advertise a capability before its backend has reached READY.
 */
fun interface NativeSessionFactory {
    /** Open one native session; failures are typed and must not leave a published partial session. */
    fun open(request: AccessSessionRequest): AccessResult<NativeSession>
}
