package com.ghostlock.app.jni

/**
 * The Kotlin half of the boundary: four declarations and nothing else.
 *
 * The four entries are the whole JNI surface (see docs/analysis/jni-two-backend-plan.md §3.1):
 * create a session, invoke one op on it, shut it down, destroy it. Everything else -- which op to
 * run, in what order, with which values -- is Kotlin's, and the op/key/status numbering lives in
 * the native export (build/native/jni-protocol-manifest.tsv) rather than in a hand-copied table.
 *
 * Conventions the native side keeps:
 *  - create answers a **positive handle**, or the negated protocol status code (see the manifest);
 *  - invoke answers a reply frame (never null unless the VM is out of memory); the frame carries
 *    its own status, so a refused op is a frame, not an exception;
 *  - shutdown is idempotent and answers a protocol status code;
 *  - destroy is idempotent; a handle from a destroyed session is refused as stale, never reused.
 *
 * The class must stay named Native: the native library exports Java_com_ghostlock_app_jni_Native_*
 * and a different name would make the lookup fail at run time with an UnsatisfiedLinkError that
 * says only "no implementation found".
 */
object Native {
    /**
     * The name of the library the entries live in.
     *
     * Declared here so a missing load fails at the point of use with a message that names the
     * library, instead of as an UnsatisfiedLinkError from inside a call.
     */
    const val LIBRARY = "ghostlock_jni"

    /** Set once the library has been loaded; guards against loading it twice. */
    @Volatile
    private var loaded = false

    /**
     * Load the library, once.
     *
     * @return true when the library is available.
     * @note A failure here is reported by the caller; it is not swallowed.
     */
    @Synchronized
    fun ensureLoaded(): Boolean {
        if (loaded) {
            return true
        }
        return try {
            System.loadLibrary(LIBRARY)
            loaded = true
            true
        } catch (missing: UnsatisfiedLinkError) {
            false
        }
    }

    /**
     * Create a session from a create frame: an initialize request carrying the backend id (and,
     * once the backend's own batch lands, its parameter entries).
     *
     * @param createFrame the frame, laid out by core/src/jni/session_protocol.hpp.
     * @return a positive handle, or a negative protocol status code.
     */
    external fun nativeSessionCreate(createFrame: ByteArray): Long

    /**
     * Run one op inside a session.
     *
     * @param handle       a handle from [nativeSessionCreate].
     * @param op           the op code, which must equal the op the frame carries.
     * @param requestFrame the request frame.
     * @return the reply frame (it carries its own status), or null only if the VM could not
     *         allocate it.
     */
    external fun nativeSessionInvoke(handle: Long, op: Int, requestFrame: ByteArray): ByteArray?

    /**
     * Take a session down. Idempotent: the second call repeats the first answer and does not touch
     * the backend again.
     *
     * @param handle a handle from [nativeSessionCreate].
     * @return the protocol status code.
     */
    external fun nativeSessionShutdown(handle: Long): Int

    /**
     * Release a session. Idempotent, and safe on a handle that is already gone.
     *
     * @param handle a handle from [nativeSessionCreate]; zero is accepted and ignored.
     */
    external fun nativeSessionDestroy(handle: Long)
}
