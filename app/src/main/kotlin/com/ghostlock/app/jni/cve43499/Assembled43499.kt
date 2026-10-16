package com.ghostlock.app.jni.cve43499

/**
 * 43499 的**上层包装**：拼装、想写什么、写到哪、收尾都放在这里（与 Assembled43284 同型）。
 *
 * 分工（用户裁定 2026-10-15）：
 * - [Native43499] 只是 JNI 接口（每函数一个设备原语）；
 * - 本类承载顺序、条件、参数选择与收尾；native 不新增可放在这里的逻辑。
 *
 * @param handle [Native43499.create] 给的会话句柄。
 */
class Assembled43499(private val handle: Long) {

    /** 最近一次的调用码与具名拒绝（不吞）。 */
    var lastCode: Int = 0
        private set
    var lastRefusal: String = ""
        private set

    /** 当前的内核访问对象句柄（0 = 没有）。 */
    var accessId: Long = 0L
        private set

    /**
     *  Assembly for initialize: seventeen values, in the order the JNI export documents
     *  (waiter, task, lock, buffer, maxAttempts, armHold, armSequence, parkMs, settleUs,
     *  consumerDelayUs, burstCalls, maxCalls, fallbackLockUs, niceModulus, policy, fakeTask, fakeLock).
     *  The caller decides the values (Kotlin side); null keeps the plain entry point.
     */
    var assembly: LongArray? = null

    private fun refusalSlot(): Array<String> = arrayOf("")

    private fun record(code: Int, refusal: Array<String>): Int {
        lastCode = code
        lastRefusal = refusal[0]
        return code
    }

    /**
     * 按 route 取一个内核访问对象。
     *
     * @param route 档案里的 route 取值（route_catalog 的枚举序号）——由调用方决定，本类不猜。
     * @return 句柄；0 表示没取到（lastRefusal 是原因）。
     */
    fun openAccess(route: Int): Long {
        val refusal = refusalSlot()
        val out = LongArray(1)
        val code = Native43499.makeAccess43499(handle, route, out, refusal)
        record(code, refusal)
        accessId = if (code == 0) out[0] else 0L
        return accessId
    }

    /** 拆掉内核访问对象（谁建谁拆）。 */
    fun closeAccess(): Boolean {
        if (accessId == 0L) {
            return true
        }
        val refusal = refusalSlot()
        val code = Native43499.destroyAccess43499(handle, accessId, refusal)
        record(code, refusal)
        accessId = 0L
        return code == 0
    }

    /** 分步：初始化访问对象（路由与位置由调用方给）。 */
    fun initializeAccess(route: Int, offset: Long, offsetSet: Boolean): Boolean {
        if (accessId == 0L) {
            lastCode = -1
            lastRefusal = "init:no-handle"
            return false
        }
        val refusal = refusalSlot()
        val assemb = assembly
        val code = if (assemb != null && assemb.isNotEmpty()) {
            Native43499.initialize43499Full(handle, accessId, route, offset, offsetSet, assemb, refusal)
        } else {
            Native43499.initialize43499(handle, accessId, route, offset, offsetSet, refusal)
        }
        record(code, refusal)
        return code == 0
    }

    /** 分步：写一块（通道 PiWindow）。 */
    fun writeBlock(route: Int, offset: Long, address: Long, block: ByteArray): Boolean {
        if (accessId == 0L) {
            lastCode = -1
            lastRefusal = "write:no-handle"
            return false
        }
        val refusal = refusalSlot()
        val code = Native43499.write43499(handle, accessId, route, offset, address, block, refusal)
        record(code, refusal)
        return code == 0
    }

    /** 分步：读一块；成功返回读到的字节（长度可能短于请求），失败返回 null（lastRefusal 有因）。 */
    fun readBlock(route: Int, offset: Long, address: Long, bytes: Int): ByteArray? {
        if (accessId == 0L) {
            lastCode = -1
            lastRefusal = "read:no-handle"
            return null
        }
        val refusal = refusalSlot()
        val out = ByteArray(bytes.toInt().coerceAtLeast(0))
        val outLen = LongArray(1)
        val code = Native43499.read43499(handle, accessId, route, offset, address, bytes, out, outLen, refusal)
        record(code, refusal)
        if (code != 0) {
            return null
        }
        val count = outLen[0].toInt().coerceIn(0, out.size)
        return out.copyOf(count)
    }

    /** 分步：关掉访问对象（收尾第一步，先 shutdown 再 closeAccess）。 */
    fun shutdownAccess(): Boolean {
        if (accessId == 0L) {
            return true
        }
        val refusal = refusalSlot()
        val code = Native43499.shutdown43499(handle, accessId, refusal)
        record(code, refusal)
        return code == 0
    }

    /** 收尾：先拆访问对象，再关会话。 */
    fun close(): Boolean {
        val accessClosed = closeAccess()
        val refusal = refusalSlot()
        val shutdown = record(Native43499.shutdown(handle, refusal), refusal)
        val second = refusalSlot()
        val destroyed = record(Native43499.destroy(handle, second), second)
        return accessClosed && shutdown == 0 && destroyed == 0
    }
}
