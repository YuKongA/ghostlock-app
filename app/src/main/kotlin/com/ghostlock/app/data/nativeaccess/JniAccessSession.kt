package com.ghostlock.app.data.nativeaccess

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.AccessSession
import com.ghostlock.app.core.AccessSessionFactory
import com.ghostlock.app.core.AccessSessionRequest
import com.ghostlock.app.core.CleanupDisposition
import com.ghostlock.app.core.SessionPhase
import com.ghostlock.app.jni.Native

/**
 * One native transaction's answer: the protocol status plus whatever the reply carried.
 *
 * The four entries and the wire are opaque above this layer: callers ask for an op and read named
 * fields, never offsets or codes. A refused op is a **successful** round trip here (the refusal
 * travels as status/refusal); AccessResult.Failure is reserved for transport problems -- the
 * library missing, a frame this side could not decode, or a session that is already closed.
 */
class NativeReply internal constructor(
    val op: NativeProtocol.Op,
    val status: NativeProtocol.Status,
    /** Set when the native side answered a status this Kotlin build does not know (fail-closed). */
    val unknownStatus: Int?,
    private val reply: NativeProtocol.Reply,
) {
    /** The named refusal, when native put one in Text. */
    val refusal: String? get() = reply.refusal

    val ok: Boolean get() = status == NativeProtocol.Status.Ok

    fun u64(key: NativeProtocol.Key): ULong? = reply.u64(key)

    fun blob(key: NativeProtocol.Key): ByteArray? = reply.blob(key)

    /** 全部 u64 字段，键是线上的 key 名。 */
    val values: Map<String, ULong> get() = reply.values.entries.associate { it.key.token to it.value }

    /** 全部字节字段，键是线上的 key 名。 */
    val blobs: Map<String, ByteArray> get() = reply.blobs.entries.associate { it.key.token to it.value }

    override fun toString(): String =
        "op=" + op.token + " status=" + (unknownStatus?.let { "unknown(" + it + ")" } ?: status.token) +
            (refusal?.let { " refusal=" + it } ?: "")
}

/**
 * The four JNI entries, wrapped as an AccessSession.
 *
 * The session owns exactly one native handle. Every call is serialised on this object's monitor so
 * two Kotlin callers cannot interleave two ops inside one native session; the native side has its
 * own lock as well (it refuses reentrancy with "busy") -- two locks for one rule on purpose: either
 * end may be the one that notices first.
 *
 * Capability views stay null until the steps batch (J6) publishes descriptors from a snapshot; what
 * this object adds today is the op surface the device gate and the temporary UI drive.
 */
class JniAccessSession internal constructor(
    private val handle: Long,
    /** The backend this session was opened for, as its id token. */
    val backendId: String,
) : AccessSession, SessionInvoker {
    private val gate = Any()

    @Volatile
    private var closed = false

    @Volatile
    private var lastPhase: SessionPhase = SessionPhase.INITIALIZING

    @Volatile
    private var cleanupState: CleanupDisposition = CleanupDisposition.CLEAN

    override val phase: SessionPhase get() = lastPhase

    override val cleanup: CleanupDisposition get() = cleanupState

    /**
     * Run one op.
     *
     * @param values u64 fields; @param blobs byte fields (a path, an image, a secret frame).
     */
    override fun invoke(
        op: NativeProtocol.Op,
        values: Map<NativeProtocol.Key, ULong>,
        blobs: Map<NativeProtocol.Key, ByteArray>,
    ): AccessResult<NativeReply> = synchronized(gate) {
        if (closed) {
            return@synchronized AccessResult.Failure(
                AccessFailure.NOT_READY,
                "session closed (handle retired)",
            )
        }
        val builder = NativeProtocol.Builder(op)
        for ((key, value) in values) {
            builder.u64(key, value)
        }
        for ((key, payload) in blobs) {
            builder.bytes(key, payload)
        }
        val frame = builder.build()
        val answer = try {
            Native.nativeSessionInvoke(handle, op.code, frame)
        } finally {
            frame.fill(0)
        } ?: return@synchronized AccessResult.Failure(
            AccessFailure.NATIVE_FAILURE,
            "native returned no reply frame",
        )
        val reply = try {
            NativeProtocol.parse(answer)
        } catch (bad: NativeProtocol.DecodeException) {
            return@synchronized AccessResult.Failure(
                AccessFailure.NATIVE_FAILURE,
                bad.message ?: "undecodable reply",
            )
        } finally {
            answer.fill(0)
        }
        if (reply.status == NativeProtocol.Status.Indeterminate) {
            /* An indeterminate answer means the native side does not know whether the effect
             *  landed; the session is not clean any more, and nothing may be retried blindly. */
            cleanupState = CleanupDisposition.DIRTY
        }
        AccessResult.Success(NativeReply(op, reply.status, reply.unknownStatus, reply))
    }

    /** Take the session down, then release it. Idempotent, and never throws. */
    override fun close() {
        synchronized(gate) {
            if (closed) {
                return
            }
            closed = true
            val status = Native.nativeSessionShutdown(handle)
            if (status != NativeProtocol.Status.Ok.code) {
                /* A shutdown that did not come back cleanly leaves resources that may still be
                 *  armed; the disposition says so instead of pretending the teardown was clean. */
                cleanupState = CleanupDisposition.DIRTY
            }
            Native.nativeSessionDestroy(handle)
            lastPhase = SessionPhase.CLOSED
        }
    }
}

/**
 * Opens sessions over the JNI boundary.
 *
 * The backend is named by its id token ("cve_2026_43499"); the number the wire carries is derived
 * from that token in one place (backendCodeOf), so no other file spells it.
 */
class JniAccessSessionFactory : AccessSessionFactory {
    override fun open(request: AccessSessionRequest): AccessResult<AccessSession> {
        if (!Native.ensureLoaded()) {
            return AccessResult.Failure(
                AccessFailure.NATIVE_FAILURE,
                "JNI library missing: " + Native.LIBRARY,
            )
        }
        request.consumeProfileFrame().fill(0) // J8 wires the profile; nothing reads it yet
        val backend = backendCodeOf(request.backendId)
            ?: return AccessResult.Failure(
                AccessFailure.INVALID_REQUEST,
                "unknown backend id: " + request.backendId,
            )
        val frame = NativeProtocol.Builder(NativeProtocol.Op.Initialize)
            .u64(NativeProtocol.Key.Backend, backend)
            .build()
        val handle = try {
            Native.nativeSessionCreate(frame)
        } finally {
            frame.fill(0)
        }
        if (handle <= 0) {
            val status = NativeProtocol.Status.entries.firstOrNull { it.code == handle.toInt() }
            return AccessResult.Failure(
                AccessFailure.NATIVE_FAILURE,
                "session refused: " + (status?.token ?: ("status " + handle)),
            )
        }
        return AccessResult.Success(JniAccessSession(handle, request.backendId))
    }

    private companion object {
        /** The one place a backend token becomes the number the wire carries. */
        fun backendCodeOf(token: String): ULong? = when {
            token.endsWith("43499") -> 43499uL
            token.endsWith("43284") -> 43284uL
            else -> null
        }
    }
}
