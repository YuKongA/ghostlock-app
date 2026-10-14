package com.ghostlock.app.jni

/**
 * The Kotlin half of the boundary: three declarations and nothing else.
 *
 * These were not present anywhere in the tree -- the native file declared
 * Java_com_ghostlock_app_jni_Native_* but no Kotlin side ever named them, so this file is the
 * first appearance of the Kotlin half rather than an edit of an existing one.
 *
 * The frame the entries carry is described in core/src/jni/session_frame.hpp. Kotlin fills a
 * ByteArray there, native reads it; the layout is one proposal among three and is not yet
 * accepted, which is why the third party in the build -- the loader -- is asked to fail loudly
 * if the library is missing rather than silently returning a default.
 */
/**
 * The name of this object is not free: the native file exports
 * Java_com_ghostlock_app_jni_Native_* , so the class holding the external declarations must be
 * called Native. Naming it anything else makes the JNI lookup fail at run time with an
 * UnsatisfiedLinkError that says only "no implementation found".
 */
object Native {
    /**
     * The name of the library the three entries live in.
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
     * Create a session by handing the native side the whole frame.
     *
     * @param frame  the parameters, laid out as session_frame.hpp describes.
     * @return a positive handle, or a negative refusal code.
     */
    external fun nativeSessionCreate(frame: ByteArray): Long

    /**
     * Run one write in an existing session.
     *
     * @param handle  a handle from nativeSessionCreate.
     * @param frame   the frame for this write.
     * @return 0 on a verified write, negative on a named refusal.
     */
    external fun nativeRunOneWrite(handle: Long, frame: ByteArray): Int

    /**
     * Release a session.
     *
     * @param handle  a handle from nativeSessionCreate; zero is accepted and ignored.
     */
    external fun nativeSessionDestroy(handle: Long)
}
