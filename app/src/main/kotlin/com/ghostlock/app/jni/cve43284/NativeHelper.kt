package com.ghostlock.app.jni.cve43284

import com.ghostlock.app.log.log

/** 薄 JNI：crash_dump helper 读与页缓存 drop；不持有会话。 */
object NativeHelper {
    init {
        System.loadLibrary("ghostlock_jni")
    }

    private external fun nativeRead(path: String, offset: Long): ByteArray?
    private external fun nativeFadviseDrop(path: String): Boolean
    private external fun nativeTriggerOrphan(delayMs: Int): Boolean
    private external fun nativeLastError(): String

    fun read(path: String, offset: Long): ByteArray? {
        log.verbose("NativeHelper.read path=" + path + " offset=" + offset)
        val out = nativeRead(path, offset)
        log.debug("NativeHelper.read result bytes=" + (out?.size ?: -1))
        return out
    }

    fun fadviseDrop(path: String): Boolean {
        log.verbose("NativeHelper.fadviseDrop path=" + path)
        val ok = nativeFadviseDrop(path)
        log.debug("NativeHelper.fadviseDrop result ok=" + ok)
        return ok
    }

    fun triggerOrphan(delayMs: Int): Boolean {
        log.verbose("NativeHelper.triggerOrphan delayMs=" + delayMs)
        val ok = nativeTriggerOrphan(delayMs)
        log.debug("NativeHelper.triggerOrphan result ok=" + ok)
        return ok
    }

    fun lastError(): String {
        val text = nativeLastError()
        log.debug("NativeHelper.lastError value=" + text)
        return text
    }
}
