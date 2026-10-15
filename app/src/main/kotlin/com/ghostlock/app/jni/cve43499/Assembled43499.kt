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
