package com.ghostlock.app.jni

/** 收尾参数写入 JNI 入口；native 实现见 core/src/boundary/jni/native_finish.cpp。 */
object NativeFinish {
    /** 确保 libghostlock_jni 已加载后再调用 native。 */
    fun write(path: String, content: String): Int {
        if (!Native.ensureLoaded()) return -1
        return runCatching { writeFile(path, content) }.getOrDefault(-1)
    }

    private external fun writeFile(path: String, content: String): Int
}
