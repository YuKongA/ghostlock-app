package com.ghostlock.app.data.profile

import com.ghostlock.app.data.component.BackendKind

/**
 * Channel-B stdin composition (plan section 6.3): the app-call stdin stream is
 * a length-prefixed GLKv3 document and, for the cve_2026_43284 backend only, a
 * second length-prefixed session-secret frame. Every other backend -- 43499
 * included -- sends the GLKv3 document alone, so their stdin/status-ACK
 * behavior is byte-for-byte unchanged.
 *
 * The decision is made from the *selected* backend, never inferred from the
 * document: a caller that owns session secrets for a non-43284 backend still
 * sends only the GLKv3 frame. A 43284 selection without secrets fails closed
 * (the helpers return null) instead of sending a stream native will reject.
 */
object ChannelBStdin {
    /** True only for the backend that consumes the channel-B session frame. */
    fun requiresSessionFrame(backend: BackendKind?): Boolean =
        backend == BackendKind.Cve2026_43284

    /**
     * The complete app-call stdin stream for [backend]: length-prefixed GLKv3
     * [document], followed by the framed session secrets when the backend
     * requires them. Returns null when a 43284 selection carries no secrets so
     * the caller fails closed; [secrets] is ignored for every other backend.
     */
    fun appCall(
        backend: BackendKind?,
        document: ByteArray,
        secrets: IpsecSessionSecrets?,
    ): ByteArray? = if (requiresSessionFrame(backend)) {
        secrets?.let { SessionSecretFrame.appCallStdin(document, it) }
    } else {
        SessionSecretFrame.lengthPrefix(document.size) + document
    }

    /**
     * The framed (4-byte length prefixed) session frame for [backend], or null
     * when the backend must not receive one. Never encodes secrets for a
     * non-43284 backend.
     */
    fun sessionFrame(
        backend: BackendKind?,
        secrets: IpsecSessionSecrets?,
    ): ByteArray? = if (requiresSessionFrame(backend)) {
        secrets?.let(SessionSecretFrame::encodeFramed)
    } else {
        null
    }
}
