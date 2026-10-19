package com.ghostlock.app.jni.cve43284

/** 测试用：在 fork 子进程里执行 DFRoot payload，验证 first-inst/jumpback 返回路径。 */
object NativePayloadProbe {
    init {
        System.loadLibrary("ghostlock_jni")
    }

    external fun run(payload: ByteArray): Int
}
