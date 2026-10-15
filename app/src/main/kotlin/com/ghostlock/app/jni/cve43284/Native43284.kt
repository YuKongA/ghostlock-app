package com.ghostlock.app.jni.cve43284

/**
 * libghostlock43284.so 的函数面（D2 装配为 43499 已完成；43284 仍是数组参数版，等它的同构装配）。
 *
 * 这里如实反映**当前**的 native 签名：参数用 long[]/byte[]，失败原因由 refusal(handle) 取。
 * 43284 的 DTO 化（Init43284 + 九个请求组）落地后，本文件会跟着换成 DTO 参数。
 */
object Native43284 {
    /** 这个 backend 的库名（CMake 目标 ghostlock43284）。 */
    const val LIBRARY = "ghostlock43284"

    private var loaded = false

    /** @return 库能不能加载；失败时 false。 */
    fun ensureLoaded(): Boolean {
        if (loaded) {
            return true
        }
        loaded = try {
            System.loadLibrary(LIBRARY)
            true
        } catch (missing: UnsatisfiedLinkError) {
            false
        }
        return loaded
    }

    external fun create(): Long

    external fun initialize(handle: Long, params: LongArray, refusalOut: Array<String>): Int

    external fun snapshot(handle: Long, out: LongArray, refusalOut: Array<String>): Int

    /** 绑定一个角色到一条路径。@param out 一个 long：票据。 */
    external fun resolveTarget(
        handle: Long,
        role: Int,
        path: ByteArray,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int

    external fun writeBlock(
        handle: Long,
        ticket: Long,
        offset: Long,
        block: ByteArray,
        refusalOut: Array<String>,
    ): Int

    external fun readBlock(
        handle: Long,
        ticket: Long,
        offset: Long,
        out: ByteArray,
        refusalOut: Array<String>,
    ): Int

    external fun trigger(handle: Long, delayMs: Long, refusalOut: Array<String>): Int

    external fun waitTerminal(
        handle: Long,
        budgetMs: Long,
        pollMs: Long,
        moduleName: ByteArray,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int

    /** @return 本构建自带的字节；失败时 null（名字在 refusalOut[0]）。 */
    external fun embeddedImage(handle: Long, role: Int, refusalOut: Array<String>): ByteArray?

    external fun hookPayload(handle: Long, refusalOut: Array<String>): ByteArray?

    external fun hookWords(handle: Long, out: LongArray, refusalOut: Array<String>): Int

    external fun shutdown(handle: Long, refusalOut: Array<String>): Int

    external fun destroy(handle: Long, refusalOut: Array<String>): Int

    external fun snapshotFields(): Array<String>

    external fun initializeFields(): Array<String>

    external fun hookFields(): Array<String>
}
