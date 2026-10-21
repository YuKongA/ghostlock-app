package com.ghostlock.app.engine.ktstep

/** 受害者 uid 的读回口；K4 的真实实现会走 victim pipe，测试用 fake。 */
fun interface VictimProbe {
    /** 默认 fixture；生产实现必须显式声明 Production。 */
    val kind: ProbeKind get() = ProbeKind.Fixture

    fun uid(): SyscallResult
}
