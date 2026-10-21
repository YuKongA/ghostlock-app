package com.ghostlock.app.engine.ktstep

/** 可脚本化的 victim uid 读回。 */
class FakeVictimProbe(var result: SyscallResult = SyscallResult.Ok(0L)) : VictimProbe {
    var calls = 0

    override fun uid(): SyscallResult {
        ++calls
        return result
    }
}
